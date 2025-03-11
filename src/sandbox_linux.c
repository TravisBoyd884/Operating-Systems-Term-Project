/* #define _GNU_SOURCE */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/reg.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include "sandbox_common.h"

// Linux x86_64 syscall numbers
#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_OPENAT 257
#define SYS_UNLINK 87
#define SYS_UNLINKAT 263
#define MAX_PATH 4096
#define TRUE 1
#define FALSE 0

// Global variables to track open files
#define MAX_TRACKED_FDS 128
typedef struct {
  int fd;
  char path[MAX_PATH];
  int active;
} tracked_fd_t;

tracked_fd_t tracked_fds[MAX_TRACKED_FDS] = {0};

// Helper function to check if the path should be monitored
int should_monitor_path(const char* path) {
  if (!path) return 0;
  
  // Skip system libraries and other system paths
  if (strncmp(path, "/etc/", 5) == 0 ||
      strncmp(path, "/usr/lib/", 9) == 0 ||
      strncmp(path, "/lib/", 5) == 0 ||
      strncmp(path, "/dev/", 5) == 0 ||
      strncmp(path, "/proc/", 6) == 0 ||
      strncmp(path, "/sys/", 5) == 0) {
    return 0;
  }
  
  // In a real implementation, you would have a more sophisticated
  // way to determine which paths to monitor. For now, we'll 
  // monitor all non-system paths.
  return 1;
}

// Helper to track a new file descriptor and its path
void track_fd(int fd, const char* path) {
  if (fd < 0 || !path) return;
  
  // Find an empty slot or update existing fd
  for (int i = 0; i < MAX_TRACKED_FDS; i++) {
    if (!tracked_fds[i].active || tracked_fds[i].fd == fd) {
      tracked_fds[i].fd = fd;
      strncpy(tracked_fds[i].path, path, MAX_PATH - 1);
      tracked_fds[i].path[MAX_PATH - 1] = '\0'; // Ensure null termination
      tracked_fds[i].active = 1;
      return;
    }
  }
}

// Helper to get path for a file descriptor
const char* get_fd_path(int fd) {
  for (int i = 0; i < MAX_TRACKED_FDS; i++) {
    if (tracked_fds[i].active && tracked_fds[i].fd == fd) {
      return tracked_fds[i].path;
    }
  }
  return NULL;
}

// Function to read a string from the child's memory
char* read_string(pid_t child_pid, unsigned long addr) {
  static char buffer[MAX_PATH];
  int i = 0;
  
  while (i < MAX_PATH - sizeof(long)) {
    long data;
    
    errno = 0;
    data = ptrace(PTRACE_PEEKDATA, child_pid, addr + i, NULL);
    if (errno != 0) {
      perror("ptrace peek");
      break;
    }
    
    memcpy(buffer + i, &data, sizeof(long));
    
    // Check for null terminator in the bytes we just read
    int j;
    for (j = 0; j < sizeof(long); j++) {
      if (buffer[i + j] == '\0') {
        return buffer;
      }
    }
    
    i += sizeof(long);
  }
  
  // Ensure null termination
  buffer[MAX_PATH - 1] = '\0';
  return buffer;
}

// Check if a file exists
int file_exists(const char *filepath) {
  FILE *file = fopen(filepath, "r");
  if (file) {
    fclose(file);
    return 1;
  }
  return 0;
}

// Global variable to track if we're entering or exiting syscall
int in_syscall = 0;
unsigned long saved_syscall = 0;

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <program_to_sandbox> [args...]\n", argv[0]);
    return 1;
  }

  char *program = argv[1];    // Program to run
  
  printf("%sSandbox monitoring: %s%s\n", INFO_COLOR, program, COLOR_RESET);
  printf("%sFile operations monitored: read, write, open, and delete%s\n", INFO_COLOR, COLOR_RESET);

  // Fork a child process
  pid_t child_pid = fork();
  
  if (child_pid == -1) {
    perror("fork failed");
    return 1;
  }
  
  if (child_pid == 0) {
    // Child process
    
    // Request to be traced
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
      perror("ptrace traceme");
      exit(1);
    }
    
    // Execute the program
    execvp(program, &argv[1]);
    
    // If we get here, execvp failed
    perror("execvp failed");
    exit(1);
  }
  
  // Parent process (sandbox)
  int status;
  struct user_regs_struct regs;
  
  // Wait for child to stop after execvp (first trap)
  waitpid(child_pid, &status, 0);
  
  /* This makes it easy for the tracer to 
  *  distinguish normal traps from those caused by a system call. */  
  if (ptrace(PTRACE_SETOPTIONS, child_pid, 0, 
             PTRACE_O_TRACESYSGOOD) == -1) {
    perror("ptrace setoptions");
    return 1;
  }
  
  printf("%sStarting to trace process with PID %d%s\n", INFO_COLOR, child_pid, COLOR_RESET);
  
  // Continue to the next syscall
  if (ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL) == -1) {
    perror("ptrace syscall");
    return 1;
  }
  
  // Monitor the child process
  while (1) {
    // Wait for the child to stop
    if (waitpid(child_pid, &status, 0) == -1) {
      perror("waitpid");
      break;
    }
    
    // Check if the child has exited
    if (WIFEXITED(status)) {
      printf("Child process exited with status %d\n", WEXITSTATUS(status));
      break;
    }
    
    // Check if the child was terminated by a signal
    if (WIFSIGNALED(status)) {
      printf("Child process terminated by signal %d\n", WTERMSIG(status));
      break;
    }
    
    // Check if this is a syscall-stop
    if (WIFSTOPPED(status) && (WSTOPSIG(status) & 0x80)) {
      // Get the registers to see what syscall was made
      if (ptrace(PTRACE_GETREGS, child_pid, NULL, &regs) == -1) {
        perror("ptrace getregs");
        break;
      }
      
      if (!in_syscall) {
        // Entering syscall
        in_syscall = 1;
        saved_syscall = regs.orig_rax;
        
// Check for monitored syscalls
        if (saved_syscall == SYS_UNLINK || saved_syscall == SYS_UNLINKAT || 
            saved_syscall == SYS_READ || saved_syscall == SYS_WRITE || 
            saved_syscall == SYS_OPEN || saved_syscall == SYS_OPENAT) {
          
          char* path = NULL;
          char* operation = NULL;
          char details[MAX_PATH + 100] = {0};
          int should_monitor = 0;
          
          // Get operation type and path based on syscall
          if (saved_syscall == SYS_UNLINK) {
            operation = "delete";
            path = read_string(child_pid, regs.rdi);
            sprintf(details, "delete file: %s", path);
            should_monitor = should_monitor_path(path);
          } else if (saved_syscall == SYS_UNLINKAT) {
            operation = "delete";
            int dirfd = (int)regs.rdi;
            path = read_string(child_pid, regs.rsi);
            sprintf(details, "delete file: %s (dirfd: %d)", path, dirfd);
            should_monitor = should_monitor_path(path);
          } else if (saved_syscall == SYS_READ) {
            operation = "read";
            int fd = (int)regs.rdi;
            const char* fd_path = get_fd_path(fd);
            if (fd_path) {
              sprintf(details, "read from file: %s (fd: %d)", fd_path, fd);
              should_monitor = should_monitor_path(fd_path);
            } else {
              sprintf(details, "read from file descriptor: %d", fd);
              should_monitor = 0; // Skip untracked file descriptors
            }
          } else if (saved_syscall == SYS_WRITE) {
            operation = "write";
            int fd = (int)regs.rdi;
            const char* fd_path = get_fd_path(fd);
            if (fd_path) {
              sprintf(details, "write to file: %s (fd: %d)", fd_path, fd);
              should_monitor = should_monitor_path(fd_path);
            } else {
              sprintf(details, "write to file descriptor: %d", fd);
              should_monitor = 0; // Skip untracked file descriptors
            }
          } else if (saved_syscall == SYS_OPEN) {
            operation = "open";
            path = read_string(child_pid, regs.rdi);
            int flags = (int)regs.rsi;
            sprintf(details, "open file: %s (flags: 0x%x)", path, flags);
            should_monitor = should_monitor_path(path);
          } else if (saved_syscall == SYS_OPENAT) {
            operation = "open";
            int dirfd = (int)regs.rdi;
            path = read_string(child_pid, regs.rsi);
            int flags = (int)regs.rdx;
            sprintf(details, "open file: %s (dirfd: %d, flags: 0x%x)", path, dirfd, flags);
            should_monitor = should_monitor_path(path);
          }
          
          // Only prompt for monitored paths
          if (should_monitor) {
            printf("\n%s[!] ALERT: Program is attempting to %s%s\n", ALERT_COLOR, details, COLOR_RESET);
            printf("%sAllow this operation? (y/n): %s", PROMPT_COLOR, COLOR_RESET);
            fflush(stdout);
            
            char response;
            if (scanf(" %c", &response) != 1) {
              response = 'n'; // Default to blocking if read fails
            }
            
            // Clear input buffer
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            
            if (response == 'y' || response == 'Y') {
              printf("%s[+] ALLOWED: User permitted %s operation%s\n", ALLOWED_COLOR, operation, COLOR_RESET);
              // Allow the syscall to proceed normally
            } else {
              printf("%s[-] BLOCKED: User denied %s operation%s\n", BLOCKED_COLOR, operation, COLOR_RESET);
              
              // Set syscall to -1 to prevent it from executing
              regs.orig_rax = -1;
              if (ptrace(PTRACE_SETREGS, child_pid, NULL, &regs) == -1) {
                perror("ptrace setregs");
              }
            }
          }
        }
      } else {
        // Exiting syscall
        in_syscall = 0;
        
        // Special handling for successful open/openat calls to track file descriptors
        if ((saved_syscall == SYS_OPEN || saved_syscall == SYS_OPENAT) && regs.rax >= 0) {
          int new_fd = (int)regs.rax;
          char* path = NULL;
          
          if (saved_syscall == SYS_OPEN) {
            // For open, get the path from arg1
            path = read_string(child_pid, regs.rdi);
          } else if (saved_syscall == SYS_OPENAT) {
            // For openat, get the path from arg2
            path = read_string(child_pid, regs.rsi);
          }
          
          if (path) {
            // Track this new file descriptor
            track_fd(new_fd, path);
          }
        }
        
        // If we blocked a syscall, make it return EPERM
        if (regs.orig_rax == -1) {
          regs.rax = -EPERM;
          if (ptrace(PTRACE_SETREGS, child_pid, NULL, &regs) == -1) {
            perror("ptrace setregs for return value");
          }
        }
      }
    } else if (WIFSTOPPED(status)) {
      // Got a regular signal - forward it
      int sig = WSTOPSIG(status);
      printf("Child got signal: %d\n", sig);
      
      if (ptrace(PTRACE_SYSCALL, child_pid, NULL, sig) == -1) {
        perror("ptrace syscall (signal forwarding)");
        break;
      }
      continue;
    }
    
    // Continue to the next syscall
    if (ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL) == -1) {
      perror("ptrace syscall");
      break;
    }
  }
  
  return 0;
}

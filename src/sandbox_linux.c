#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/reg.h>
#include <sys/syscall.h>
#include <sys/user.h>

// Linux x86_64 unlink syscall numbers
#define SYS_UNLINK 87
#define SYS_UNLINKAT 263
#define MAX_PATH 4096
#define TRUE 1
#define FALSE 0

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
  
  printf("Sandbox monitoring: %s\n", program);
  printf("All file deletion operations will be monitored\n");

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
  
  // Set Linux-specific options
  if (ptrace(PTRACE_SETOPTIONS, child_pid, 0, 
             PTRACE_O_TRACESYSGOOD) == -1) {
    perror("ptrace setoptions");
    return 1;
  }
  
  printf("Starting to trace process with PID %d\n", child_pid);
  
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
        
        // Check for unlink syscalls
        if (saved_syscall == SYS_UNLINK || saved_syscall == SYS_UNLINKAT) {
          // Get the file path
          char* path;
          if (saved_syscall == SYS_UNLINK) {
            path = read_string(child_pid, regs.rdi);
            printf("\n🔔 ALERT: Program is attempting to delete file: %s\n", path);
          } else { // SYS_UNLINKAT
            int dirfd = (int)regs.rdi;
            path = read_string(child_pid, regs.rsi);
            printf("\n🔔 ALERT: Program is attempting to delete file: %s (dirfd: %d)\n", 
                   path, dirfd);
          }
          
          printf("Allow this operation? (y/n): ");
          fflush(stdout);
          
          char response;
          if (scanf(" %c", &response) != 1) {
            response = 'n'; // Default to blocking if read fails
          }
          
          // Clear input buffer
          int c;
          while ((c = getchar()) != '\n' && c != EOF);
          
          if (response == 'y' || response == 'Y') {
            printf("✅ ALLOWED: User permitted file deletion operation\n");
            // Allow the syscall to proceed normally
          } else {
            printf("🛡️ BLOCKED: User denied file deletion operation\n");
            
            // Set syscall to -1 to prevent it from executing
            regs.orig_rax = -1;
            if (ptrace(PTRACE_SETREGS, child_pid, NULL, &regs) == -1) {
              perror("ptrace setregs");
            }
          }
        }
      } else {
        // Exiting syscall
        in_syscall = 0;
        
        // If we blocked a syscall, make it return EPERM
        if ((saved_syscall == SYS_UNLINK || saved_syscall == SYS_UNLINKAT) && 
            regs.orig_rax == -1) {
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
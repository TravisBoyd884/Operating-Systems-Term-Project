#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Platform-specific includes and definitions
#ifdef MACOS
  #include <sys/syscall.h>
  #include <sys/ptrace.h>
  #include <mach/mach.h>
  #include <mach/thread_status.h>
  #include <mach/machine/thread_status.h>
  #include <libproc.h>
  
  // Define missing constants for macOS if needed
  #ifndef PT_TRACE_ME
    #define PT_TRACE_ME 0
  #endif
  #ifndef PT_STEP
    #define PT_STEP 9
  #endif
  #ifndef PT_CONTINUE
    #define PT_CONTINUE 7
  #endif
  #ifndef PT_KILL
    #define PT_KILL 8
  #endif
  
  // macOS unlink syscall number
  #define SYS_UNLINK 10
#elif defined(LINUX)
  #include <sys/reg.h>
  #include <sys/syscall.h>
  #include <sys/user.h>
  
  // Linux x86_64 unlink syscall number
  #define SYS_UNLINK 87
#endif

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
#if defined(MACOS)
    data = ptrace(PT_READ_D, child_pid, (caddr_t)(addr + i), 0);
#elif defined(LINUX)
    data = ptrace(PTRACE_PEEKDATA, child_pid, addr + i, NULL);
#endif
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

// For macOS: check if a file exists
int file_exists(const char *filepath) {
  FILE *file = fopen(filepath, "r");
  if (file) {
    fclose(file);
    return 1;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <program_to_sandbox> <file_to_protect>\n", argv[0]);
    return 1;
  }

  char *filepath = argv[1];    // Program to run
  char *protected_file = argv[2]; // File to protect
  
  printf("Sandbox monitoring: %s\n", filepath);
  printf("Protected file: %s\n", protected_file);
  
  // Verify the file exists before we start monitoring
  if (!file_exists(protected_file)) {
    fprintf(stderr, "Error: Protected file '%s' does not exist\n", protected_file);
    return 1;
  }
  
  // Fork a child process
  pid_t child_pid = fork();
  
  if (child_pid == -1) {
    perror("fork failed");
    return 1;
  }
  
  if (child_pid == 0) {
    // Child process
    
#if defined(MACOS)
    // Request to be traced by parent
    if (ptrace(PT_TRACE_ME, 0, (caddr_t)0, 0) == -1) {
      perror("ptrace traceme");
      exit(1);
    }
#elif defined(LINUX)
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
      perror("ptrace traceme");
      exit(1);
    }
#endif
    
    // Stop so parent can set up tracing
    raise(SIGSTOP);
    
    // Execute the program that was requested
    execl(filepath, filepath, protected_file, NULL);
    
    // If execl fails, try to find it in PATH
    if (errno == ENOENT) {
      char *path_env = getenv("PATH");
      if (path_env) {
        char *path_copy = strdup(path_env);
        char *dir = strtok(path_copy, ":");
        char full_path[MAX_PATH];
        
        while (dir != NULL) {
          snprintf(full_path, MAX_PATH, "%s/%s", dir, filepath);
          execl(full_path, filepath, protected_file, NULL);
          dir = strtok(NULL, ":");
        }
        
        free(path_copy);
      }
    }
    
    perror("execl failed");
    exit(1);
  }
  
  // Parent process (sandbox)
  int status;
  
  // Wait for child to stop with SIGSTOP
  waitpid(child_pid, &status, 0);
  
  if (!WIFSTOPPED(status)) {
    fprintf(stderr, "Child didn't stop as expected\n");
    return 1;
  }
  
#if defined(MACOS)
  // Continue the child process
  if (ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0) == -1) {
    perror("ptrace continue");
    return 1;
  }
#elif defined(LINUX)
  // Set Linux-specific options
  if (ptrace(PTRACE_SETOPTIONS, child_pid, 0, PTRACE_O_TRACESYSGOOD) == -1) {
    perror("ptrace setoptions");
    return 1;
  }
  
  // Continue to the next syscall
  if (ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL) == -1) {
    perror("ptrace syscall");
    return 1;
  }
#endif
  
  // Monitor the child process
  while (1) {
    // Wait for the child to stop
    waitpid(child_pid, &status, 0);
    
    // Check if the child has exited
    if (WIFEXITED(status)) {
      printf("Child process exited with status %d\n", WEXITSTATUS(status));
      
      // After the child exits, verify the file still exists
      if (!file_exists(protected_file)) {
        printf("❌ WARNING: The protected file '%s' was deleted despite the sandbox!\n", protected_file);
      } else {
        printf("✅ Protected file '%s' is still intact.\n", protected_file);
      }
      
      break;
    }
    
    // Check if the child was terminated by a signal
    if (WIFSIGNALED(status)) {
      printf("Child process terminated by signal %d\n", WTERMSIG(status));
      break;
    }
    
    // Check if the file has already been deleted
    if (!file_exists(protected_file)) {
      printf("❌ ALERT: File '%s' has already been deleted! Killing the process.\n", protected_file);
      
      // Kill the child process
#if defined(MACOS)
      ptrace(PT_KILL, child_pid, (caddr_t)0, 0);
#elif defined(LINUX)
      ptrace(PTRACE_KILL, child_pid, NULL, NULL);
#endif
      waitpid(child_pid, &status, 0);
      return 1;
    }
    
#if defined(MACOS)
    // On macOS, we need to be more careful about how we detect and handle unlink operations
    if (WIFSTOPPED(status)) {
      int sig = WSTOPSIG(status);
      
      // If we get a trap signal, this could be an unlink
      if (sig == SIGTRAP) {
        // We can't easily determine which syscall is being made on macOS with ptrace alone,
        // so we'll ask the user if they want to allow potential file operations
        
        printf("\n🔔 ALERT: Program '%s' might be attempting file operations on '%s'\n", filepath, protected_file);
        printf("Allow this operation? (y/n): ");
        fflush(stdout);
        
        char response;
        if (scanf(" %c", &response) != 1) {
          response = 'n'; // Default to blocking if read fails
        }
        
        // Clear any remaining characters in the input buffer
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        
        if (response == 'y' || response == 'Y') {
          printf("✅ ALLOWED: User permitted operation\n");
          
          // Continue execution
          ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0);
        } else {
          printf("🛡️ BLOCKED: User denied operation\n");
          
          // Kill the process to prevent any file operations
          printf("Terminating process to protect the file...\n");
          ptrace(PT_KILL, child_pid, (caddr_t)0, 0);
          waitpid(child_pid, &status, 0);
          printf("Process terminated successfully.\n");
          
          // Verify the file is still there
          if (file_exists(protected_file)) {
            printf("✅ Protected file '%s' is intact.\n", protected_file);
          } else {
            printf("❌ WARNING: The protected file was deleted before we could kill the process!\n");
          }
          
          break;
        }
      } else if (sig != SIGSTOP) {
        // Forward any other signals to the child
        ptrace(PT_CONTINUE, child_pid, (caddr_t)1, sig);
      } else {
        // Continue execution for SIGSTOP
        ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0);
      }
    } else {
      // If the child is not stopped, just continue it
      ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0);
    }
#elif defined(LINUX)
    // Linux specific code for tracking syscalls
    if (WIFSTOPPED(status) && WSTOPSIG(status) == (SIGTRAP | 0x80)) {
      // This is a syscall-stop
      struct user_regs_struct regs;
      
      if (ptrace(PTRACE_GETREGS, child_pid, NULL, &regs) == -1) {
        perror("ptrace getregs");
        break;
      }
      
      // Check if it's an unlink syscall
      if (regs.orig_rax == SYS_UNLINK) {
        // Get the file path from the child's memory
        char* path = read_string(child_pid, regs.rdi);
        
        // Check if this matches our protected file
        if (strcmp(path, protected_file) == 0) {
          printf("\n🔔 ALERT: Program is attempting to delete file: %s\n", path);
          printf("Allow this operation? (y/n): ");
          fflush(stdout);
          
          char response;
          if (scanf(" %c", &response) != 1) {
            response = 'n'; // Default to blocking if read fails
          }
          
          if (response == 'y' || response == 'Y') {
            printf("✅ ALLOWED: User permitted unlink operation\n");
            // Allow the syscall to proceed normally
          } else {
            printf("🛡️ BLOCKED: User denied unlink operation\n");
            
            // Change the return value to EPERM (Operation not permitted)
            regs.rax = -EPERM;
            
            if (ptrace(PTRACE_SETREGS, child_pid, NULL, &regs) == -1) {
              perror("ptrace setregs");
            }
          }
        }
      }
      
      // Continue to the next syscall
      if (ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL) == -1) {
        perror("ptrace syscall");
        break;
      }
    } else if (WIFSTOPPED(status)) {
      // Forward any other signals to the child
      int sig = WSTOPSIG(status);
      
      if (ptrace(PTRACE_SYSCALL, child_pid, NULL, sig) == -1) {
        perror("ptrace syscall (signal forwarding)");
        break;
      }
    }
#endif
  }
  
  return 0;
}
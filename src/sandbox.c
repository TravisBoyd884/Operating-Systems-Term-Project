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
  #include <sys/proc_info.h>
  #include <libproc.h>
  // macOS uses different register structure
  #if defined(__x86_64__)
    #include <sys/user.h>
    #define SYS_UNLINK 10
  #elif defined(__arm64__)
    #define SYS_UNLINK 10
    // ARM64 registers will be defined below
  #endif
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
    // macOS uses PT_READ_D instead of PTRACE_PEEKDATA
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

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <filepath>\n", argv[0]);
    return 1;
  }

  char *filepath = argv[1];
  printf("Sandbox monitoring: %s\n", filepath);
  
  // Create child process
  pid_t child_pid = fork();
  
  if (child_pid == -1) {
    perror("fork failed");
    return 1;
  }
  
  if (child_pid == 0) {
    // Child process
    
#if defined(MACOS)
    // Request to be traced by parent (macOS)
    if (ptrace(PT_TRACE_ME, 0, (caddr_t)0, 0) == -1) {
      perror("ptrace traceme (macOS)");
      exit(1);
    }
#elif defined(LINUX)
    // Request to be traced by parent (Linux)
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
      perror("ptrace traceme (Linux)");
      exit(1);
    }
#endif
    
    // Stop so parent can set up tracing
    raise(SIGSTOP);
    
    // Try to run the program specified in argv[1] (when resumed by parent)
    // First try if it's a full path or in current directory
    execl(filepath, filepath, argv[2], NULL);
    
    // If that fails, search in PATH
    if (errno == ENOENT) {
      // Get the PATH environment variable
      char *path_env = getenv("PATH");
      if (path_env) {
        char *path_copy = strdup(path_env);
        char *dir = strtok(path_copy, ":");
        char full_path[MAX_PATH];
        
        // Try each directory in PATH
        while (dir != NULL) {
          snprintf(full_path, MAX_PATH, "%s/%s", dir, filepath);
          execl(full_path, filepath, argv[2], NULL);
          dir = strtok(NULL, ":");
        }
        
        free(path_copy);
      }
    }
    
    // Should not reach here unless all exec attempts fail
    perror("execl failed");
    exit(1);
  }
  
  // Parent process - trace the child
  int status;
  
  // Wait for child to stop with SIGSTOP
  waitpid(child_pid, &status, 0);
  
  if (!WIFSTOPPED(status)) {
    fprintf(stderr, "Child didn't stop as expected\n");
    return 1;
  }
  
#if defined(MACOS)
  // macOS doesn't have PTRACE_SETOPTIONS or PTRACE_O_TRACESYSGOOD
  // Just continue with PT_CONTINUE on macOS
  if (ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0) == -1) {
    perror("ptrace continue (macOS)");
    return 1;
  }
#elif defined(LINUX)
  // Set tracing options
  if (ptrace(PTRACE_SETOPTIONS, child_pid, 0, PTRACE_O_TRACESYSGOOD) == -1) {
    perror("ptrace setoptions (Linux)");
    return 1;
  }
  
  // Let the traced process run until the next system call
  if (ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL) == -1) {
    perror("ptrace syscall (Linux)");
    return 1;
  }
#endif
  
  int entering_syscall = TRUE;
  int should_block = FALSE;
  
  // Monitor system calls
  while (1) {
    // Wait for syscall-stop or other events
    waitpid(child_pid, &status, 0);
    
    // Check if child has exited
    if (WIFEXITED(status)) {
      printf("Child process exited with status %d\n", WEXITSTATUS(status));
      break;
    }
    
    // Check if child got a signal
    if (WIFSIGNALED(status)) {
      printf("Child terminated by signal %d\n", WTERMSIG(status));
      break;
    }
    
#if defined(MACOS)
    // macOS doesn't use SIGTRAP | 0x80 for syscalls
    // Just check for SIGTRAP
    if (!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGTRAP)) {
      ptrace(PT_STEP, child_pid, (caddr_t)1, 0);
      continue;
    }
#elif defined(LINUX)
    // If the child was stopped by a signal that's not SIGTRAP+0x80, continue
    if (!WIFSTOPPED(status) || (WSTOPSIG(status) != (SIGTRAP | 0x80))) {
      ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL);
      continue;
    }
#endif
    
    // Get the registers
#if defined(MACOS)
  #if defined(__x86_64__)
    struct x86_thread_state64_t regs;
    unsigned int count = sizeof(regs)/sizeof(int);
    if (ptrace(PT_GETREGS, child_pid, (caddr_t)&regs, 0) == -1) {
      perror("ptrace getregs (macOS x86_64)");
      break;
    }
    
    if (entering_syscall) {
      // This is a syscall entry
      
      // Check if it's the unlink syscall (note: macOS uses different registers)
      if (regs.__rax == SYS_UNLINK) {
        // Get the path argument
        char* path = read_string(child_pid, regs.__rdi);
        printf("Unlink syscall detected for: %s\n", path);
  #elif defined(__arm64__)
    struct arm_thread_state64_t regs;
    unsigned int count = sizeof(regs)/sizeof(int);
    if (ptrace(PT_GETREGS, child_pid, (caddr_t)&regs, 0) == -1) {
      perror("ptrace getregs (macOS ARM64)");
      break;
    }
    
    if (entering_syscall) {
      // This is a syscall entry
      
      // Check if it's the unlink syscall
      if (regs.__x[16] == SYS_UNLINK) {
        // Get the path argument (x0 is the first argument on ARM64)
        char* path = read_string(child_pid, regs.__x[0]);
        printf("Unlink syscall detected for: %s\n", path);
  #endif
#elif defined(LINUX)
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, child_pid, NULL, &regs) == -1) {
      perror("ptrace getregs (Linux)");
      break;
    }
    
    if (entering_syscall) {
      // This is a syscall entry
      
      // Check if it's the unlink syscall
      if (regs.orig_rax == SYS_UNLINK) {
        // Get the path argument
        char* path = read_string(child_pid, regs.rdi);
        printf("Unlink syscall detected for: %s\n", path);
#endif
        
        // Prompt user for permission to delete the file
        printf("🔔 ALERT: Program is attempting to delete file: %s\n", path);
        printf("Allow this operation? (y/n): ");
        fflush(stdout);
        
        char response;
        if (scanf(" %c", &response) != 1) {
          response = 'n'; // Default to blocking if read fails
        }
        
        if (response == 'y' || response == 'Y') {
          printf("✅ ALLOWED: User permitted unlink operation\n");
          should_block = FALSE;
        } else {
          printf("🛡️ BLOCKED: User denied unlink operation\n");
          should_block = TRUE;
          
          // Instead of killing the process, we'll let it continue but 
          // will modify the return value in the syscall exit handling
        }
      }
    } else {
      // This is a syscall exit
      
      // If we decided to block this syscall, change the return value to an error
      if (should_block) {
        printf("Setting syscall return value to -EPERM (Operation not permitted)\n");
        
#if defined(MACOS)
  #if defined(__x86_64__)
        regs.__rax = -EPERM;  // Operation not permitted
        
        if (ptrace(PT_SETREGS, child_pid, (caddr_t)&regs, 0) == -1) {
          perror("ptrace setregs (macOS x86_64)");
        } else {
          printf("Successfully modified syscall return value\n");
        }
  #elif defined(__arm64__)
        regs.__x[0] = -EPERM;  // x0 holds the return value on ARM64
        
        if (ptrace(PT_SETREGS, child_pid, (caddr_t)&regs, 0) == -1) {
          perror("ptrace setregs (macOS ARM64)");
        } else {
          printf("Successfully modified syscall return value\n");
        }
  #endif
#elif defined(LINUX)
        regs.rax = -EPERM;  // Operation not permitted
        
        if (ptrace(PTRACE_SETREGS, child_pid, NULL, &regs) == -1) {
          perror("ptrace setregs (Linux)");
        } else {
          printf("Successfully modified syscall return value\n");
        }
#endif
        
        should_block = FALSE;
      }
    }
    
    // Toggle between syscall entry/exit
    entering_syscall = !entering_syscall;
    
#if defined(MACOS)
    // Continue to the next syscall (macOS)
    if (ptrace(PT_STEP, child_pid, (caddr_t)1, 0) == -1) {
      perror("ptrace step (macOS)");
      break;
    }
#elif defined(LINUX)
    // Continue to the next syscall (Linux)
    if (ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL) == -1) {
      perror("ptrace syscall (Linux)");
      break;
    }
#endif
  }
  
  return 0;
}

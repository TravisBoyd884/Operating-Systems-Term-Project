#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <unistd.h>

// Use the correct syscall number for unlink on x86_64
#define SYS_UNLINK 87

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
    
    // Request to be traced by parent
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
      perror("ptrace traceme");
      exit(1);
    }
    
    // Stop so parent can set up tracing
    raise(SIGSTOP);
    
    // Run the program specified in argv[1] (when resumed by parent)
    execl(filepath, filepath, argv[2], NULL);
    
    // Should not reach here unless exec fails
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
  
  // Set tracing options
  if (ptrace(PTRACE_SETOPTIONS, child_pid, 0, PTRACE_O_TRACESYSGOOD) == -1) {
    perror("ptrace setoptions");
    return 1;
  }
  
  // Let the traced process run until the next system call
  if (ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL) == -1) {
    perror("ptrace syscall");
    return 1;
  }
  
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
    
    // If the child was stopped by a signal that's not SIGTRAP+0x80, continue
    if (!WIFSTOPPED(status) || (WSTOPSIG(status) != (SIGTRAP | 0x80))) {
      ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL);
      continue;
    }
    
    // Get the registers
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, child_pid, NULL, &regs) == -1) {
      perror("ptrace getregs");
      break;
    }
    
    if (entering_syscall) {
      // This is a syscall entry
      
      // Check if it's the unlink syscall
      if (regs.orig_rax == SYS_UNLINK) {
        // Get the path argument
        char* path = read_string(child_pid, regs.rdi);
        printf("Unlink syscall detected for: %s\n", path);
        
        // Check if this path is in the protected directory
        if (strstr(path, "/home/travis/HomeWork/Operating_Systems/term_project/test/") != NULL) {
          printf("🛡️ BLOCKED: unlink operation on test directory file\n");
          
          // Immediately kill the child process to prevent the unlink
          printf("Terminating child process to prevent unlink\n");
          if (ptrace(PTRACE_KILL, child_pid, NULL, NULL) == -1) {
            perror("ptrace kill");
          }
          break; // Exit the monitoring loop
        } else {
          printf("✅ ALLOWED: unlink operation outside test directory\n");
          should_block = FALSE;
        }
      }
    } else {
      // This is a syscall exit
      
      // If we decided to block this syscall, change the return value to an error
      if (should_block) {
        printf("Setting syscall return value to -EPERM (Operation not permitted)\n");
        regs.rax = -EPERM;  // Operation not permitted
        
        if (ptrace(PTRACE_SETREGS, child_pid, NULL, &regs) == -1) {
          perror("ptrace setregs");
        } else {
          printf("Successfully modified syscall return value\n");
        }
        
        should_block = FALSE;
      }
    }
    
    // Toggle between syscall entry/exit
    entering_syscall = !entering_syscall;
    
    // Continue to the next syscall
    if (ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL) == -1) {
      perror("ptrace syscall");
      break;
    }
  }
  
  return 0;
}

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
#include <sys/syscall.h>
#include <sys/ptrace.h>
#include <mach/mach.h>
#include <mach/thread_status.h>
#include <mach/machine/thread_status.h>
#include <libproc.h>
#include <sys/user.h>
#include "sandbox_common.h"

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

// macOS syscall numbers
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_UNLINK 10
#define SYS_UNLINKAT 472
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
    data = ptrace(PT_READ_D, child_pid, (caddr_t)(addr + i), 0);
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

// Function to get the current process name
char* get_process_name(pid_t pid) {
  static char name[MAX_PATH];
  proc_name(pid, name, sizeof(name));
  return name;
}

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
    
    // Request to be traced by parent
    if (ptrace(PT_TRACE_ME, 0, (caddr_t)0, 0) == -1) {
      perror("ptrace traceme");
      exit(1);
    }
    
    // Execute the program with all arguments
    execvp(program, &argv[1]);
    
    // If we get here, execvp failed
    perror("execvp failed");
    exit(1);
  }
  
  // Parent process (sandbox)
  int status;
  
  // Wait for child to stop after execvp
  waitpid(child_pid, &status, 0);
  
  if (!WIFSTOPPED(status)) {
    fprintf(stderr, "Child didn't stop as expected\n");
    return 1;
  }
  
  // Get the process name for better output
  char* proc_name = get_process_name(child_pid);
  
  printf("%sStarting to trace process '%s' with PID %d%s\n", INFO_COLOR, proc_name, child_pid, COLOR_RESET);
  
  // Continue the child process
  if (ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0) == -1) {
    perror("ptrace continue");
    return 1;
  }
  
  // Set a trap for the first instruction to catch syscalls
  ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0);

  // Monitor the child process
  while (1) {
    // Wait for the child to stop
    waitpid(child_pid, &status, 0);
    
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
    
    if (WIFSTOPPED(status)) {
      int sig = WSTOPSIG(status);
      
      if (sig == SIGTRAP) {
        // Get the thread state to access registers
        x86_thread_state64_t thread_state;
        mach_msg_type_number_t thread_state_count = x86_THREAD_STATE64_COUNT;
        thread_act_t thread;
        task_t task;
        
        task_for_pid(mach_task_self(), child_pid, &task);
        thread_act_port_array_t thread_list;
        mach_msg_type_number_t thread_count;
        
        task_threads(task, &thread_list, &thread_count);
        thread = thread_list[0];  // Get the first thread
        
        thread_get_state(thread, x86_THREAD_STATE64, (thread_state_t)&thread_state, &thread_state_count);
        
        // Get syscall number from rax register
        uint64_t syscall_num = thread_state.__rax;
        uint64_t arg1 = thread_state.__rdi;  // First argument (often a file path or descriptor)
        
        // Check for file-related syscalls
        char* operation_type = NULL;
        char* filepath = NULL;
        int is_file_operation = FALSE;
        
        switch (syscall_num) {
          case SYS_OPEN:
            operation_type = "open";
            filepath = read_string(child_pid, arg1);
            is_file_operation = TRUE;
            break;
          case SYS_READ:
            operation_type = "read";
            is_file_operation = TRUE;
            break;
          case SYS_WRITE:
            operation_type = "write";
            is_file_operation = TRUE;
            break;
          case SYS_UNLINK:
            operation_type = "delete";
            filepath = read_string(child_pid, arg1);
            is_file_operation = TRUE;
            break;
          case SYS_UNLINKAT:
            operation_type = "delete";
            filepath = read_string(child_pid, thread_state.__rsi);  // Second argument for unlinkat
            is_file_operation = TRUE;
            break;
        }
        
        if (is_file_operation) {
          if (filepath) {
            printf("\n%s[!] ALERT: Process '%s' (PID %d) attempting %s operation on: %s%s\n", 
                  ALERT_COLOR, proc_name, child_pid, operation_type, filepath, COLOR_RESET);
          } else {
            printf("\n%s[!] ALERT: Process '%s' (PID %d) attempting %s operation%s\n", 
                  ALERT_COLOR, proc_name, child_pid, operation_type, COLOR_RESET);
          }
          
          printf("%sAllow this operation? (y/n): %s", PROMPT_COLOR, COLOR_RESET);
          fflush(stdout);
          
          char response;
          if (scanf(" %c", &response) != 1) {
            response = 'n'; // Default to blocking if read fails
          }
          
          // Clear any remaining characters in the input buffer
          int c;
          while ((c = getchar()) != '\n' && c != EOF);
          
          if (response == 'y' || response == 'Y') {
            printf("%s[+] ALLOWED: User permitted %s operation%s\n", ALLOWED_COLOR, operation_type, COLOR_RESET);
            
            // Continue execution
            ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0);
          } else {
            printf("%s[-] BLOCKED: User denied %s operation%s\n", BLOCKED_COLOR, operation_type, COLOR_RESET);
            
            // Kill the process to prevent any file operations
            printf("%sTerminating process to protect files...%s\n", BLOCKED_COLOR, COLOR_RESET);
            ptrace(PT_KILL, child_pid, (caddr_t)0, 0);
            waitpid(child_pid, &status, 0);
            printf("%sProcess terminated successfully.%s\n", BLOCKED_COLOR, COLOR_RESET);
            break;
          }
        } else {
          // Continue for non-file operations
          ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0);
        }
        
        // Cleanup
        vm_deallocate(mach_task_self(), (vm_address_t)thread_list, thread_count * sizeof(thread_act_t));
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
  }
  
  return 0;
}
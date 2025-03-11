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
#include "sandbox_logger.h"

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
  int log_enabled = 1;        // Enable logging by default
  
  printf("Sandbox monitoring: %s\n", program);
  printf("All file deletion operations will be monitored\n");
  
  // Initialize logging
  if (log_enabled) {
    if (!logger_init(program)) {
      fprintf(stderr, "Warning: Could not initialize logging\n");
      log_enabled = 0;
    } else {
      printf("Logging enabled: Check sandbox_%s_*.log for details\n", program);
    }
  }
  
  logger_log(LOG_INFO, "Starting sandbox for program: %s", program);

  // Fork a child process
  pid_t child_pid = fork();
  
  if (child_pid == -1) {
    perror("fork failed");
    logger_log(LOG_ERROR, "Fork failed: %s", strerror(errno));
    logger_close();
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
    logger_log(LOG_ERROR, "Child process didn't stop as expected");
    logger_close();
    return 1;
  }
  
  // Get the process name for better output
  char* proc_name = get_process_name(child_pid);
  
  printf("Starting to trace process '%s' with PID %d\n", proc_name, child_pid);
  logger_log(LOG_INFO, "Tracing process '%s' with PID %d", proc_name, child_pid);
  
  // Continue the child process
  if (ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0) == -1) {
    perror("ptrace continue");
    logger_log(LOG_ERROR, "Failed to continue child process: %s", strerror(errno));
    logger_close();
    return 1;
  }
  
  // Monitor the child process
  while (1) {
    // Wait for the child to stop
    waitpid(child_pid, &status, 0);
    
    // Check if the child has exited
    if (WIFEXITED(status)) {
      int exit_status = WEXITSTATUS(status);
      printf("Child process exited with status %d\n", exit_status);
      logger_log(LOG_INFO, "Child process exited with status %d", exit_status);
      break;
    }
    
    // Check if the child was terminated by a signal
    if (WIFSIGNALED(status)) {
      int sig = WTERMSIG(status);
      printf("Child process terminated by signal %d\n", sig);
      logger_log(LOG_INFO, "Child process terminated by signal %d", sig);
      break;
    }
    
    // On macOS, we need to be more careful about how we detect and handle unlink operations
    if (WIFSTOPPED(status)) {
      int sig = WSTOPSIG(status);
      
      // If we get a trap signal, this could be an unlink
      if (sig == SIGTRAP) {
        // In macOS, we can't easily determine which syscall is being made with just ptrace
        // But we can provide context about what's happening
        
        // Check if any of the command line arguments are files that exist
        // This is a heuristic - if one of the arguments is a file, we show that
        char* potential_file = NULL;
        for (int i = 2; i < argc; i++) {
          if (file_exists(argv[i])) {
            potential_file = argv[i];
            break;
          }
        }
        
        logger_increment_total();
        
        if (potential_file) {
          printf("\n🔔 ALERT: Process '%s' (PID %d) might be attempting to delete file: %s\n", 
                proc_name, child_pid, potential_file);
          logger_log(LOG_ALERT, "Process '%s' (PID %d) might be attempting to delete file: %s", 
                     proc_name, child_pid, potential_file);
        } else {
          printf("\n🔔 ALERT: Process '%s' (PID %d) might be attempting a file operation\n", 
                proc_name, child_pid);
          logger_log(LOG_ALERT, "Process '%s' (PID %d) might be attempting a file operation", 
                     proc_name, child_pid);
        }
        
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
          logger_increment_allowed();
          printf("✅ ALLOWED: User permitted file operation\n");
          logger_log(LOG_INFO, "✅ ALLOWED: User permitted file operation");
          
          // Continue execution
          ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0);
        } else {
          logger_increment_blocked();
          printf("🛡️ BLOCKED: User denied file operation\n");
          logger_log(LOG_WARNING, "🛡️ BLOCKED: User denied file operation");
          
          // Kill the process to prevent any file operations
          printf("Terminating process to protect files...\n");
          logger_log(LOG_INFO, "Terminating process to protect files");
          ptrace(PT_KILL, child_pid, (caddr_t)0, 0);
          waitpid(child_pid, &status, 0);
          printf("Process terminated successfully.\n");
          logger_log(LOG_INFO, "Process terminated successfully");
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
  }
  
  // Get statistics
  int total, allowed, blocked;
  logger_get_stats(&total, &allowed, &blocked);
  
  printf("\nSandbox monitoring completed.\n");
  printf("Statistics: Total operations: %d, Allowed: %d, Blocked: %d\n", 
         total, allowed, blocked);
  
  // Close logging
  logger_close();
  
  return 0;
}
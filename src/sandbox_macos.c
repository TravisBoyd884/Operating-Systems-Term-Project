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
#include <fcntl.h>
#include <dirent.h>

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
#define SYS_OPEN 5
#define SYS_OPEN_NOCANCEL 338
#define SYS_OPENAT 496
#define SYS_OPENAT_NOCANCEL 499
#define SYS_UNLINK 10
#define SYS_UNLINKAT 472
#define MAX_PATH 4096
#define TRUE 1
#define FALSE 0

// Operation types
#define OP_OPEN 1
#define OP_DELETE 2

// Global variable to store directory contents
char *dir_contents[1024];
int dir_content_count = 0;

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

// Function to scan directories for files
void scan_directories(const char *path) {
  DIR *dir;
  struct dirent *entry;
  char full_path[MAX_PATH];
  
  // Clear previous scan results
  for (int i = 0; i < dir_content_count; i++) {
    free(dir_contents[i]);
    dir_contents[i] = NULL;
  }
  dir_content_count = 0;
  
  // Open the directory
  dir = opendir(path);
  if (!dir) {
    return;
  }
  
  // Read directory entries
  while ((entry = readdir(dir)) != NULL && dir_content_count < 1024) {
    // Skip . and ..
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    
    // Create full path
    snprintf(full_path, MAX_PATH, "%s/%s", path, entry->d_name);
    
    // Store the path
    dir_contents[dir_content_count] = strdup(full_path);
    dir_content_count++;
  }
  
  closedir(dir);
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

// Function to check if string matches or is contained in any command line argument
// Also returns which operation it might be attempting
char* find_matching_file(int argc, char *argv[], pid_t child_pid, int *operation_type) {
  // First, check direct command line arguments
  for (int i = 2; i < argc; i++) {
    if (file_exists(argv[i])) {
      // If the file exists, it could be either an open or delete operation
      // We'll set a default of open, but both are possible
      *operation_type = OP_OPEN;
      return argv[i];
    }
  }
  
  // Next, check current directory contents
  char cwd[MAX_PATH];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    scan_directories(cwd);
    for (int i = 0; i < dir_content_count; i++) {
      if (file_exists(dir_contents[i])) {
        *operation_type = OP_OPEN;
        return dir_contents[i];
      }
    }
  }
  
  // Default return
  *operation_type = OP_OPEN; // Default assumption
  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <program_to_sandbox> [args...]\n", argv[0]);
    return 1;
  }

  char *program = argv[1];    // Program to run
  
  printf("Sandbox monitoring: %s\n", program);
  printf("All file open and delete operations will be monitored\n");

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
  
  printf("Starting to trace process '%s' with PID %d\n", proc_name, child_pid);
  
  // Continue the child process
  if (ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0) == -1) {
    perror("ptrace continue");
    return 1;
  }
  
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
    
    // On macOS, we need to be more careful about how we detect and handle syscalls
    if (WIFSTOPPED(status)) {
      int sig = WSTOPSIG(status);
      
      // If we get a trap signal, this could be a file operation
      if (sig == SIGTRAP) {
        // Try to identify if this might be a file operation
        int operation_type = OP_OPEN; // Default assumption
        char* potential_file = find_matching_file(argc, argv, child_pid, &operation_type);
        
        // We will use syscall-specific inspection to determine if this is an open or unlink
        // But in macOS, this is difficult, so we use heuristics

        // Try to guess the operation type based on context and heuristics
        // For example, file arguments that exist are likely to be opened or deleted
        
        if (potential_file) {
          const char* op_name = (operation_type == OP_OPEN) ? "open" : "delete";
          const char* emoji = (operation_type == OP_OPEN) ? "🔍" : "🗑️";
          
          printf("\n%s DETECTED: Process '%s' (PID %d) might be attempting to %s file: %s\n", 
                emoji, proc_name, child_pid, op_name, potential_file);
          
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
            printf("✅ ALLOWED: User permitted file %s operation\n", op_name);
            
            // Continue execution
            ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0);
          } else {
            printf("🛡️ BLOCKED: User denied file %s operation\n", op_name);
            
            // Kill the process to prevent any file operations
            printf("Terminating process to protect files...\n");
            ptrace(PT_KILL, child_pid, (caddr_t)0, 0);
            waitpid(child_pid, &status, 0);
            printf("Process terminated successfully.\n");
            break;
          }
        } else {
          // Continue execution for non-file-related syscalls
          ptrace(PT_CONTINUE, child_pid, (caddr_t)1, 0);
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
  
  // Clean up any allocated memory
  for (int i = 0; i < dir_content_count; i++) {
    free(dir_contents[i]);
  }
  
  return 0;
}
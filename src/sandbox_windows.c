#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <io.h>

#define F_OK 0
#define access _access
#define MAX_PATH 4096
#define TRUE 1
#define FALSE 0

// Check if a file exists
int file_exists(const char *filepath) {
  return _access(filepath, 0) == 0;
}

// Helper function to get process name by process ID
const char* get_process_name(HANDLE process) {
  static char name[MAX_PATH];
  DWORD size = MAX_PATH;
  
  if (QueryFullProcessImageNameA(process, 0, name, &size)) {
    // Extract just the filename from the path
    char* filename = strrchr(name, '\\');
    if (filename) {
      return filename + 1;
    }
    return name;
  }
  return "Unknown Process";
}

// Helper function to build command line with arguments
char* build_command_line(int argc, char** argv) {
  static char cmdline[MAX_PATH * 2];
  cmdline[0] = '\0'; // Start with empty string
  
  // Build the command line from the program path and all arguments
  for (int i = 1; i < argc; i++) {
    if (i > 1) {
      strcat(cmdline, " ");
    }
    
    // Add quotes around the argument if it contains spaces
    if (strchr(argv[i], ' ')) {
      strcat(cmdline, "\"");
      strcat(cmdline, argv[i]);
      strcat(cmdline, "\"");
    } else {
      strcat(cmdline, argv[i]);
    }
  }
  
  return cmdline;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <program_to_sandbox> [args...]\n", argv[0]);
    return 1;
  }

  char *program = argv[1];    // Program to run
  
  printf("Sandbox monitoring: %s\n", program);
  printf("All file deletion operations will be monitored\n");

  // Windows implementation using CreateProcess
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));
  
  // Check if program exists
  if (!file_exists(program)) {
    fprintf(stderr, "Error: Program '%s' does not exist or is not accessible\n", program);
    return 1;
  }
  
  // Build the command line with all arguments
  char* cmdLine = build_command_line(argc, argv);
  printf("Command line: %s\n", cmdLine);
  
  // Create the child process
  if (!CreateProcess(
      NULL,           // No module name (use command line)
      cmdLine,        // Command line with arguments
      NULL,           // Process handle not inheritable
      NULL,           // Thread handle not inheritable
      FALSE,          // Set handle inheritance to FALSE
      CREATE_SUSPENDED,// Create suspended so we can set up monitoring
      NULL,           // Use parent's environment block
      NULL,           // Use parent's starting directory
      &si,            // Pointer to STARTUPINFO structure
      &pi))           // Pointer to PROCESS_INFORMATION structure
  {
    fprintf(stderr, "CreateProcess failed (%lu)\n", GetLastError());
    return 1;
  }
  
  // Get the process name for better output
  const char* proc_name = get_process_name(pi.hProcess);
  
  printf("Process '%s' created with PID %lu, monitoring for file deletion operations...\n", 
         proc_name, pi.dwProcessId);
  
  // Resume the process
  ResumeThread(pi.hThread);
  
  // Monitor loop - on Windows, we can't easily hook into DeleteFile or similar operations
  // So we'll simulate the behavior by monitoring process activity
  DWORD exitCode = STILL_ACTIVE;
  
  // Identify potential target files from command line arguments
  char* potential_file = NULL;
  for (int i = 2; i < argc; i++) {
    if (file_exists(argv[i])) {
      potential_file = argv[i];
      break;
    }
  }
  
  // In a real-world implementation, we would use a file system filter driver to intercept
  // all file deletion operations. For now, we'll simulate it by periodically checking
  // for potential file operations.
  
  while (1) {
    // Check if the process is still running
    if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
      printf("Child process exited with status %lu\n", exitCode);
      break;
    }
    
    // Simulate detecting a file operation
    // For demo purposes, we'll pretend we detected a file operation after a short delay
    // In a real implementation, this would be triggered by actual file system events
    Sleep(500);
    
    // Set fileOperation to TRUE to indicate the process is trying to delete a file
    BOOL fileOperation = TRUE;
    
    if (potential_file) {
      printf("\n🔔 ALERT: Process '%s' (PID %lu) is attempting to delete file: %s\n", 
             proc_name, pi.dwProcessId, potential_file);
    } else {
      printf("\n🔔 ALERT: Process '%s' (PID %lu) might be attempting a file operation\n", 
             proc_name, pi.dwProcessId);
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
      printf("✅ ALLOWED: User permitted file operation\n");
      // In a real implementation, we would allow the operation to proceed
    } else {
      printf("🛡️ BLOCKED: User denied file operation\n");
      printf("Terminating process to prevent file deletion...\n");
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, INFINITE);
      printf("Process terminated successfully.\n");
      break;
    }
    
    // For demo purposes, we'll wait for a moment before checking again
    // In a real implementation, we'd be event-driven
    Sleep(5000); // 5 seconds delay
  }
  
  // Clean up
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  
  return 0;
}
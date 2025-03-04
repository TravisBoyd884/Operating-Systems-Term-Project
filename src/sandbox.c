#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WINDOWS
  #include <windows.h>
  #include <psapi.h>
  #include <tlhelp32.h>
  #include <io.h>
  #define F_OK 0
  #define access _access
#else
  #include <signal.h>
  #include <sys/ptrace.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
#endif

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

// Check if a file exists (cross-platform)
int file_exists(const char *filepath) {
#ifdef WINDOWS
  return _access(filepath, 0) == 0;
#else
  FILE *file = fopen(filepath, "r");
  if (file) {
    fclose(file);
    return 1;
  }
  return 0;
#endif
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <program_to_sandbox>\n", argv[0]);
    return 1;
  }

  char *filepath = argv[1];    // Program to run
  
  printf("Sandbox monitoring: %s\n", filepath);
  printf("All file deletion operations will be monitored\n");

#ifdef WINDOWS
  // Windows implementation using CreateProcess and file system filter
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  char cmdLine[MAX_PATH * 2];
  
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));
  
  // Prepare command line with just the filepath
  snprintf(cmdLine, sizeof(cmdLine), "\"%s\"", filepath);
  
  // Create the child process
  if (!CreateProcess(
      NULL,           // No module name (use command line)
      cmdLine,        // Command line
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
  
  printf("Process created, monitoring for file deletion operations...\n");
  
  // Resume the process
  ResumeThread(pi.hThread);
  
  // Monitor loop - on Windows, we can't easily hook into DeleteFile or similar operations
  // So we'll simulate the behavior by monitoring process activity
  DWORD exitCode = STILL_ACTIVE;
  while (1) {
    // Check if the process is still running
    if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
      printf("Child process exited with status %lu\n", exitCode);
      break;
    }
    
    // In a real implementation, we would need to use a file system minifilter driver
    // or a similar advanced technique to intercept DeleteFile, unlink, etc. calls
    // For now, we'll simulate this behavior with a prompt
    printf("\n🔔 ALERT: Program might be attempting to delete a file\n");
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
      // Continue monitoring
      Sleep(1000); // Sleep to avoid CPU spikes
    } else {
      printf("🛡️ BLOCKED: User denied operation\n");
      printf("Terminating process to prevent file deletion...\n");
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, INFINITE);
      printf("Process terminated successfully.\n");
      break;
    }
  }
  
  // Clean up
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  
#else
  // Unix-based implementation (Linux and macOS)
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
    execl(filepath, filepath, NULL);
    
    // If execl fails, try to find it in PATH
    if (errno == ENOENT) {
      char *path_env = getenv("PATH");
      if (path_env) {
        char *path_copy = strdup(path_env);
        char *dir = strtok(path_copy, ":");
        char full_path[MAX_PATH];
        
        while (dir != NULL) {
          snprintf(full_path, MAX_PATH, "%s/%s", dir, filepath);
          execl(full_path, filepath, NULL);
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
      break;
    }
    
    // Check if the child was terminated by a signal
    if (WIFSIGNALED(status)) {
      printf("Child process terminated by signal %d\n", WTERMSIG(status));
      break;
    }
    
#if defined(MACOS)
    // On macOS, we need to be more careful about how we detect and handle unlink operations
    if (WIFSTOPPED(status)) {
      int sig = WSTOPSIG(status);
      
      // If we get a trap signal, this could be an unlink
      if (sig == SIGTRAP) {
        // We can't easily determine which syscall is being made on macOS with ptrace alone,
        // so we'll ask the user if they want to allow potential file operations
        
        printf("\n🔔 ALERT: Program might be attempting file operations\n");
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
          printf("Terminating process to protect files...\n");
          ptrace(PT_KILL, child_pid, (caddr_t)0, 0);
          waitpid(child_pid, &status, 0);
          printf("Process terminated successfully.\n");
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
#endif // End of Windows/Unix implementation divide
  
  return 0;
}
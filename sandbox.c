#include <stdio.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// List of blocked system calls
int blocked_syscalls[] = {
  SYS_unlink // Delete a file (87)
  SYS_rmdir // Delete a directory (84)
}

int main(int argc, char *argv[]) {
  char *filepath = argv[1];
  printf("%s \n", filepath);
  pid_t child = fork();

  if (child == 0) {

    // Child process: Run the malicious program
    ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    execl("./unlink", filepath, NULL);
  } else {

    // Parent process: Monitor system calls
    int status;
    while (1) {
      wait(&status);
      if (WIFEXITED(status))
        break;

      // Get the system call number using ptrace
      long syscall = ptrace(PTRACE_PEEKUSER, child, 8 * ORIG_RAX, NULL);

      // Check if the system call is unlink (system call number 87 on x86_64)
      if (syscall == 87 &&
          !strcmp(filepath, "/home/travis/HomeWork/Operating_Systems/"
                            "term_project/file.txt")) {
        printf("Blocked unlink system call!\n");

        // Prevent the system call from executing
        ptrace(PTRACE_KILL, child, NULL, NULL);
        break;
      }

      // Continue the program
      ptrace(PTRACE_SYSCALL, child, NULL, NULL);
    }
  }

  return 0;
}

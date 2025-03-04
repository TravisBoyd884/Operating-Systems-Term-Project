#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <windows.h>
  #define unlink _unlink
  #include <io.h>
#else
  #include <unistd.h>
#endif

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <filepath>\n", argv[0]);
    return 1;
  }
  
  const char *filepath = argv[1];
  printf("Attempting to delete: %s\n", filepath);

  // Attempt to delete the file
  int result = unlink(filepath);
  
  if (result == 0) {
    printf("File '%s' successfully deleted.\n", filepath);
    return 0;
  } else {
    perror("Error deleting file");
    #ifdef _WIN32
      return GetLastError();
    #else
      return errno;
    #endif
  }
}

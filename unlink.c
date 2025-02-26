#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  const char *filepath = argv[1];
  printf("%s \n", argv[1]);

  // Attempt to delete the file
  if (unlink(filepath) == 0) {
    printf("File '%s' successfully deleted.\n", filepath);
  } else {
    perror("Error deleting file");
  }

  return 0;
}

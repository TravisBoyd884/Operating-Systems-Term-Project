#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define BUFFER_SIZE 4096

int main(int argc, char *argv[]) {
  const char *filename;
  
  // Check if a filename was provided as argument
  if (argc < 2) {
    printf("Usage: %s <file_to_read_and_modify>\n", argv[0]);
    return 1;
  }

  filename = argv[1];
  printf("Target file: %s\n", filename);
  
  // Open the file using open() syscall
  printf("Attempting to open the file...\n");
  int fd = open(filename, O_RDWR);
  if (fd == -1) {
    perror("Failed to open file");
    return 1;
  }
  printf("Successfully opened file with descriptor: %d\n", fd);
  
  // Read the file contents using read() syscall
  char buffer[BUFFER_SIZE];
  ssize_t bytes_read;
  
  printf("\nReading file contents...\n");
  printf("------- File Contents Begin -------\n");
  
  // Read the contents of the file and print to stdout
  while ((bytes_read = read(fd, buffer, BUFFER_SIZE - 1)) > 0) {
    buffer[bytes_read] = '\0';  // Null-terminate the string
    printf("%s", buffer);
  }
  
  if (bytes_read == -1) {
    perror("Failed to read file");
    close(fd);
    return 1;
  }
  
  printf("\n------- File Contents End -------\n");
  
  // Write to the file using write() syscall
  const char *malicious_text = "\n\nThis file has been modified by malicious_file_operations.c\n";
  
  printf("\nAttempting to modify the file...\n");
  
  // Seek to the end of the file
  if (lseek(fd, 0, SEEK_END) == -1) {
    perror("Failed to seek to end of file");
    close(fd);
    return 1;
  }
  
  // Write the malicious text
  ssize_t bytes_written = write(fd, malicious_text, strlen(malicious_text));
  if (bytes_written == -1) {
    perror("Failed to write to file");
    close(fd);
    return 1;
  }
  
  printf("Successfully wrote %zd bytes to the file\n", bytes_written);
  
  // Close the file
  close(fd);
  
  printf("\nOperation completed successfully.\n");
  printf("The file has been modified.\n");
  
  return 0;
}
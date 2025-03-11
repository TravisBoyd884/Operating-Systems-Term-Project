#include <stdio.h>
#include <unistd.h>

int main() {
    // Create a test file
    const char* test_file = "testfile.txt";
    FILE* file = fopen(test_file, "w");
    if (file) {
        fprintf(file, "This is a test file.\n");
        fclose(file);
        printf("Created test file: %s\n", test_file);
    } else {
        perror("Error creating test file");
        return 1;
    }

    // Attempt to delete the test file
    printf("Attempting to delete file: %s\n", test_file);
    if (unlink(test_file) == 0) {
        printf("File deleted successfully.\n");
    } else {
        perror("Error deleting file");
    }

    // Attempt to delete a non-existent file
    const char* non_existent_file = "nonexistent.txt";
    printf("Attempting to delete non-existent file: %s\n", non_existent_file);
    if (unlink(non_existent_file) == 0) {
        printf("File deleted successfully.\n");
    } else {
        perror("Error deleting file");
    }

    return 0;
}
#ifndef SANDBOX_COMMON_H
#define SANDBOX_COMMON_H

// Common definitions used across all platform implementations
#define MAX_PATH 4096
#define TRUE 1
#define FALSE 0

// Function to check if a file exists (implementation varies by platform)
int file_exists(const char *filepath);

#endif /* SANDBOX_COMMON_H */
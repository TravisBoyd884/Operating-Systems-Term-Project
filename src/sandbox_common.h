#ifndef SANDBOX_COMMON_H
#define SANDBOX_COMMON_H

// Common definitions used across all platform implementations
#define MAX_PATH 4096
#define TRUE 1
#define FALSE 0

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_WHITE   "\033[1;37m"

// Special color macros for specific messages
#define ALERT_COLOR      COLOR_YELLOW
#define ALLOWED_COLOR    COLOR_GREEN
#define BLOCKED_COLOR    COLOR_RED
#define INFO_COLOR       COLOR_BLUE
#define PROMPT_COLOR     COLOR_CYAN

// Function to check if a file exists (implementation varies by platform)
int file_exists(const char *filepath);

#endif /* SANDBOX_COMMON_H */
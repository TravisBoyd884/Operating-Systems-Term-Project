#ifndef SANDBOX_LOGGER_H
#define SANDBOX_LOGGER_H

#include <stdio.h>
#include <stdarg.h>

// Log levels
#define LOG_INFO 0
#define LOG_WARNING 1
#define LOG_ERROR 2
#define LOG_ALERT 3

// Initialize the logging system
// Returns 1 on success, 0 on failure
int logger_init(const char* program_name);

// Close the logging system
void logger_close(void);

// Log a message with the specified level
void logger_log(int level, const char* format, ...);

// Get statistics about logged operations
void logger_get_stats(int* total, int* allowed, int* blocked);

// Increment statistics counters
void logger_increment_total(void);
void logger_increment_allowed(void);
void logger_increment_blocked(void);

#endif /* SANDBOX_LOGGER_H */
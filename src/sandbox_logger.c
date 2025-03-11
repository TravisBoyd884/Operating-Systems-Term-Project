#include "sandbox_logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libgen.h>


// Maximum path length for filenames
#define MAX_PATH 4096

// Log file handle
static FILE* log_file = NULL;

// Statistics
static int total_operations = 0;
static int allowed_operations = 0;
static int blocked_operations = 0;

int logger_init(const char* program_name) {
    char log_filename[MAX_PATH];
    time_t current_time;
    struct tm* time_info;
    char time_str[20];
    char program_base[MAX_PATH];
    
    // Extract the base name from the program path
    strncpy(program_base, program_name, MAX_PATH - 1);
    program_base[MAX_PATH - 1] = '\0';
    char* base = basename(program_base);
    
    // Get current time
    time(&current_time);
    time_info = localtime(&current_time);
    strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", time_info);
    
    // Create log filename: sandbox_<program>_<timestamp>.log
    snprintf(log_filename, MAX_PATH, "../logs/sandbox_%s_%s.log", 
             base, time_str);
    
    // Debug: Print the log filename
    printf("Attempting to create log file: %s\n", log_filename);
    
    // Open log file
    log_file = fopen(log_filename, "w");
    if (!log_file) {
        perror("Failed to open log file");
        return 0;
    }
    
    // Write log header
    fprintf(log_file, "===========================================================\n");
    fprintf(log_file, "Sandbox Monitoring Log - %s\n", base);
    fprintf(log_file, "Started: %s", ctime(&current_time));
    fprintf(log_file, "===========================================================\n\n");
    fflush(log_file);
    
    // Reset statistics
    total_operations = 0;
    allowed_operations = 0;
    blocked_operations = 0;
    
    return 1;
}

void logger_close(void) {
    if (log_file) {
        time_t current_time;
        time(&current_time);
        
        fprintf(log_file, "\n===========================================================\n");
        fprintf(log_file, "Sandbox Monitoring Ended: %s", ctime(&current_time));
        fprintf(log_file, "Statistics: Total operations: %d, Allowed: %d, Blocked: %d\n",
                total_operations, allowed_operations, blocked_operations);
        fprintf(log_file, "===========================================================\n");
        
        fclose(log_file);
        log_file = NULL;
    }
}

void logger_log(int level, const char* format, ...) {
    if (!log_file) return;
    
    // Get current time
    time_t current_time;
    struct tm* time_info;
    char time_str[20];
    
    time(&current_time);
    time_info = localtime(&current_time);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", time_info);
    
    // Write log level prefix
    const char* level_str;
    switch (level) {
        case LOG_INFO:    level_str = "INFO "; break;
        case LOG_WARNING: level_str = "WARN "; break;
        case LOG_ERROR:   level_str = "ERROR"; break;
        case LOG_ALERT:   level_str = "ALERT"; break;
        default:          level_str = "INFO "; break;
    }
    
    fprintf(log_file, "[%s] [%s] ", time_str, level_str);
    
    // Write the actual message with variable arguments
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    
    // Add newline if not already present
    if (format[strlen(format) - 1] != '\n') {
        fprintf(log_file, "\n");
    }
    
    // Flush to ensure logs are written immediately
    fflush(log_file);
}

void logger_get_stats(int* total, int* allowed, int* blocked) {
    if (total) *total = total_operations;
    if (allowed) *allowed = allowed_operations;
    if (blocked) *blocked = blocked_operations;
}

void logger_increment_total(void) {
    total_operations++;
}

void logger_increment_allowed(void) {
    allowed_operations++;
}

void logger_increment_blocked(void) {
    blocked_operations++;
}
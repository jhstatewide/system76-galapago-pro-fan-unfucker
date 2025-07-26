#include "utils.h"
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

int check_proc_instances(const char* proc_name) {
    DIR* dir = opendir("/proc");
    if (dir == NULL) return 0;
    
    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_DIR && isdigit(ent->d_name[0])) {
            char path[512];
            char comm[256];
            if (snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name) >= (int)sizeof(path)) {
                continue; // Skip if path would be truncated
            }
            FILE* f = fopen(path, "r");
            if (f != NULL) {
                if (fgets(comm, sizeof(comm), f) != NULL) {
                    comm[strcspn(comm, "\n")] = 0;
                    if (strcmp(comm, proc_name) == 0) {
                        count++;
                    }
                }
                fclose(f);
            }
        }
    }
    closedir(dir);
    return count;
}

void get_time_string(char* buffer, size_t max, const char* format) {
    time_t timer;
    struct tm tm_info;
    time(&timer);
    localtime_r(&timer, &tm_info);
    strftime(buffer, max, format, &tm_info);
}

void signal_term(void (*handler)(int)) {
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    signal(SIGQUIT, handler);
}

int utils_init(void) {
    // No initialization needed for basic utilities
    return 0;
}

void utils_cleanup(void) {
    // No cleanup needed for basic utilities
} 
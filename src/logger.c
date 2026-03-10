#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

#define LOG_FILE "logs/server.log"

// Mutex to ensure lines don't get mixed up if two threads log at once
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

void write_log(char *message) {
    pthread_mutex_lock(&log_lock);
    
    FILE *fp = fopen(LOG_FILE, "a"); // Append mode
    if (fp == NULL) {
        pthread_mutex_unlock(&log_lock);
        return;
    }

    // Get current time
    time_t now;
    time(&now);
    char *date = ctime(&now);
    date[strlen(date) - 1] = '\0'; // Remove newline at end

    fprintf(fp, "[%s] %s\n", date, message);
    
    fclose(fp);
    pthread_mutex_unlock(&log_lock);
}
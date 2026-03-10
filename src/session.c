#include <stdio.h>
#include <pthread.h>
#include "common.h"

int loggedInUsers[MAX_CLIENTS];
pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER; // Create the lock

void init_sessions() {
    for(int i=0; i<MAX_CLIENTS; i++) {
        loggedInUsers[i] = -1;
    }
}

int create_session(int user_id) {
    pthread_mutex_lock(&session_lock); // LOCK
    
    // Check if already logged in
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(loggedInUsers[i] == user_id) {
            pthread_mutex_unlock(&session_lock); // UNLOCK
            return -1; // Already logged in
        }
    }
    
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(loggedInUsers[i] == -1) {
            loggedInUsers[i] = user_id;
            pthread_mutex_unlock(&session_lock); // UNLOCK
            return i; // Success
        }
    }
    
    pthread_mutex_unlock(&session_lock); // UNLOCK
    return -2; // Server full
}

void remove_session(int user_id) {
    pthread_mutex_lock(&session_lock); // LOCK
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(loggedInUsers[i] == user_id) {
            loggedInUsers[i] = -1;
            break;
        }
    }
    pthread_mutex_unlock(&session_lock); // UNLOCK
}
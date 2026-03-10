#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "common.h"
#include "logger.h"
#include "file_handler.h"
#include <time.h>

#define USER_FILE "data/users.dat"

// Helper to get next ID
int get_next_user_id() {
    int fd = open(USER_FILE, O_RDONLY);
    if (fd == -1) return 1; // File doesn't exist, start at 1

    User u;
    int max_id = 0;
    
    // Read sequentially to find max ID (Locking not strictly needed for just reading size)
    while(read(fd, &u, sizeof(User)) > 0) {
        if (u.id > max_id) max_id = u.id;
    }
    close(fd);
    return max_id + 1;
}

int register_user(const char *username, const char *password, int role, int initial_balance, const char *sec_answer) {
    int fd = open(USER_FILE, O_RDWR | O_CREAT, 0666);
    if (fd == -1) return -1;

    if (lock_record(fd, F_WRLCK, 0, 0) == -1) { close(fd); return -1; }

    User u;
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &u, sizeof(User)) > 0) {
        if (strcmp(u.username, username) == 0) {
            unlock_record(fd, 0, 0); close(fd); return -2; 
        }
    }

    off_t file_size = lseek(fd, 0, SEEK_END);
    int new_id = (file_size / sizeof(User)) + 1;

    User new_user;
    new_user.id = new_id;
    strcpy(new_user.username, username);
    hash_password(password, new_user.password); 
    new_user.role = role;
    new_user.balance = initial_balance;
    new_user.cooldown_until = 0; 
    
    // --- HASH AND SAVE SECURITY ANSWER ---
    hash_password(sec_answer, new_user.security_answer);

    write(fd, &new_user, sizeof(User));
    unlock_record(fd, 0, 0); close(fd);
    
    return new_id;
}

int get_user_balance(int user_id) {
    int fd = open(USER_FILE, O_RDONLY);
    if (fd == -1) return -1;

    off_t offset = (user_id - 1) * sizeof(User);
    
    if (lock_record(fd, F_RDLCK, offset, sizeof(User)) == -1) {
        close(fd); return -1;
    }

    User u;
    lseek(fd, offset, SEEK_SET);
    if (read(fd, &u, sizeof(User)) <= 0) {
        unlock_record(fd, offset, sizeof(User));
        close(fd); 
        return -1;
    }

    unlock_record(fd, offset, sizeof(User));
    close(fd);
    return u.balance;
}

int authenticate_user(const char *username, const char *password) {
    int fd = open(USER_FILE, O_RDONLY);
    if (fd == -1) return -1;

    // Apply a read lock
    if (lock_record(fd, F_RDLCK, 0, 0) == -1) {
        close(fd); 
        return -1;
    }

    User u;
    int authenticated_id = -1; // <--- We declare the variable here!

    // Scan the file for the username
    while (read(fd, &u, sizeof(User)) > 0) {
        if (strcmp(u.username, username) == 0) {
            
            // Hash the incoming password to compare it against the stored hash
            char hashed_incoming[50];
            hash_password(password, hashed_incoming);
            
            // If the hashes match, grab the user's ID
            if (strcmp(u.password, hashed_incoming) == 0) {
                authenticated_id = u.id;
            }
            break; // Username found, no need to keep reading the file
        }
    }

    unlock_record(fd, 0, 0);
    close(fd);
    
    return authenticated_id; // Will return -1 if not found or wrong password
}

int transfer_funds(int from_user_id, int to_user_id, int amount) {
    int fd = open(USER_FILE, O_RDWR);
    if (fd == -1) return -1;

    // DEADLOCK PREVENTION: Always lock smaller ID first
    int first_id = (from_user_id < to_user_id) ? from_user_id : to_user_id;
    int second_id = (from_user_id < to_user_id) ? to_user_id : from_user_id;

    off_t offset1 = (first_id - 1) * sizeof(User);
    off_t offset2 = (second_id - 1) * sizeof(User);

    // 1. Lock First User
    if (lock_record(fd, F_WRLCK, offset1, sizeof(User)) == -1) {
        close(fd); return -1;
    }
    
    // 2. Lock Second User
    if (lock_record(fd, F_WRLCK, offset2, sizeof(User)) == -1) {
        unlock_record(fd, offset1, sizeof(User)); // Rollback
        close(fd); return -1;
    }

    // 3. Read Both Users
    User u1, u2;
    lseek(fd, offset1, SEEK_SET); read(fd, &u1, sizeof(User));
    lseek(fd, offset2, SEEK_SET); read(fd, &u2, sizeof(User));

    // 4. Identify Payer and Payee (since we swapped IDs for locking)
    User *payer = (u1.id == from_user_id) ? &u1 : &u2;
    User *payee = (u1.id == to_user_id) ? &u1 : &u2;

    char log_msg[200];
    sprintf(log_msg, "Transaction in progress: User %d (%s) transferring $%d to User %d (%s)", 
            from_user_id, payer->username, amount, to_user_id, payee->username);
    write_log(log_msg);

    // 5. Check Balance
    if (payer->balance < amount) {
        // Insufficient funds
        unlock_record(fd, offset2, sizeof(User));
        unlock_record(fd, offset1, sizeof(User));
        close(fd);
        sprintf(log_msg, "Transaction failed: User %d (%s) has insufficient funds.", 
                from_user_id, payer->username);
        write_log(log_msg);
        return -2;
    }

    // 6. Perform Transfer
    payer->balance -= amount;
    payee->balance += amount;

    // 7. Write Back
    lseek(fd, offset1, SEEK_SET); write(fd, &u1, sizeof(User));
    lseek(fd, offset2, SEEK_SET); write(fd, &u2, sizeof(User));

    // 8. Unlock Both
    unlock_record(fd, offset2, sizeof(User));
    unlock_record(fd, offset1, sizeof(User));
    
    close(fd);
    sprintf(log_msg, "Transaction successful: User %d (%s) transferred $%d to User %d (%s)", 
            from_user_id, payer->username, amount, to_user_id, payee->username);
    write_log(log_msg);
    return 1;
}

void get_username(int user_id, char *buffer) {
    strcpy(buffer, "Unknown"); // Default fallback
    if (user_id <= 0) return;
    
    int fd = open(USER_FILE, O_RDONLY);
    if (fd == -1) return;

    User u;
    // Jump directly to the user's record
    lseek(fd, (user_id - 1) * sizeof(User), SEEK_SET);
    if (read(fd, &u, sizeof(User)) > 0) {
        strcpy(buffer, u.username); // Copy name to buffer
    }
    close(fd);
}

int update_balance(int user_id, int amount_change) {
    int fd = open(USER_FILE, O_RDWR);
    if (fd == -1) return -1;
    
    off_t offset = (user_id - 1) * sizeof(User);
    if (lock_record(fd, F_WRLCK, offset, sizeof(User)) == -1) {
        close(fd); return -1;
    }
    
    User u;
    lseek(fd, offset, SEEK_SET);
    if (read(fd, &u, sizeof(User)) <= 0) {
        unlock_record(fd, offset, sizeof(User));
        close(fd); return -1;
    }
    
    // If deducting, check if balance is sufficient
    if (amount_change < 0 && u.balance < -amount_change) {
        unlock_record(fd, offset, sizeof(User));
        close(fd); return -2; // Insufficient Funds
    }
    
    u.balance += amount_change;
    
    lseek(fd, offset, SEEK_SET);
    write(fd, &u, sizeof(User));
    
    unlock_record(fd, offset, sizeof(User));
    close(fd);
    return 1;
}

int get_user_cooldown(int user_id) {
    int fd = open(USER_FILE, O_RDONLY);
    if (fd == -1) return 0;
    
    User u;
    lseek(fd, (user_id - 1) * sizeof(User), SEEK_SET);
    if (read(fd, &u, sizeof(User)) > 0) {
        close(fd);
        time_t now = time(NULL);
        if (u.cooldown_until > now) {
            return (int)(u.cooldown_until - now);
        }
    } else {
        close(fd);
    }
    return 0;
}

void set_user_cooldown(int user_id, int cooldown_seconds) {
    int fd = open(USER_FILE, O_RDWR);
    if (fd == -1) return;
    
    off_t offset = (user_id - 1) * sizeof(User);
    if (lock_record(fd, F_WRLCK, offset, sizeof(User)) != -1) {
        User u;
        lseek(fd, offset, SEEK_SET);
        if (read(fd, &u, sizeof(User)) > 0) {
            u.cooldown_until = time(NULL) + cooldown_seconds;
            lseek(fd, offset, SEEK_SET);
            write(fd, &u, sizeof(User));
        }
        unlock_record(fd, offset, sizeof(User));
    }
    close(fd);
}

// Lightweight DJB2 Hash Algorithm to safely scramble passwords
void hash_password(const char *str, char *output) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    // Convert the numeric hash into a string we can store
    sprintf(output, "%lu", hash); 
}

int reset_password(int user_id, const char *old_pwd, const char *new_pwd) {
    int fd = open(USER_FILE, O_RDWR);
    if (fd == -1) return -1;

    off_t offset = (user_id - 1) * sizeof(User);
    if (lock_record(fd, F_WRLCK, offset, sizeof(User)) == -1) {
        close(fd); return -1;
    }

    User u;
    lseek(fd, offset, SEEK_SET);
    if (read(fd, &u, sizeof(User)) <= 0) {
        unlock_record(fd, offset, sizeof(User)); close(fd); return -1;
    }

    // Verify old password
    char hashed_old[50];
    hash_password(old_pwd, hashed_old);
    if (strcmp(u.password, hashed_old) != 0) {
        unlock_record(fd, offset, sizeof(User)); close(fd); return -2; // Incorrect old password
    }

    // Hash and save new password
    hash_password(new_pwd, u.password);

    lseek(fd, offset, SEEK_SET);
    write(fd, &u, sizeof(User));

    unlock_record(fd, offset, sizeof(User));
    close(fd);
    return 1;
}

int process_forgot_password(const char *username, const char *sec_answer, const char *new_password) {
    int fd = open(USER_FILE, O_RDWR);
    if (fd == -1) return -1;
    if (lock_record(fd, F_WRLCK, 0, 0) == -1) { close(fd); return -1; }

    User u;
    int found = 0;
    off_t offset = 0;
    
    // Scan for the username
    while (read(fd, &u, sizeof(User)) > 0) {
        if (strcmp(u.username, username) == 0) {
            found = 1;
            break;
        }
        offset += sizeof(User);
    }

    if (!found) { unlock_record(fd, 0, 0); close(fd); return -1; } // User not found

    // Hash the provided answer to compare it
    char hashed_ans[50];
    hash_password(sec_answer, hashed_ans);

    if (strcmp(u.security_answer, hashed_ans) != 0) {
        unlock_record(fd, 0, 0); close(fd); return -2; // Wrong answer
    }

    // Answer is correct! Hash and save the new password
    hash_password(new_password, u.password);
    lseek(fd, offset, SEEK_SET);
    write(fd, &u, sizeof(User));

    unlock_record(fd, 0, 0); close(fd);
    return 1; // Success
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include "common.h"
#include <termios.h>

void clear_input() { while (getchar() != '\n'); }

void get_password(char *password, int max_len) {
    struct termios oldt, newt;
    int ch, i = 0;
    
    // Disable terminal echoing
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    while (i < max_len - 1) {
        ch = getchar();
        if (ch == '\n' || ch == EOF) break;
        if (ch == 127 || ch == 8) { // Handle Backspace
            if (i > 0) {
                i--;
                printf("\b \b");
            }
        } else {
            password[i++] = ch;
            printf("*"); // Print asterisk instead of character
        }
    }
    password[i] = '\0';
    printf("\n");
    
    // Restore terminal echoing
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

int recv_all(int sock, void *buffer, size_t length) {
    size_t total_received = 0;
    char *ptr = (char *)buffer;
    while (total_received < length) {
        ssize_t received = recv(sock, ptr + total_received, length - total_received, 0);
        if (received <= 0) return received;
        total_received += received;
    }
    return total_received;
}

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
        return -1;

    serv_addr.sin_family = AF_INET; 
    serv_addr.sin_port = htons(PORT);

    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0) 
        return -1;

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) 
        return -1;

    int choice;
    Request req;
    Response res;

    while(1) {
        printf("\n--- WELCOME TO THE AUCTION SYSTEM ---\n");
        printf("1. Register\n");
        printf("2. Login\n");
        printf("3. Forgot Password\n");
        printf("4. Exit\n");
        printf("Enter choice: ");
        scanf("%d", &choice);
        clear_input();

        memset(&req, 0, sizeof(Request));

        if (choice == 1) {
            req.operation = OP_REGISTER;
            int initial_balance = 0;
            char sec_ans[50];
            
            printf("Enter Username: "); 
            scanf("%s", req.username); 
            clear_input();
            printf("Enter Password: "); 
            get_password(req.password, 50);
            printf("Enter Initial Balance: "); 
            scanf("%d", &initial_balance);
            clear_input();
            printf("\nSecurity Question: What is your favorite food?\n");
            printf("Enter Answer: "); scanf(" %[^\n]", sec_ans); clear_input();
            
            // Send the balance in the payload
            sprintf(req.payload, "%d|%s", initial_balance, sec_ans);            
            send(sock, &req, sizeof(Request), 0);
            recv_all(sock, &res, sizeof(Response));
            printf("Server: %s\n", res.message);
        }
        else if (choice == 2) {
            req.operation = OP_LOGIN;
            printf("Enter Username: "); scanf("%s", req.username); clear_input(); // Added clear_input
            printf("Enter Password: "); get_password(req.password, 50); // MASKED
            if (send(sock, &req, sizeof(Request), 0) < 0) {
                printf("Error: Send failed.\n");
                break;
            }
            
            int bytes_received = recv_all(sock, &res, sizeof(Response));
            if (bytes_received <= 0) {
                printf("Error: Connection lost with server.\n");
                close(sock);
                return -1; // Exit or handle reconnection
            }
            printf("Server: %s\n", res.message);
            
            if (res.operation == OP_SUCCESS) {
                int my_client_id = -1;
                char display_msg[200];
                sscanf(res.message, "%d|%[^\n]", &my_client_id, display_msg);

                //printf("Server: %s\n", display_msg);
                // --- ENTERING AUCTION MENU LOOP ---
                int logged_in = 1;
                while(logged_in) {
                    // 1. Check if user is a seller
                    memset(&req, 0, sizeof(Request));
                    req.operation = OP_CHECK_SELLER;
                    send(sock, &req, sizeof(Request), 0);
                    recv_all(sock, &res, sizeof(Response));
                    int is_seller = atoi(res.message);

                    // 1.5. Check if user has active bids they can withdraw
                    memset(&req, 0, sizeof(Request));
                    req.operation = OP_CHECK_ACTIVE_BIDS;
                    send(sock, &req, sizeof(Request), 0);
                    recv_all(sock, &res, sizeof(Response));
                    int has_bids = atoi(res.message);

                    // 2. Define perfectly sequential dynamic menu numbers
                    int current_opt = 4; // Start numbering after the 3 static options
                    int opt_withdraw = has_bids  ? current_opt++ : -1;
                    int opt_close    = is_seller ? current_opt++ : -1;
                    int opt_bal      = current_opt++;
                    int opt_mybids   = current_opt++;
                    int opt_hist     = current_opt++;
                    int opt_reset    = current_opt++;
                    int opt_logout   = current_opt++;

                    // 3. Print Dynamic Menu
                    printf("\n--- AUCTION MENU ---\n");
                    printf("1. List New Item (Sell)\n");
                    printf("2. View All Items (Buy)\n");
                    printf("3. Place Bid\n");
                    if (has_bids)  printf("%d. Withdraw Bid\n", opt_withdraw);
                    if (is_seller) printf("%d. Close Auction (Seller)\n", opt_close);
                    printf("%d. Check Balance\n", opt_bal);
                    printf("%d. My Active Bids\n", opt_mybids);
                    printf("%d. Transaction History\n", opt_hist);
                    printf("%d. Reset Password\n", opt_reset);
                    printf("%d. Logout\n", opt_logout);
                    printf("Enter choice: ");
                    
                    int menu_choice;
                    scanf("%d", &menu_choice);
                    clear_input();
                    
                    memset(&req, 0, sizeof(Request)); // Reset req for next operations

                    if (menu_choice == 1) {
                        // ... (existing logic for OP_CREATE_ITEM) ...
                        req.operation = OP_CREATE_ITEM;
                        char name[50], desc[100];
                        int price, duration;
                        printf("Item Name: "); scanf(" %[^\n]", name); clear_input();
                        printf("Description: "); scanf(" %[^\n]", desc); clear_input();
                        printf("Base Price: "); scanf("%d", &price); clear_input();
                        printf("Duration (in minutes): "); scanf("%d", &duration); clear_input();
                        sprintf(req.payload, "%s|%s|%d|%d", name, desc, price, duration);
                        send(sock, &req, sizeof(Request), 0);
                        recv_all(sock, &res, sizeof(Response));
                        printf("Server: %s\n", res.message);
                    }
                    else if (menu_choice == 2) {
                        // ... (existing logic for OP_LIST_ITEMS / DisplayItem loop) ...
                        req.operation = OP_LIST_ITEMS;
                        send(sock, &req, sizeof(Request), 0);
                        recv_all(sock, &res, sizeof(Response));
                        int count = atoi(res.message);
                        
                        printf("\nFound %d Auctions:\n", count);
                        printf("%-5s %-20s %-10s %-15s %-15s\n", "ID", "Name", "Price", "High Bidder", "Time Left");
                        printf("----------------------------------------------------------------------\n");
                        
                        DisplayItem item;
                        time_t now = time(NULL);

                        for(int i=0; i<count; i++) {
                            recv_all(sock, &item, sizeof(DisplayItem));
                            char time_str[20];
                            int seconds_left = (int)difftime(item.end_time, now);

                            if (item.status == ITEM_SOLD || seconds_left <= 0) {
                                strcpy(time_str, "Ended");
                            } else {
                                int min = seconds_left / 60;
                                int sec = seconds_left % 60;
                                sprintf(time_str, "%dm %ds", min, sec);
                            }
                            printf("%-5d %-20s $%-9d %-15s %-15s\n", 
                                   item.id, item.name, item.current_bid, item.winner_name, time_str);
                        }
                    }
                    else if (menu_choice == 3) {
                        // ... (existing logic for OP_BID) ...
                        req.operation = OP_BID;
                        int item_id, amount;
                        printf("Enter Item ID to bid on: "); scanf("%d", &item_id);
                        printf("Enter your Bid Amount: "); scanf("%d", &amount);
                        clear_input();
                        sprintf(req.payload, "%d|%d", item_id, amount);
                        send(sock, &req, sizeof(Request), 0);
                        recv_all(sock, &res, sizeof(Response));
                        printf("Server: %s\n", res.message);
                    }
                    else if (has_bids && menu_choice == opt_withdraw) {
                        // ... (existing logic for OP_WITHDRAW_BID) ...
                        req.operation = OP_WITHDRAW_BID;
                        printf("Enter Item ID to Withdraw Bid from: ");
                        int wid;
                        scanf("%d", &wid);
                        clear_input();
                        sprintf(req.payload, "%d", wid);
                        send(sock, &req, sizeof(Request), 0);
                        recv_all(sock, &res, sizeof(Response));
                        printf("Server: %s\n", res.message);
                    }
                    else if (is_seller && menu_choice == opt_close) {
                        // ... (existing logic for OP_CLOSE_AUCTION) ...
                        req.operation = OP_CLOSE_AUCTION;
                        printf("Enter Item ID to Close: ");
                        int cid;
                        scanf("%d", &cid);
                        sprintf(req.payload, "%d", cid);
                        send(sock, &req, sizeof(Request), 0);
                        recv_all(sock, &res, sizeof(Response));
                        printf("Server: %s\n", res.message);
                    }
                    else if (menu_choice == opt_bal) {
                        // ... (existing logic for OP_VIEW_BALANCE) ...
                        req.operation = OP_VIEW_BALANCE;
                        send(sock, &req, sizeof(Request), 0);
                        recv_all(sock, &res, sizeof(Response));
                        printf("Server: %s\n", res.message);
                    }
                    else if (menu_choice == opt_mybids) {
                        req.operation = OP_MY_BIDS;
                        send(sock, &req, sizeof(Request), 0);
                        
                        recv_all(sock, &res, sizeof(Response));
                        int count = atoi(res.message);
                        
                        if (count == 0) {
                            printf("\nYou have no active bids.\n");
                        } else {
                            // Receive all items
                            DisplayItem my_bids[50];
                            for(int i=0; i<count; i++) {
                                recv_all(sock, &my_bids[i], sizeof(DisplayItem));
                            }

                            // TABLE 1: Winning
                            printf("\n[ ITEMS YOU ARE WINNING ]\n");
                            printf("%-5s %-20s %-15s\n", "ID", "Name", "Your Bid");
                            printf("------------------------------------------\n");
                            int win_count = 0;
                            for(int i=0; i<count; i++) {
                                if(my_bids[i].winner_id == my_client_id) {
                                    printf("%-5d %-20s $%-14d\n", 
                                           my_bids[i].id, my_bids[i].name, my_bids[i].current_bid);
                                    win_count++;
                                }
                            }
                            if(win_count == 0) printf("None.\n");

                            // TABLE 2: Outbid (Applied but not winning)
                            printf("\n[ APPLIED BUT NOT WINNING (OUTBID) ]\n");
                            // Added "My Bid" column
                            printf("%-5s %-20s %-10s %-10s %-15s\n", "ID", "Name", "Curr Bid", "My Bid", "Winning User");
                            printf("----------------------------------------------------------------\n");
                            int outbid_count = 0;
                            for(int i=0; i<count; i++) {
                                if(my_bids[i].winner_id != my_client_id) {
                                    // Print the my_bid_amount variable
                                    printf("%-5d %-20s $%-9d $%-9d %-15s\n", 
                                           my_bids[i].id, my_bids[i].name, my_bids[i].current_bid, my_bids[i].my_bid_amount, my_bids[i].winner_name);
                                    outbid_count++;
                                }
                            }
                            if(outbid_count == 0) printf("None.\n");
                        }
                    }
                    else if (menu_choice == opt_hist) {
                        // ... (existing logic for OP_TRANSACTION_HISTORY) ...
                        req.operation = OP_TRANSACTION_HISTORY;
                        send(sock, &req, sizeof(Request), 0);
                        recv_all(sock, &res, sizeof(Response));
                        int count = atoi(res.message);
                        
                        printf("\n--- TRANSACTION HISTORY ---\n");
                        if (count == 0) {
                            printf("No past transactions found.\n");
                        } else {
                            HistoryRecord hist[50];
                            for(int i=0; i<count; i++) {
                                recv_all(sock, &hist[i], sizeof(HistoryRecord));
                            }
                            
                            printf("\n[ ITEMS YOU SOLD ]\n");
                            printf("%-5s %-20s %-15s %-15s\n", "ID", "Name", "Final Price", "Winner");
                            printf("-----------------------------------------------------------\n");
                            int sold_count = 0;
                            for(int i=0; i<count; i++) {
                                if (hist[i].seller_id == my_client_id) {
                                    printf("%-5d %-20s $%-14d %-15s\n", 
                                           hist[i].item_id, hist[i].item_name, hist[i].amount, hist[i].winner_name);
                                    sold_count++;
                                }
                            }
                            if (sold_count == 0) printf("You haven't sold any items yet.\n");

                            printf("\n[ ITEMS YOU WON ]\n");
                            printf("%-5s %-20s %-15s %-15s\n", "ID", "Name", "Winning Bid", "Seller");
                            printf("-----------------------------------------------------------\n");
                            int won_count = 0;
                            for(int i=0; i<count; i++) {
                                if (hist[i].winner_id == my_client_id) {
                                    printf("%-5d %-20s $%-14d %-15s\n", 
                                           hist[i].item_id, hist[i].item_name, hist[i].amount, hist[i].seller_name);
                                    won_count++;
                                }
                            }
                            if (won_count == 0) printf("You haven't won any items yet.\n");
                        }
                    }
                    else if (menu_choice == opt_reset) {
                        req.operation = OP_RESET_PASSWORD;
                        char old_p[50], new_p[50];
                        
                        printf("Enter Current Password: "); get_password(old_p, 50);
                        printf("Enter New Password: "); get_password(new_p, 50);
                        
                        sprintf(req.payload, "%s|%s", old_p, new_p);
                        send(sock, &req, sizeof(Request), 0);
                        recv_all(sock, &res, sizeof(Response));
                        printf("Server: %s\n", res.message);
                    }
                    else if (menu_choice == opt_logout) {
                        req.operation = OP_EXIT;
                        send(sock, &req, sizeof(Request), 0);
                        logged_in = 0;
                        printf("Logged out.\n");
                    }
                    else {
                        printf("Invalid choice. Please try again.\n");
                    }
                }
            }
        }
        else if (choice == 3) {
            req.operation = OP_FORGOT_PASSWORD;
            char f_user[50], f_pass[50], f_ans[50];
            
            printf("Enter Username: "); scanf("%s", f_user); clear_input();
            printf("Enter New Password: "); get_password(f_pass, 50);
            printf("\nSecurity Question: What is your favorite food?\n");
            printf("Enter Answer: "); scanf(" %[^\n]", f_ans); clear_input();
            
            // Package payload
            sprintf(req.payload, "%s|%s|%s", f_user, f_pass, f_ans);
            send(sock, &req, sizeof(Request), 0);
            recv_all(sock, &res, sizeof(Response));
            printf("Server: %s\n", res.message);
        }
        else if (choice == 4) {
            req.operation = OP_EXIT;
            send(sock, &req, sizeof(Request), 0);
            printf("Exiting...\n");
            break;
        }else {
            printf("Invalid choice.\n");
        }
    }
    close(sock);
    return 0;
}
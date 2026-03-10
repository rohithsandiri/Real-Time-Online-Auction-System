#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "common.h"
#include "file_handler.h"
#include "user_handler.h"
#include "item_handler.h"
#include "session.h"
#include "logger.h"

// MONITOR THREAD
void *auction_monitor_thread(void *arg) {
    while(1) {
        check_expired_items();
        sleep(1); // Check every 1 second
    }
    return NULL;
}

int recv_all(int sock, void *buffer, size_t length) {
    size_t total_received = 0;
    char *ptr = (char *)buffer;
    while (total_received < length) {
        ssize_t received = recv(sock, ptr + total_received, length - total_received, 0);
        if (received <= 0) return received; // Error or closed
        total_received += received;
    }
    return total_received;
}

void *client_handler(void *socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);
    
    Request req;
    Response res;
    int my_user_id = -1;

    while(recv_all(sock, &req, sizeof(Request)) > 0) {
        memset(&res, 0, sizeof(Response));
        
        switch(req.operation) {
            case OP_REGISTER:
                int init_bal;
                char sec_ans[50];
                
                // Unpack the balance and the security answer
                sscanf(req.payload, "%d|%[^\n]", &init_bal, sec_ans);
                
                // Call the updated function and store in reg_status
                int reg_status = register_user(req.username, req.password, 1, init_bal, sec_ans);
                
                if (reg_status > 0) {
                    res.operation = OP_SUCCESS;
                    strcpy(res.message, "Registration successful! You can now login.");
                } else if (reg_status == -2) {
                    res.operation = OP_ERROR;
                    strcpy(res.message, "Error: Username already exists.");
                } else {
                    res.operation = OP_ERROR;
                    strcpy(res.message, "Error: Registration failed.");
                }
                break;

            case OP_LOGIN:
                printf("Login request: %s\n", req.username);
                int user_id = authenticate_user(req.username, req.password);
                if (user_id > 0) {
                    int session_status = create_session(user_id);
                    if (session_status >= 0) {
                        my_user_id = user_id;
                        res.operation = OP_SUCCESS;
                        res.session_id = session_status;
                        sprintf(res.message, "%d|Welcome User %s", user_id, req.username);
                        char log_msg[150];
                        sprintf(log_msg, "User %d %s successfully logged in.", my_user_id, req.username);
                        write_log(log_msg);
                    } else {
                        res.operation = OP_ERROR;
                        strcpy(res.message, "User already logged in.");
                    }
                } else {
                    res.operation = OP_ERROR;
                    strcpy(res.message, "Invalid Credentials.");
                }
                break;


            case OP_CREATE_ITEM:
                printf("User %d listing item: %s\n", my_user_id, req.payload); 
                
                char i_name[50], i_desc[100];
                int i_price, i_duration;
                // Parse duration instead of date string
                sscanf(req.payload, "%[^|]|%[^|]|%d|%d", i_name, i_desc, &i_price, &i_duration);
                
                int item_id = create_item(i_name, i_desc, i_price, i_duration, my_user_id);
                
                if (item_id > 0) {
                    res.operation = OP_SUCCESS;
                    sprintf(res.message, "Item Listed Successfully! ID: %d", item_id);
                } else {
                    res.operation = OP_ERROR;
                    strcpy(res.message, "Failed to list item.");
                }
                break;

            case OP_LIST_ITEMS:
                // We need to send a list. The Response struct only has a small message buffer.
                // We will send a header first, then the items one by one.
                Item items[50];
                int count = get_all_items(items, 50);
                
                res.operation = OP_SUCCESS;
                sprintf(res.message, "%d", count); // Send count first
                send(sock, &res, sizeof(Response), 0);
                
                // Send actual items
                for (int i = 0; i < count; i++) {
                    DisplayItem d_item;
                    memset(&d_item, 0, sizeof(DisplayItem));
                    
                    d_item.id = items[i].id;
                    strcpy(d_item.name, items[i].name);
                    d_item.current_bid = items[i].current_bid;
                    d_item.end_time = items[i].end_time;
                    d_item.status = items[i].status;

                    // Resolve the Highest Bidder's Name
                    if (items[i].current_winner_id == -1) {
                        strcpy(d_item.winner_name, "None");
                    } else {
                        get_username(items[i].current_winner_id, d_item.winner_name);
                    }

                    send(sock, &d_item, sizeof(DisplayItem), 0);
                }
                continue; // Skip the default send at bottom since we already sent response

            case OP_EXIT:
                printf("User %d logged out.\n", my_user_id);
                if (my_user_id != -1) {
                    char log_msg[150];
                    sprintf(log_msg, "User %d %s successfully logged out.", my_user_id, req.username);
                    write_log(log_msg);
                    remove_session(my_user_id);
                    my_user_id = -1; // Reset local ID
                }
                continue;

            case OP_BID:
                int b_item_id, b_amount;
                // Client sends "ItemID|Amount" in payload
                sscanf(req.payload, "%d|%d", &b_item_id, &b_amount);
                
                printf("User %d trying to bid %d on Item %d\n", my_user_id, b_amount, b_item_id);
                
                int result = place_bid(b_item_id, my_user_id, b_amount);
                
                if (result == 1) {
                    res.operation = OP_SUCCESS;
                    sprintf(res.message, "Bid Accepted! You are the highest bidder.");
                } else if (result == -3) {
                    res.operation = OP_ERROR;
                    sprintf(res.message, "Bid Failed: Amount too low (Current bid is higher).");
                } else if (result == -4) {
                    res.operation = OP_ERROR;
                    sprintf(res.message, "Bid Failed: Auction is closed.");
                } else if (result == -5) {
                    res.operation = OP_ERROR;
                    sprintf(res.message, "Bid Failed: You cannot bid on your own listed item.");
                } else if (result == -6) {
                    res.operation = OP_ERROR;
                    sprintf(res.message, "Bid Failed: Insufficient balance to place this bid.");
                } else if (result == -7) {
                    // --- NEW COOLDOWN ERROR ---
                    int cd_left = get_user_cooldown(my_user_id);
                    res.operation = OP_ERROR;
                    sprintf(res.message, "Bid Failed: You are on cooldown for %d more seconds.", cd_left);
                } else {
                    res.operation = OP_ERROR;
                    sprintf(res.message, "Bid Failed: System Error or Invalid ID.");
                }
                break;

            case OP_CLOSE_AUCTION:
                int c_item_id;
                sscanf(req.payload, "%d", &c_item_id);
                
                int close_result = close_auction(c_item_id, my_user_id);
                
                if (close_result == 1) {
                    res.operation = OP_SUCCESS;
                    strcpy(res.message, "Auction Closed! Funds Transferred.");
                } else if (close_result == 0) {
                     res.operation = OP_SUCCESS;
                     strcpy(res.message, "Auction Closed (No Bids).");
                } else if (close_result == -2) {
                     res.operation = OP_ERROR;
                     strcpy(res.message, "Error: You are not the seller of this item.");
                } else if (close_result == -3) {
                     res.operation = OP_ERROR;
                     strcpy(res.message, "Error: This auction is already closed or expired.");
                } else if (close_result == -4) {
                     res.operation = OP_ERROR;
                     strcpy(res.message, "Error: Please enter a valid Item ID.");
                } else {
                     res.operation = OP_ERROR;
                     strcpy(res.message, "Error closing auction.");
                }
                break;

            case OP_VIEW_BALANCE:
                int bal = get_user_balance(my_user_id);
                
                if (bal >= 0) {
                    res.operation = OP_SUCCESS;
                    sprintf(res.message, "Current Balance: $%d", bal);
                } else {
                    res.operation = OP_ERROR;
                    strcpy(res.message, "Error retrieving balance.");
                }
                break;

            case OP_MY_BIDS:
                Item my_items[50];
                int my_count = get_my_bids(my_user_id, my_items, 50);
                
                res.operation = OP_SUCCESS;
                sprintf(res.message, "%d", my_count);
                send(sock, &res, sizeof(Response), 0);

                for (int i = 0; i < my_count; i++) {
                    DisplayItem d_item;
                    memset(&d_item, 0, sizeof(DisplayItem));
                    
                    d_item.id = my_items[i].id;
                    strcpy(d_item.name, my_items[i].name);
                    d_item.current_bid = my_items[i].current_bid;
                    d_item.end_time = my_items[i].end_time;
                    d_item.status = my_items[i].status;
                    d_item.winner_id = my_items[i].current_winner_id; 

                    d_item.my_bid_amount = 0;
                    for(int j = 0; j < my_items[i].past_bidders_count; j++) {
                        if(my_items[i].past_bidders[j] == my_user_id) {
                            d_item.my_bid_amount = my_items[i].past_bid_amounts[j];
                            break;
                        }
                    }

                    if (my_items[i].current_winner_id == -1) {
                        strcpy(d_item.winner_name, "None");
                    } else {
                        get_username(my_items[i].current_winner_id, d_item.winner_name);
                    }

                    send(sock, &d_item, sizeof(DisplayItem), 0);
                }
                continue;
            
            case OP_TRANSACTION_HISTORY:
                Item hist_items[50];
                int hist_count = get_transaction_history(my_user_id, hist_items, 50);
                
                res.operation = OP_SUCCESS;
                sprintf(res.message, "%d", hist_count);
                send(sock, &res, sizeof(Response), 0);
                
                // Package and send HistoryRecords instead of raw Items
                for(int i = 0; i < hist_count; i++) {
                    HistoryRecord hr;
                    memset(&hr, 0, sizeof(HistoryRecord));
                    
                    hr.item_id = hist_items[i].id;
                    strcpy(hr.item_name, hist_items[i].name);
                    hr.amount = hist_items[i].current_bid;
                    hr.seller_id = hist_items[i].seller_id;
                    hr.winner_id = hist_items[i].current_winner_id;
                    
                    // Resolve Seller Name
                    get_username(hist_items[i].seller_id, hr.seller_name);
                    
                    // Resolve Winner Name
                    if (hist_items[i].current_winner_id == -1) {
                        strcpy(hr.winner_name, "None");
                    } else {
                        get_username(hist_items[i].current_winner_id, hr.winner_name);
                    }
                    
                    send(sock, &hr, sizeof(HistoryRecord), 0);
                }
                continue; // Skip the default send at the bottom
            
            case OP_CHECK_SELLER:
                int seller_status = is_user_seller(my_user_id);
                res.operation = OP_SUCCESS;
                sprintf(res.message, "%d", seller_status);
                break;

            case OP_WITHDRAW_BID:
                int w_item_id;
                sscanf(req.payload, "%d", &w_item_id);
                
                // 1. Check if they are already on cooldown
                int current_cd = get_user_cooldown(my_user_id);
                if (current_cd > 0) {
                    res.operation = OP_ERROR;
                    sprintf(res.message, "Withdraw Failed: You are on cooldown for %d more seconds.", current_cd);
                    break;
                }

                // 2. Attempt to withdraw
                int w_res = withdraw_bid(w_item_id, my_user_id);
                
                if (w_res == 1) {
                    // Apply a 120-second (2 minute) cooldown penalty
                    set_user_cooldown(my_user_id, 120); 
                    res.operation = OP_SUCCESS;
                    strcpy(res.message, "Bid Withdrawn. Escrow refunded. 2-Minute Cooldown Applied.");
                } else if (w_res == -2) {
                    res.operation = OP_ERROR; strcpy(res.message, "Error: Auction is no longer active.");
                } else if (w_res == -3) {
                    res.operation = OP_ERROR; strcpy(res.message, "Error: You are not the highest bidder.");
                } else {
                    res.operation = OP_ERROR; strcpy(res.message, "Error: Invalid Item ID.");
                }
                break;
            
            case OP_CHECK_ACTIVE_BIDS:
                int active_bids_status = has_active_bids(my_user_id);
                res.operation = OP_SUCCESS;
                sprintf(res.message, "%d", active_bids_status);
                break;

            case OP_RESET_PASSWORD:
                char old_pass[50], new_pass[50];
                sscanf(req.payload, "%[^|]|%s", old_pass, new_pass);
                
                int reset_res = reset_password(my_user_id, old_pass, new_pass);
                if (reset_res == 1) {
                    res.operation = OP_SUCCESS;
                    strcpy(res.message, "Password successfully updated.");
                } else if (reset_res == -2) {
                    res.operation = OP_ERROR;
                    strcpy(res.message, "Error: Incorrect current password.");
                } else {
                    res.operation = OP_ERROR;
                    strcpy(res.message, "Error updating password.");
                }
                break;

            case OP_FORGOT_PASSWORD:
                char f_username[50], f_new_pass[50], f_sec_ans[50];
                
                // Extract data (putting answer last handles any spaces typed by the user)
                sscanf(req.payload, "%[^|]|%[^|]|%[^\n]", f_username, f_new_pass, f_sec_ans);
                
                int f_res = process_forgot_password(f_username, f_sec_ans, f_new_pass);
                if (f_res == 1) {
                    res.operation = OP_SUCCESS;
                    strcpy(res.message, "Password successfully reset! You can now login.");
                } else if (f_res == -2) {
                    res.operation = OP_ERROR;
                    strcpy(res.message, "Error: Incorrect security answer.");
                } else {
                    res.operation = OP_ERROR;
                    strcpy(res.message, "Error: Username not found.");
                }
                break;
        }
        send(sock, &res, sizeof(Response), 0);
    }
    
    if (my_user_id != -1) remove_session(my_user_id);
    close(sock);
    return NULL;
}

int main() {
    init_sessions(); // Initialize the session array
    
    int server_fd, new_socket, *new_sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) { 
            perror("Socket failed"); 
            exit(EXIT_FAILURE); 
        }

    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) { 
            perror("Bind failed");
            exit(EXIT_FAILURE); 
        }

    if (listen(server_fd, 3) < 0) { 
            perror("Listen failed"); 
            exit(EXIT_FAILURE); 
        }
    
    // START MONITOR THREAD
    pthread_t monitor_tid;
    pthread_create(&monitor_tid, NULL, auction_monitor_thread, NULL);
    pthread_detach(monitor_tid); // Run in background
    
    printf("Auction Server running on port %d\n", PORT);
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) continue;
        char log_msg[100];
        sprintf(log_msg, "New connection accepted from %s:%d", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
        write_log(log_msg);
        pthread_t thread_id;
        new_sock = malloc(sizeof(int)); *new_sock = new_socket;
        pthread_create(&thread_id, NULL, client_handler, (void*)new_sock);
    }
    return 0;
}
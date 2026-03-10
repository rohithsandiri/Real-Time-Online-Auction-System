#ifndef COMMON_H
#define COMMON_H

#define PORT 8085
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10
#define MAX_BIDDERS 20

// Operation Codes (Client -> Server)
#define OP_LOGIN 1
#define OP_REGISTER 2
#define OP_EXIT 3
#define OP_CREATE_ITEM 4
#define OP_LIST_ITEMS 5
#define OP_BID 6
#define OP_CLOSE_AUCTION 7
#define OP_VIEW_BALANCE 8
#define OP_MY_BIDS 9
#define OP_TRANSACTION_HISTORY 10
#define OP_CHECK_SELLER 11
#define OP_WITHDRAW_BID 12
#define OP_CHECK_ACTIVE_BIDS 13
#define OP_RESET_PASSWORD 14
#define OP_FORGOT_PASSWORD 15
#define OP_SUCCESS 100
#define OP_ERROR 101

// Item Status Constants
#define ITEM_ACTIVE 1
#define ITEM_SOLD 2

// User Roles
#define ROLE_ADMIN 1
#define ROLE_USER 2

// Data Structures
typedef struct {
    int id;
    char username[50];
    char password[50];
    int role;         // ROLE_ADMIN or ROLE_USER
    int balance;
    time_t cooldown_until;
    char security_answer[50];
} User;

typedef struct {
    int id;
    char name[50];
    char description[100];
    int seller_id;          // Who is selling it
    int current_winner_id;  // Who has the highest bid (-1 if none)
    int base_price;
    int current_bid;        // Current highest price
    time_t end_time;        // Auction end time
    int status;             // ITEM_ACTIVE or ITEM_SOLD
    int past_bidders[MAX_BIDDERS];
    int past_bid_amounts[MAX_BIDDERS];
    int past_bidders_count;
} Item;

// Protocol Message
typedef struct {
    int operation;    // OP_LOGIN, etc.
    int session_id;   // -1 if not logged in
    char username[50];
    char password[50]; // text plain
    char payload[BUFFER_SIZE]; // Generic message/data
} Request;

typedef struct {
    int operation;
    char message[BUFFER_SIZE];
    int session_id; // Assigned by server on login
} Response;

typedef struct {
    int item_id;
    char item_name[50];
    int amount;
    char seller_name[50];
    char winner_name[50];
    int seller_id;
    int winner_id;
} HistoryRecord;

typedef struct {
    int id;
    char name[50];
    int current_bid;
    char winner_name[50];
    time_t end_time;
    int status;
    int winner_id;
    int my_bid_amount;
} DisplayItem;

void get_username(int user_id, char *buffer);
void hash_password(const char *str, char *output);

#endif
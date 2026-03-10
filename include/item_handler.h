#ifndef ITEM_HANDLER_H
#define ITEM_HANDLER_H

#include "common.h"

int create_item(char *name, char *desc, int base_price, int duration_minutes, int seller_id);
int get_all_items(Item *buffer, int max_items);
int place_bid(int item_id, int user_id, int bid_amount);
int close_auction(int item_id, int seller_id);
int get_my_bids(int user_id, Item *buffer, int max_items);
int get_transaction_history(int user_id, Item *buffer, int max_items);
int is_user_seller(int user_id);
void check_expired_items();
int withdraw_bid(int item_id, int user_id);
int has_active_bids(int user_id);

#endif

#ifndef USER_HANDLER_H
#define USER_HANDLER_H

int register_user(const char *username, const char *password, int role, int initial_balance, const char *sec_answer);
int authenticate_user(const char *username, const char *password);
int get_user_balance(int user_id);
int transfer_funds(int from_user_id, int to_user_id, int amount);
int update_balance(int user_id, int amount_change);
int get_user_cooldown(int user_id);
void set_user_cooldown(int user_id, int cooldown_seconds);
int reset_password(int user_id, const char *old_pwd, const char *new_pwd);
int process_forgot_password(const char *username, const char *sec_answer, const char *new_password);

#endif

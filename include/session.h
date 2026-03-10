#ifndef SESSION_H
#define SESSION_H

void init_sessions();
int create_session(int user_id);
int check_session(int user_id);
void remove_session(int user_id);

#endif
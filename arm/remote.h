// remote.h - the shell over the network (telnet on 192.168.7.1 port 23)
#ifndef REMOTE_H
#define REMOTE_H

void remote_init(void);
void remote_putc(char c);
void remote_flush(void);
int remote_getc(void);                  // a key from the network, or -1
int remote_take_connect_event(void);    // 1 once after a client connects
int remote_client(char *out);           // client's address, 0 if nobody

#endif

// ssh.h - SSH server: the shell over the network (192.168.7.1 port 22)
#ifndef SSH_H
#define SSH_H

void ssh_init(void);                    // after MyFS is mounted (host key lives there)
void ssh_poll(void);                    // handle incoming packets; call often
void ssh_putc(char c);
void ssh_flush(void);
int ssh_getc(void);                     // a key from the SSH client, or -1
int ssh_take_connect_event(void);       // 1 once after a shell starts
int ssh_client(char *out);              // "user@address" of the logged-in client, 0 if none
void ssh_logout(void);                  // end the SSH session
int ssh_active(void);                   // 1 while an SSH shell is open

int ssh_password_set(void);
int ssh_set_password(const char *pw);   // 0 = saved
int ssh_fingerprint(char *out);         // host key "SHA256:..." (needs 51 bytes)
int ssh_host_key_saved(void);           // 0 = key only in RAM (no MyFS)

#endif

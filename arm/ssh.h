// ssh.h - SSH server: the shell over the network (192.168.7.1 port 22).
// Up to ssh_sessions() connections at once; each is numbered 0..n-1 and
// gets its own terminal (console.c).
#ifndef SSH_H
#define SSH_H

void ssh_init(void);                    // after MyFS is mounted (host key lives there)
void ssh_poll(void);                    // handle incoming packets of all sessions; call often
int ssh_sessions(void);                 // how many sessions there can be
int ssh_shell_open(int i);              // session i is logged in and has a shell
void ssh_putc(int i, char c);
void ssh_flush(int i);
int ssh_getc(int i);                    // a key from session i, or -1
int ssh_client(int i, char *out);       // "user@address" of session i, 0 if none
void ssh_logout(int i);                 // end session i

int ssh_password_set(void);
int ssh_set_password(const char *pw);   // 0 = saved
int ssh_fingerprint(char *out);         // host key "SHA256:..." (needs 51 bytes)
int ssh_host_key_saved(void);           // 0 = key only in RAM (no MyFS)

#endif

#ifndef BOREDOS_LIBC_SIGNAL_H
#define BOREDOS_LIBC_SIGNAL_H

#include "sys/types.h"

typedef void (*sighandler_t)(int);
typedef unsigned long sigset_t;

struct sigaction {
	sighandler_t sa_handler;
	sigset_t sa_mask;
	int sa_flags;
};

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIGKILL 9
#define SIGTERM 15
#define SIGINT 2

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000

sighandler_t signal(int sig, sighandler_t handler);
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
int raise(int sig);
int kill(pid_t pid, int sig);

#endif

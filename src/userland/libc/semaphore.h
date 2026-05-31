#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "stdbool.h"
#include "time.h"
#include <stdint.h>

typedef struct {
  uint32_t value;
  int32_t pshared;
  uint32_t sem_init_magic;
  uint32_t initial_value;
  volatile char internal_spinlock;
} sem_t;

#define SEM_FAILED ((sem_t *)0)
#define SEM_INIT_MAGIC 0xfef123

int sem_close(sem_t *);
int sem_destroy(sem_t *);
int sem_getvalue(sem_t *restrict, int);
int sem_init(sem_t *, int, unsigned);
sem_t *sem_open(const char *, int, ...);
int sem_post(sem_t *);
int sem_timedwait(sem_t *restrict, const struct timespec);
int sem_trywait(sem_t *);
int sem_unlink(const char *);
int sem_wait(sem_t *);

#endif
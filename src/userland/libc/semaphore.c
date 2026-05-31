#include "semaphore.h"
#include "errno.h"
#include "stdlib.h"
#include <errno.h>
#include <stdbool.h>

static inline void cpu_yield(void) {
  __asm__ __volatile__("pause" ::: "memory");
}

void internal_lock(sem_t *sem) {
  // do a atomic set
  while (__atomic_test_and_set(&sem->internal_spinlock, __ATOMIC_ACQUIRE)) {
    cpu_yield();
  }
  return;
}

void internal_unlock(sem_t *sem) {
  __atomic_clear(&sem->internal_spinlock, __ATOMIC_RELEASE);
}

int sem_init(sem_t *sem, int pshared, unsigned int val) {
  if (!sem)
    return -1;

  // sem_t already init
  if (sem->sem_init_magic == SEM_INIT_MAGIC) {
    return -1;
  }

  sem->value = val;
  sem->initial_value = val;
  sem->pshared = pshared;
  sem->sem_init_magic = SEM_INIT_MAGIC;
  __atomic_clear(&sem->internal_spinlock, __ATOMIC_RELEASE);

  // I'll not implement this...
  if (pshared == 1)
    return -1;

  return 0;
}

int sem_destroy(sem_t *sem) {
  if (!sem)
    return -1;

  // just set the init magic to zero
  if (sem->sem_init_magic != SEM_INIT_MAGIC)
    return -1;

  internal_lock(sem);
  sem->sem_init_magic = 0;
  sem->value = 0;
  internal_unlock(sem);

  return 0;
}

int sem_wait(sem_t *sem) {
  if (!sem)
    return -1;

  if (sem->sem_init_magic != SEM_INIT_MAGIC)
    return -1;

  // wait for resource
  while (true) {
    internal_lock(sem);
    if (sem->value != 0) {
      sem->value--;
      internal_unlock(sem);
      return 0;
    }

    internal_unlock(sem);
    cpu_yield();
  }

  return 0;
}

int sem_trywait(sem_t *sem) {
  if (!sem)
    return -1;

  if (sem->sem_init_magic != SEM_INIT_MAGIC)
    return -1;

  internal_lock(sem);
  if (sem->value == 0) {
    errno = EAGAIN;
    internal_unlock(sem);
    return -1;
  }

  sem->value--;
  internal_unlock(sem);
  return 0;
}

int sem_post(sem_t *sem) {
  if (!sem)
    return -1;

  if (sem->sem_init_magic != SEM_INIT_MAGIC)
    return -1;

  internal_lock(sem);
  sem->value++;
  internal_unlock(sem);

  return 0;
}
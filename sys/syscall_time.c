// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "syscall_internal.h"

struct timespec {
  int64_t tv_sec;
  int64_t tv_nsec;
};

struct timeval {
  int64_t tv_sec;
  int64_t tv_usec;
};

struct tms {
  int64_t tms_utime;
  int64_t tms_stime;
  int64_t tms_cutime;
  int64_t tms_cstime;
};

static inline uint64_t rdtsc_time(void) {
  uint32_t low, high;
  asm volatile("rdtsc" : "=a"(low), "=d"(high));
  return ((uint64_t)high << 32) | low;
}

uint64_t get_time_ns_highres(void) {
  extern volatile uint64_t kernel_ticks;
  extern volatile uint64_t last_tick_tsc;
  static uint64_t cycles_per_ms = 3000000;
  static uint64_t last_calib_tick = 0;
  static uint64_t last_calib_tsc = 0;

  uint64_t cur_tick = kernel_ticks;
  uint64_t cur_tsc = rdtsc_time();

  if (last_calib_tsc == 0) {
    last_calib_tick = cur_tick;
    last_calib_tsc = cur_tsc;
  } else if (cur_tick - last_calib_tick >= 100) {
    uint64_t dt = cur_tick - last_calib_tick;
    uint64_t dc = (cur_tsc > last_calib_tsc) ? (cur_tsc - last_calib_tsc) : 0;
    if (dt > 0 && dc > 0) {
      uint64_t cpm = dc / dt;
      if (cpm >= 100000 && cpm <= 100000000ULL) {
        cycles_per_ms = cpm;
      }
    }
    last_calib_tick = cur_tick;
    last_calib_tsc = cur_tsc;
  }

  uint64_t tick_ref = last_tick_tsc;
  uint64_t delta_tsc = (cur_tsc > tick_ref && tick_ref > 0) ? (cur_tsc - tick_ref) : 0;
  uint64_t sub_ms_ns = (delta_tsc * 1000000ULL) / (cycles_per_ms ? cycles_per_ms : 3000000ULL);
  if (sub_ms_ns >= 1000000ULL) {
    sub_ms_ns = 999999ULL;
  }

  return (cur_tick * 1000000ULL) + sub_ms_ns;
}

int64_t g_realtime_offset_sec = 0;

static uint64_t rtc_to_epoch_sec(int year, int month, int day, int hour, int minute, int second) {
  static const int days_before_month[12] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
  };
  if (year < 1970 || month < 1 || month > 12 || day < 1) return 0;
  int y = year;
  int m = month - 1;
  int leap_years = (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400;
  int is_leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
  uint64_t days = (uint64_t)(y - 1970) * 365 + leap_years + days_before_month[m] + (day - 1);
  if (is_leap && m >= 2) days++;
  return days * 86400ULL + (uint64_t)hour * 3600ULL + (uint64_t)minute * 60ULL + (uint64_t)second;
}

static void epoch_sec_to_datetime(uint64_t sec, int *year, int *month, int *day, int *hour, int *minute, int *second) {
  *second = sec % 60; sec /= 60;
  *minute = sec % 60; sec /= 60;
  *hour = sec % 24; sec /= 24;
  int days = (int)sec;
  int y = 1970;
  while (1) {
    int leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
    int days_in_year = leap ? 366 : 365;
    if (days < days_in_year) break;
    days -= days_in_year;
    y++;
  }
  *year = y;
  int leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
  static const int days_in_months[2][12] = {
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
    { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
  };
  int m = 0;
  while (m < 12 && days >= days_in_months[leap][m]) {
    days -= days_in_months[leap][m];
    m++;
  }
  *month = m + 1;
  *day = days + 1;
}

void time_init(void) {
  extern void rtc_get_datetime(int *year, int *month, int *day, int *hour, int *minute, int *second);
  int y = 0, m = 0, d = 0, h = 0, min = 0, s = 0;
  rtc_get_datetime(&y, &m, &d, &h, &min, &s);
  uint64_t rtc_sec = rtc_to_epoch_sec(y, m, d, h, min, s);
  uint64_t uptime_ns = get_time_ns_highres();
  g_realtime_offset_sec = (int64_t)rtc_sec - (int64_t)(uptime_ns / 1000000000ULL);
}

uint64_t handle_sys_clock_gettime(const syscall_args_t *args) {
  int clock_id = (int)args->arg1;
  struct timespec *tp = (struct timespec *)args->arg2;
  if (!is_valid_user_ptr(tp, sizeof(struct timespec))) return (uint64_t)-14; /* EFAULT */
  uint64_t ns = get_time_ns_highres();
  if (clock_id == 0 /* CLOCK_REALTIME */ || clock_id == 5 /* CLOCK_REALTIME_COARSE */) {
    tp->tv_sec = (int64_t)(ns / 1000000000ULL) + g_realtime_offset_sec;
  } else {
    tp->tv_sec = (int64_t)(ns / 1000000000ULL);
  }
  tp->tv_nsec = (int64_t)(ns % 1000000000ULL);
  return 0;
}

uint64_t handle_sys_clock_settime(const syscall_args_t *args) {
  int clock_id = (int)args->arg1;
  const struct timespec *tp = (const struct timespec *)args->arg2;
  if (!is_valid_user_ptr(tp, sizeof(struct timespec))) return (uint64_t)-14;
  if (clock_id != 0 /* CLOCK_REALTIME */) return (uint64_t)-22; /* EINVAL */

  uint64_t ns = get_time_ns_highres();
  g_realtime_offset_sec = tp->tv_sec - (int64_t)(ns / 1000000000ULL);

  extern void rtc_set_datetime(int year, int month, int day, int hour, int minute, int second);
  int y, m, d, h, min, s;
  epoch_sec_to_datetime((uint64_t)tp->tv_sec, &y, &m, &d, &h, &min, &s);
  rtc_set_datetime(y, m, d, h, min, s);
  return 0;
}

uint64_t handle_sys_clock_getres(const syscall_args_t *args) {
  struct timespec *tp = (struct timespec *)args->arg2;
  if (is_valid_user_ptr(tp, sizeof(struct timespec))) {
    tp->tv_sec = 0;
    tp->tv_nsec = 1;
  }
  return 0;
}

uint64_t handle_sys_gettimeofday(const syscall_args_t *args) {
  struct timeval *tv = (struct timeval *)args->arg1;
  if (is_valid_user_ptr(tv, sizeof(struct timeval))) {
    uint64_t ns = get_time_ns_highres();
    tv->tv_sec = (int64_t)(ns / 1000000000ULL) + g_realtime_offset_sec;
    tv->tv_usec = (int64_t)((ns % 1000000000ULL) / 1000ULL);
  }
  return 0;
}

uint64_t handle_sys_settimeofday(const syscall_args_t *args) {
  const struct timeval *tv = (const struct timeval *)args->arg1;
  if (!tv) return 0;
  if (!is_valid_user_ptr(tv, sizeof(struct timeval))) return (uint64_t)-14;
  struct timespec ts = { .tv_sec = tv->tv_sec, .tv_nsec = tv->tv_usec * 1000 };
  syscall_args_t sargs = *args;
  sargs.arg1 = 0; // CLOCK_REALTIME
  sargs.arg2 = (uint64_t)&ts;
  return handle_sys_clock_settime(&sargs);
}

uint64_t handle_sys_times(const syscall_args_t *args) {
  struct tms *buf = (struct tms *)args->arg1;
  extern volatile uint64_t kernel_ticks;
  if (is_valid_user_ptr(buf, sizeof(struct tms))) {
    buf->tms_utime = (int64_t)kernel_ticks;
    buf->tms_stime = 0;
    buf->tms_cutime = 0;
    buf->tms_cstime = 0;
  }
  return (uint64_t)kernel_ticks;
}

uint64_t handle_sys_nanosleep(const syscall_args_t *args) {
  struct timespec *req = (struct timespec *)args->arg1;
  if (!is_valid_user_ptr(req, sizeof(struct timespec))) return (uint64_t)-14;
  uint64_t ms = (uint64_t)req->tv_sec * 1000ULL + (uint64_t)req->tv_nsec / 1000000ULL;
  if (ms == 0 && req->tv_nsec > 0) ms = 1;
  extern uint32_t get_ticks(void);
  uint32_t ticks = (uint32_t)ms;
  if (ticks == 0 && ms > 0) ticks = 1;
  process_t *proc = process_get_current();
  if (proc) {
    proc->sleep_until = get_ticks() + ticks;
    proc->state = PROC_STATE_BLOCKED;
  }
  return 0;
}

#define FUTEX_BUCKETS 64

typedef struct futex_waiter_entry futex_waiter_t;

typedef struct {
  futex_waiter_t *head;
  spinlock_t lock;
} futex_bucket_t;

static futex_bucket_t g_futex_buckets[FUTEX_BUCKETS];
static bool g_futex_initialized = false;

void futex_init(void) {
  for (int i = 0; i < FUTEX_BUCKETS; i++) {
    g_futex_buckets[i].head = NULL;
    g_futex_buckets[i].lock = SPINLOCK_INIT;
  }
  g_futex_initialized = true;
}

static inline futex_bucket_t *futex_bucket(uintptr_t key) {
  return &g_futex_buckets[(key >> 2) & (FUTEX_BUCKETS - 1)];
}

static inline uintptr_t futex_get_phys(process_t *proc, uint32_t *uaddr) {
  if (!proc || !uaddr) return 0;
  extern uintptr_t mmu_virt_to_phys(mmu_context_t *ctx, uintptr_t virt);
  if (proc->vmm_space && proc->vmm_space->mmu_ctx) {
    return mmu_virt_to_phys(proc->vmm_space->mmu_ctx, (uintptr_t)uaddr);
  }
  if (proc->pml4_phys) {
    mmu_context_t tmp_ctx = { .pml4_phys = proc->pml4_phys, .lock = SPINLOCK_INIT };
    return mmu_virt_to_phys(&tmp_ctx, (uintptr_t)uaddr);
  }
  return 0;
}

int kernel_futex_wait(uint32_t *uaddr, uint32_t expected) {
  process_t *proc = process_get_current();
  if (!proc)
    return -1;

  uintptr_t phys = futex_get_phys(proc, uaddr);
  uintptr_t key = phys ? phys : ((uintptr_t)uaddr ^ (uintptr_t)proc->pml4_phys);

  futex_bucket_t *b = futex_bucket(key);

  uint64_t flags = spinlock_acquire_irqsave(&b->lock);

  if (*uaddr != expected) {
    spinlock_release_irqrestore(&b->lock, flags);
    return -11; /* EAGAIN */
  }

  proc->futex_waiter.uaddr = uaddr;
  proc->futex_waiter.pml4_phys = proc->pml4_phys;
  proc->futex_waiter.phys_addr = phys;
  proc->futex_waiter.proc = (struct process *)proc;
  proc->futex_waiter.next = b->head;
  b->head = (futex_waiter_t *)&proc->futex_waiter;

  proc->state = PROC_STATE_BLOCKED;
  spinlock_release_irqrestore(&b->lock, flags);
  /* Caller (handle_sys_futex) must trigger a reschedule */
  return 0;
}

int kernel_futex_wake(uint32_t *uaddr, int count) {
  process_t *proc = process_get_current();
  if (!proc)
    return 0;

  uintptr_t phys = futex_get_phys(proc, uaddr);
  uintptr_t key = phys ? phys : ((uintptr_t)uaddr ^ (uintptr_t)proc->pml4_phys);

  futex_bucket_t *b = futex_bucket(key);
  int woken = 0;

  uint64_t flags = spinlock_acquire_irqsave(&b->lock);

  futex_waiter_t **pprev = &b->head;
  futex_waiter_t *cur = b->head;
  while (cur && woken < count) {
    bool match = false;
    if (phys != 0 && cur->phys_addr != 0 && cur->phys_addr == phys) {
      match = true;
    } else if (cur->pml4_phys == proc->pml4_phys && cur->uaddr == uaddr) {
      match = true;
    }

    if (match) {
      *pprev = cur->next; /* unlink */
      if (cur->proc) {
        ((process_t *)cur->proc)->state = PROC_STATE_RUNNING;
      }
      cur->uaddr = NULL;
      cur->pml4_phys = 0;
      cur->phys_addr = 0;
      cur->next = NULL;
      woken++;
      cur = *pprev; /* continue from same position */
    } else {
      pprev = &cur->next;
      cur = cur->next;
    }
  }

  spinlock_release_irqrestore(&b->lock, flags);
  if (woken > 0) {
    extern void smp_wake_idle_cpus(void);
    smp_wake_idle_cpus();
  }
  return woken;
}

uint64_t handle_sys_futex(const syscall_args_t *args) {
  uint32_t *uaddr = (uint32_t *)args->arg1;
  int op = (int)args->arg2;
  uint32_t val = (uint32_t)args->arg3;

  const struct timespec *timeout = (const struct timespec *)args->arg4;

  if (!uaddr || !is_valid_user_ptr(uaddr, sizeof(uint32_t)))
    return (uint64_t)-EFAULT;

  int cmd = op & 0x7F;

  if (cmd == 0 || cmd == 9) { // FUTEX_WAIT or FUTEX_WAIT_BITSET
    if (timeout && is_valid_user_ptr(timeout, sizeof(struct timespec))) {
      process_t *proc = process_get_current();
      if (proc) {
        uint64_t ms = (uint64_t)timeout->tv_sec * 1000ULL + (uint64_t)timeout->tv_nsec / 1000000ULL;
        if (ms == 0 && timeout->tv_nsec > 0) ms = 1;
        extern uint32_t get_ticks(void);
        proc->sleep_until = get_ticks() + (uint32_t)ms;
      }
    }
    int rc = kernel_futex_wait(uaddr, val);
    return (uint64_t)rc;
  }

  if (cmd == 1 || cmd == 10) { // FUTEX_WAKE or FUTEX_WAKE_BITSET
    int woken = kernel_futex_wake(uaddr, (int)val);
    return (uint64_t)woken;
  }

  return 0;
}

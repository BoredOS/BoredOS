#ifndef BOREDOS_OPENTTD_TIMER_H
#define BOREDOS_OPENTTD_TIMER_H

#include <stdint.h>

uint32_t openttd_boredos_ticks_ms(void);
void openttd_boredos_sleep_ms(uint32_t ms);

#endif

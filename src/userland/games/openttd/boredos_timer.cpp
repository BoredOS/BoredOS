extern "C" {
#include "libc/syscall.h"
}

#include "boredos_timer.h"

uint32_t openttd_boredos_ticks_ms(void)
{
    uint32_t ticks = (uint32_t)sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
    return (ticks * 50u) / 3u;
}

void openttd_boredos_sleep_ms(uint32_t ms)
{
    sys_system(SYSTEM_CMD_SLEEP, ms, 0, 0, 0);
}

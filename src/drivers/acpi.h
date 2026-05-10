#ifndef ACPI_H
#define ACPI_H

int acpi_init(void);

__attribute__((noreturn)) void acpi_shutdown(void);
__attribute__((noreturn)) void acpi_reboot(void);

uint32_t acpi_irq_to_gsi(uint32_t irq);
uint16_t acpi_irq_flags(uint32_t irq);

//power stuff

#define PM1A_CNT    fadt->pm1a_cnt_blk
#define PM1B_CNT    fadt->pm1b_cnt_blk

#define ACPI_S5    0x5
#define SLP_EN     (1 << 13)

#define ACPI_PM1_SLEEP_CMD(slp_typ)  (((slp_typ) << 10) | SLP_EN)

void acpi_parse_s5(void);
__attribute__((noreturn)) void acpi_shutdown(void);

#endif
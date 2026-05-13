/*
 * Copyright (c) 2026 Myles "Mellurboo" Wilson
 * Unauthorized copying or distribution is prohibited.
 * Provided "as is" without warranty.
*/

#ifndef ACPI_I2C_H
#define ACPI_I2C_H

#include <stdint.h>
#include <stddef.h>
#include "../ACPI/acpi_aml.h"

#define ACPI_I2C_MAX_DEVICES  32

int acpi_i2c_enumerate(void);

size_t acpi_i2c_count(void);

const aml_i2c_dev_t *acpi_i2c_get(size_t index);

#endif
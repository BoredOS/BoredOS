# I2C Subsystem

BoredOS provides a Linux-inspired I2C subsystem for managing I2C controllers (adapters) and communicating with slave devices. The implementation is located in `src/drivers/I2C/`.

## Architecture

The subsystem consists of:
- **I2C Core (`i2c.c/h`)**: Manages adapter registration and provides the master transfer API.
- **I2C Adapters**: Hardware-specific drivers that implement the actual bus transactions.
- **ACPI Integration (`acpi_i2c.c/h`)**: Enumerates I2C devices from ACPI tables and matches them to controllers.

### Data Structures

#### `i2c_msg_t`
Describes a single I2C transaction (read or write).
```c
typedef struct {
    uint16_t addr;     // 7-bit or 10-bit slave address
    uint16_t flags;    // I2C_M_RD, I2C_M_TEN, I2C_M_NOSTART
    uint16_t len;      // Number of bytes
    uint8_t *buf;      // Data buffer
} i2c_msg_t;
```

#### `i2c_adapter_t`
Represents an I2C controller.
```c
typedef struct i2c_adapter {
    const char *name;
    const aml_i2c_dev_t *acpi_dev;
    void *priv;
    i2c_master_xfer_fn master_xfer;
    bool active;
} i2c_adapter_t;
```

---

## Intel LPSS I2C Driver

The primary I2C driver in BoredOS is the Intel Low Power Subsystem (LPSS) driver (`i2c_lpss.c`), which targets Intel Core processors from Sky Lake through Meteor Lake. (Though only tested on Tiger Lake.)

### Features
- **PCI Discovery**: Automatically scans for LPSS I2C controllers (Class 0C0300 or 1180).
- **Generation Awareness**: Handles differences between older (Sky Lake/Kaby Lake) and newer (Ice Lake/Tiger Lake+) hardware.
- **Packed Registers**: Supports the "packed" MMIO layout used in newer Intel generations, where multiple registers are compressed into single 32-bit words.
- **Power Management**: Performs D3 to D0 power state transitions required for controller initialization.
- **Clock & Reset**: Manages LPSS-specific private registers for clock gating and hardware reset.

### Controller Matching
The driver matches physical PCI controllers to ACPI device nodes by scanning the ACPI namespace for I2C devices (HIDs like `PNP0C50`, `SYNA`, etc.) and assigning them to available controllers.

---

## ACPI Enumeration

I2C devices are discovered by walking the ACPI namespace (DSDT and SSDTs).

- **HID Matching**: Recognizes common touchpad and input HIDs:
    - `PNP0C50` (Standard HID-over-I2C)
    - `SYNA` (Synaptics)
    - `ELAN` (Elantech)
    - `ALPS` (Alps)
- **Resource Extraction**: Parses `_CRS` (Current Resource Settings) to extract:
    - Slave Address
    - Connection Speed (Standard, Fast, etc.)
- **Controller Detection**: Specifically identifies Intel I2C controllers via HIDs `INTC1040` and `INTC1043`.

---

## API Usage

### Registering an Adapter
Drivers call `i2c_adapter_register()` during initialization.
```c
int i2c_adapter_register(i2c_adapter_t *adapter);
```

### Performing Transfers
The core provides `i2c_master_xfer()` to perform one or more messages in a single transaction (often used for "write then read" patterns).
```c
int i2c_master_xfer(i2c_adapter_t *adapter, i2c_msg_t *msgs, int num);
```

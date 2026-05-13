<div align="center">
  <h1>ACPI</h1>
  <p><em>Power management and hardware enumeration via the Advanced Configuration and Power Interface.</em></p>
</div>

---

BoredOS implements an ACPI subsystem that handles power state transitions, hardware discovery, and I2C device enumeration from the firmware tables. The implementation lives in `src/drivers/ACPI/`.

## Table Discovery

At boot, the kernel requests the RSDP (Root System Description Pointer) from Limine. If the pointer is absent or fails its checksum, the kernel panics - this is a hard requirement, not a graceful fallback.

From the RSDP, the kernel determines which top-level descriptor table to use:

- **XSDT** (64-bit pointers) - used when RSDP revision ≥ 2 and a valid `xsdt_address` is present.
- **RSDT** (32-bit pointers) - fallback for older firmware.

This selection is handled transparently by `acpi_get_sdt()`, which walks whichever table is available and returns the first SDT matching a given 4-char signature.

> [!NOTE]
> The DSDT is a special case - it is not listed in the XSDT/RSDT. It is instead pointed to directly by the `dsdt` field in the FADT. Use `acpi_get_dsdt()` to retrieve it.

---

## FADT and ACPI Enable

The FADT (Fixed ACPI Description Table, signature `"FACP"`) is located first. It contains the port addresses needed for power management and the SMI command port used to hand off hardware control from firmware to the OS.

If `smi_cmd` and `acpi_enable` are both non-zero, the kernel writes the enable byte to the SMI command port and spins on `PM1a_CNT` bit 0 until the hardware acknowledges. A timeout here is a fatal error.

---

## MADT and Interrupt Routing

The MADT (Multiple APIC Description Table, signature `"APIC"`) is parsed for Interrupt Source Override entries (type 2). These describe cases where a legacy ISA IRQ is wired to a different GSI on the I/O APIC - for example, IRQ 0 (PIT) may be redirected to GSI 2 on some platforms.

Two functions expose this mapping to the rest of the kernel:

- `acpi_irq_to_gsi(irq)` - returns the GSI for a given IRQ, or the IRQ itself if no override exists.
- `acpi_irq_flags(irq)` - returns the polarity and trigger mode flags for the override.

---

## AML Parsing

The DSDT and any SSDTs present in the firmware contain AML (ACPI Machine Language) bytecode describing the hardware namespace. BoredOS does not implement a full AML interpreter - instead `src/drivers/ACPI/acpi_aml.c` implements a targeted byte-stream walker sufficient for I2C device enumeration.

The walker handles:

- **`_HID`** - device identification, either as an inline string or a packed EISAID integer.
- **`_CRS`** - current resource settings; specifically scans for `I2cSerialBusV2` descriptors (large item tag `0x8E`) to extract slave address, bus speed, and addressing mode.
- **`_DSM`** - device-specific method; scans the raw method body for the I2C-HID GUID (`3cdff6f7-4267-4555-ad05-b30a3d8938de`) and extracts the HID descriptor register address from the static return package.
- **Power states** - records presence of `_PS0`, `_PS3`, `_PR0`, `_PR3` as flags.

> [!IMPORTANT]
> `_CRS` and `_DSM` must be statically defined `Name()` objects for the walker to read them. Dynamically computed resources via AML method evaluation are not supported.

---

## I2C Enumeration

`acpi_i2c_enumerate()` is called at the end of `acpi_init()`. It walks the DSDT via `acpi_get_dsdt()` followed by all SSDTs found in the XSDT/RSDT, feeding each into `aml_walk_table()`.

Devices are emitted into a flat table when both a `_HID` and a valid `I2cSerialBusV2` resource (non-zero `connection_speed`) are found. The result is accessible via:

- `acpi_i2c_count()` - number of enumerated devices.
- `acpi_i2c_get(index)` - pointer to an `aml_i2c_dev_t` record containing the name, HID, slave address, speed, DSM result, and power flags.

---

## Shutdown

Performing a clean S5 (soft power-off) requires writing the correct `SLP_TYP` value to the PM1 Control Block. This value is firmware-specific and must be read from the `_S5_` object in the DSDT.

`aml_parse_s5()` scans the DSDT AML for the `_S5_` name and extracts `SLP_TYPa` and `SLP_TYPb`, pre-shifted to bit position 10 ready for `outw`. If the parse fails, the kernel falls back to `SLP_TYP = 5`, which works on the majority of hardware.

### Shutdown sequence

On compliant hardware, step 1 is all that runs and the machine powers off immediately. The remaining steps target known virtual machine implementations that don't respond to standard ACPI.

1. Write `SLP_TYPa | SLP_EN` to `PM1a_CNT_BLK` (and `PM1b_CNT_BLK` if present).
2. Write known virtualizer-specific magic values - QEMU (`0x604`), VirtualBox (`0x4004`), Bochs (`0xB004`), Cloud Hypervisor (`0x600`) - to trigger their respective shutdown paths.
3. Disable interrupts and halt indefinitely as a last resort.

> [!NOTE]
> The virtualizer ports are sent after the ACPI write deliberately. On real hardware, sending those port values is harmless, but the ordering avoids any ambiguity about what triggered the power-off.
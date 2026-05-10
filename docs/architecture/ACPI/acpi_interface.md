<div align="center">
 <h1>ACPI</h1>
 <p><em>ACPI Power Interface</em></p>
</div>

ACPI Subsystem

BoredOS implements an ACPI subsystem which manages power state transitions. 
The implementation lives
in `src/acpi/`

---

### Startup and Table Discovery

The first thing that ACPI does at boot time is to locate the RSDP.This can simply be requested fromlimine.
If the pointer is not present or the pointer is not valid we panic!It is a hardware requirement

If this is not present then we also check a checksum if that fails then the RSDP is also not valid and we panic

XSDT vs RSDT Fallback
When the RSDP specifies aRevision of 2 or greater, and we have a valid XSDT Address then we should try and use the XSDT instead,otherwise we use the RSDT.This is automatically abstracted by `acpi_get_sdt()`

---

### Shutdown and Power Off

 This works by writing an appropriate value to PM1 Control Block register, and will prompt hardware to move into S5 power state.

### Performing the power off
Once the sleep types are identified and so on, this will continue as follows
On hardware that actually obeys ACPI standards this process should only perform step 1, and your machine should be turned off. To try and handle virtual machines or emulators that are known to fail at step 1,

 we send the virtual machine interrupts second in case writing bytes makes our hardware think it is acting up and we do not wish to cause an unnecessary interrupt


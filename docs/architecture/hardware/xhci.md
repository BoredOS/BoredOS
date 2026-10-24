# xHCI - BoredOS implementation
The eXtensible Host Controller Interface, or as its more commonly referred to, xHCI, is Intels modern solution for handling of USB3.0+ & legacy USB2.0 devices. BoredOS uses this specification for a compliant, efficient and slim xHCI driver.

>### Note:
> This is not a specification of how a xHCI works, but rather its an implementation guide so you may create your own HID or USB device drivers that interface with the exposed APIs. Why? Well, the Intel spec is well over 500 pages, and the actual `C` implementation is arguably much more useful as reference.

With that, here is a general overview to understand how the xHCI controller functions within boredOS, and how you could implement your own USB subsystem!

## Whats missing?
This isn't a feature complete xHCI driver. Those are huge, and require handling bizarre edgecases of freaky hardware. Your hardware from a non-compliant vendor may not work! However, past that, there are some QOL & technical features missing.

1) Hot plugging
2) Misbehaving device handling*
3) TRB chaining
4) Missing USB hub interface**

```
* This is technically false as they are immediately disabled. However, a full xHCI implementation should probably handle weird vendor edge cases
** this is technically not a limitation of the xHCI controller (you could easily write a module for this), however because some AMD systems have a USB hub on level 1, you may discover some systems cannot detect any USBs due to this limitation.
```

## API
The xHCI interface has one job, to interface with the xHC. It will then scan ports to discover independant hardware via their `CC`, `SC`, `PROT`, which will then be checked against a list of registered drivers (configured to a max of 16), in order to deduce which driver is the right one for the job. This functionality is implemented within `src/drivers/xHCI/xHCI_drvr.c`. The struct we care about is this one:
```C
typedef struct {
    uint8_t cc;
    uint8_t sc;
    uint8_t prot;
    uint8_t match;
} xHCI_CCSCPR_t;
```
Which allows us to register our custom xHCI class simply, and effectively.

However, the xHCI driver also needs to callback to our driver to initialize it, and invoke interrupts. This is where `xHCI_EXTD_t` comes into play. EXTD, or EXTended Device, allows you to modularise the xHCI components.

```C
struct xHCI_EXTD_t {
    void (*destroy)(xHCI_EXTD_t *this);
    void (*initialize)(xHCI_EXTD_t *this, xHCI_PCI_t *host, uint8_t _slot, void *endpoints, int ep_count);
    void (*on_int)(xHCI_EXTD_t *this, uint32_t bytes_transferred, uint32_t endpoint_id, uint64_t param_event);
    void (*start)(xHCI_EXTD_t *this);
    void *local_data;
};
```

## Implementation
Lets analyze how the HID keyboard implements this:
```C
void usbkb_init(void) {
    serial_write("[USBKB] Submitting keyboard boot HID driver\n");

    xHCI_EXTD_t *xHCI_dev = kzalloc(sizeof(xHCI_EXTD_t));
    xHCI_dev->initialize = kbusb_initialize;
    xHCI_dev->start = kbusb_start;
    xHCI_dev->on_int = kbusb_on_int;

    xHCI_CCSCPR_t classes = {
        .cc = XHCI_CL_USB_HID,
        .prot = XHCI_PR_HID_KB,
        .sc = XHCI_BOOT_SUBCL,
        .match = XHCI_MATCH_SC | XHCI_MATCH_PROT
    };

    xHCI_submit_new_drvr(classes, xHCI_dev);
}
```

As you can see from the above function, the implementation begins by allocating the necessary structure, `xHCI_EXTD_t`, and populating it with the necessary function pointers. Then, we allocate a stack `xHCI_CCSCPR_t`, which contains our specified `CC` (class code), `SC` (subclass), `PROT` (protocol). In our case, boot compliant HID keyboards use the values (in order) `0x3, 0x1, 0x1`. This is also why we pass the `.match = XHCI_MATCH_SC | XHCI_MATCH_PROT`, as we do have specific protocol & subclass values to detect. This is unlike a USB mass storage device, where you only need to match the class code. Finally, you invoke `xHCI_submit_new_drvr` with your two structs, and you are good to go! However, what should a good initialize or a good start function look like?

Well, it is up to you, for the most part. It is device specific, however you should know the following:

1) init is ALWAYS invoked before start
2) start is invoked after more things have been setup
3) init's signature has more paramaters.

So at the bare minimum you should at least be defining your private `void *` variable within init. Otherwise it'd be a bit of a headache to use start.

If we look again at the USB Keyboard for its init function, and start function, you can see that clearly:

```C
void kbusb_initialize(xHCI_EXTD_t *this, xHCI_PCI_t *host, uint8_t slot, void *ep_ptr, int ep_count) {
    USB_kbd_t *this_keyboard = kzalloc(sizeof(USB_kbd_t));
    this_keyboard->xHCI_ctrl = host;

    this->local_data = this_keyboard;
    this_keyboard->slot_id = slot;

    xHCI_PE_t *parsed_eps = (xHCI_PE_t *)(ep_ptr);

    for (int i = 0; i < ep_count; i++) {
        uint8_t ep_addr = parsed_eps[i].address;
        bool is_in = (ep_addr & XUSB_EP_DIR_MASK) != 0;
        uint8_t ep_type = parsed_eps[i].attributes & XUSB_EP_ATTR_MASK;

        if (ep_type == XUSB_EP_ATTR_INT && is_in) {
            this_keyboard->int_in = ep_addr;
            break; 
        }
    }

    if (!this_keyboard->int_in) {
        serial_write("[USBKB] Error: Keyboard missing int-in endpoint?\n");
        return;
    }

    xHCI_send_req(host, slot, 0x21, 0xB, 0, 0, 0, 0); // Boot mode config
    xHCI_send_req(host, slot, 0x21, 0xA, 0, 0, 0, 0);
    
    page_t *allocd = pmm_alloc_page(PMM_ZONE_DMA32);;
    this_keyboard->report_phys = pmm_page_to_paddr(allocd);
    this_keyboard->report_virt = pmm_page_to_vaddr(allocd);

    memset(this_keyboard->report_virt, 0, sizeof(USB_kbbr_t));
    memset(&this_keyboard->last_report, 0, sizeof(USB_kbbr_t));

    serial_write("[USBKB] Configured boot keyboard driver\n");
}
```

As you can clearly see above, this init function handles general private data configuration and allocation, while also checking that the provided slots are what is expected. Contrast this to the very small start function:
```C
void kbusb_start(xHCI_EXTD_t *this) {
    serial_write("[USBKB] Starting HID USB Keyboard polling loop\n");
    USB_kbd_t *this_usb = this->local_data;

    xHCI_queue_transfer(this_usb->xHCI_ctrl, this_usb->slot_id, this_usb->int_in, this_usb->report_phys, sizeof(USB_kbbr_t));
}
```
**But why?** Well, in the case of this HID device, once you configure the data, its just about letting the keyboard know you are waiting for a keystroke, and once one occurs its about just echoing back you are ready for the next one. Thats literally why its just grab the local struct data, and dispatch a transfer.

> [!WARNING]
> Do not invoke `xHCI_queue_transfer` during init. This is the whole reason start is seperate, as it runs when the device is generally ready.

Finally we have the topics of `on_int` and `destroy`. These are rather tricky to demonstrate as they require a specific implementation depending on the end device. During `on_int` the mass storage driver uses a state machine to manage in/out data, whereas the HID keyboard just translates the packet and dispatches it to the tty. Destroy on the other hand just involves cleaning up the xHCI device paramaters and other general data once the USB hardware ends its connection.

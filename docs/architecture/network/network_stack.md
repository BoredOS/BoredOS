# Networking Stack

BoredOS features a robust networking stack capable of handling Ethernet, IPv4, TCP, UDP, ICMP, DHCP, and DNS. The stack is built on top of the **lwIP (Lightweight IP)** library, which is integrated with a custom hardware driver layer.

## 1. Architecture Overview

The network stack is split into three main layers:
1. **Hardware/Driver Layer (`src/net/nic/`)**: Communicates with physical and virtual Network Interface Cards (NICs), handling raw Ethernet frame transmission and reception. Supported drivers include the Intel E1000 (`e1000.c`), Realtek RTL8139 (`rtl8139.c`), Realtek RTL8111 (`rtl8111.c`), and VirtIO network devices (`virtio_net.c`). A generic interface is provided via `nic.c`.
2. **Protocol Layer (lwIP)**: Processes Ethernet frames, handles ARP resolution, routes IPv4 packets, and manages TCP state machines.
3. **OS Interface Layer (`src/net/network.c`)**: Wraps the asynchronous lwIP API into a synchronous, easy-to-use API for BoredOS applications and the kernel.

## 2. Initialization & Polling

### `network_init()`
When the kernel boots, it initializes the network subsystem by:
1. Probing the PCI bus for supported NICs (e.g., the Intel E1000).
2. Initializing the lwIP core (`lwip_init()`) and DNS subsystem.
3. Binding the hardware NIC to lwIP using `netif_add`.
4. Automatically attempting to acquire an IP address via DHCP (`network_dhcp_acquire()`).

### The Polling Mechanism (`network_process_frames`)
Unlike some operating systems that process network packets entirely inside hardware interrupt handlers, BoredOS uses a **polled approach** to avoid re-entrancy issues in the TCP/IP stack. 

The `network_process_frames()` function is called periodically (e.g., from the Window Manager loop or during blocking network calls). It calls:
- `nic_netif_poll()`: Pulls raw packets from the NIC ring buffer and feeds them to lwIP (`ethernet_input`).
- `sys_check_timeouts()`: Fires lwIP internal timers for TCP retransmissions, ARP cache expiration, and DHCP lease renewals.

A `network_processing` flag acts as a lightweight spinlock to prevent nested execution of the network poll loop.

## 3. TCP Implementation & Application API

While lwIP provides a callback-based raw API, BoredOS wraps this into a sequential API for userland applications.

Currently, the OS supports **one active TCP connection globally across the entire system**. The connection state is managed via a global Protocol Control Block (`current_tcp_pcb`). To prevent unauthorized cleanup, the OS tracks which process initiated the connection (`tcp_owner_pid`). If a new process attempts to connect while a connection is active, the existing connection is forcefully aborted.

### `network_tcp_connect(ip, port)`
1. Allocates a new Protocol Control Block (`tcp_new()`).
2. Registers callbacks for receive (`tcp_recv_callback`), error, and connection success.
3. Blocks (while polling the network) until the connection succeeds or times out after 15 seconds.

### `network_tcp_recv(buf, max_len)`
When packets arrive, `tcp_recv_callback` chains them into a `tcp_recv_queue` (`struct pbuf`). 
The `network_tcp_recv` function blocks until data is available in this queue, then copies it into the application's buffer and frees the `pbuf`. A non-blocking variant (`network_tcp_recv_nb`) is also provided.

### Process Cleanup (`network_cleanup`)
If an application crashes or exits without closing its socket, the kernel's process manager calls `network_cleanup()`. This checks if the exiting process owns the current TCP PCB (`tcp_owner_pid`) and forcibly aborts the connection to prevent resource leaks.

## 4. Helper Protocols

- **DHCP:** Managed entirely by lwIP. BoredOS simply waits up to 10 seconds during boot for a lease.
- **DNS (`network_dns_lookup`):** Uses lwIP's `dns_gethostbyname`. It blocks and polls until the callback is triggered with the resolved IP address.
- **ICMP (Ping):** The kernel provides a `network_icmp_single_ping` function using an lwIP raw socket (`raw_pcb`) to construct, checksum, and transmit an ICMP Echo Request, blocking until a reply is received to calculate the Round-Trip Time (RTT).

#include "network.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/stats.h"
#include "lwip/raw.h"
#include "lwip/sys.h"
#include "netif/ethernet.h"
#include "nic_netif.h"
#include "kutils.h"
#include "pci.h"
#include "e1000.h"
#include "nic.h"
#include "spinlock.h"

static struct netif nic_netif;
static int lwip_initialized = 0;

static struct tcp_pcb *current_tcp_pcb = NULL;
static struct pbuf *tcp_recv_queue = NULL;
static int tcp_connect_done = 0;
static int tcp_connect_error = 0;
static int tcp_closed = 0;
static uint32_t tcp_owner_pid = 0; 

static err_t tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg; (void)tpcb; (void)err;
    if (p == NULL) {
        // Connection closed
        tcp_closed = 1;
        return ERR_OK;
    }
    if (tcp_recv_queue == NULL) {
        tcp_recv_queue = p;
    } else {
        pbuf_chain(tcp_recv_queue, p);
    }
    return ERR_OK;
}

static void tcp_err_callback(void *arg, err_t err) {
    (void)arg; (void)err;
    current_tcp_pcb = NULL;
    tcp_connect_error = 1;
}

static err_t tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
    (void)arg; (void)tpcb;
    if (err == ERR_OK) {
        tcp_connect_done = 1;
    } else {
        tcp_connect_error = 1;
    }
    return ERR_OK;
}

int network_init(void) {
    if (lwip_initialized) return 0;
    
    // First, find and initialize the generic NIC device if not already done
    if (nic_init() != 0) {
        return -1; // No supported NIC found
    }

    lwip_init();
    dns_init(); // Explicitly init DNS just in case
    
    ip4_addr_t ipaddr, netmask, gw;
    ip4_addr_set_zero(&ipaddr);
    ip4_addr_set_zero(&netmask);
    ip4_addr_set_zero(&gw);
    
    if (netif_add(&nic_netif, &ipaddr, &netmask, &gw, NULL, nic_netif_init, ethernet_input) == NULL) {
        return -1;
    }
    
    netif_set_default(&nic_netif);
    netif_set_up(&nic_netif);
    
    lwip_initialized = 1;

    // Automate DHCP by default and return its result
    int dhcp_res = network_dhcp_acquire();
    
    if (dhcp_res == 0) {
        ipv4_address_t ip;
        network_get_ipv4_address(&ip);
        extern void serial_write(const char *str);
        serial_write("[NET] IP Assigned: ");
        char buf[32];
        itoa(ip.bytes[0], buf); serial_write(buf); serial_write(".");
        itoa(ip.bytes[1], buf); serial_write(buf); serial_write(".");
        itoa(ip.bytes[2], buf); serial_write(buf); serial_write(".");
        itoa(ip.bytes[3], buf); serial_write(buf); serial_write("\n");
    } else {
        extern void serial_write(const char *str);
        serial_write("[NET] DHCP Failed during init\n");
    }

    // Set default DNS server (1.1.1.1)
    ip_addr_t dns1;
    IP_ADDR4(&dns1, 1, 1, 1, 1);
    dns_setserver(0, &dns1);

    return dhcp_res;
}

int network_get_mac_address(mac_address_t* mac) {
    if (!lwip_initialized) return -1;
    return nic_get_mac_address(mac->bytes);
}

int network_get_nic_name(char* name_out) {
    if (!lwip_initialized) return -1;
    extern const char* nic_get_active_name(void);
    const char* n = nic_get_active_name();
    if (!n) return -1;
    while (*n) *name_out++ = *n++;
    *name_out = 0;
    return 0;
}

int network_get_ipv4_address(ipv4_address_t* ip) {
    if (!lwip_initialized) return -1;
    u32_t addr = ip4_addr_get_u32(netif_ip4_addr(&nic_netif));
    ip->bytes[0] = (addr >> 0) & 0xFF;
    ip->bytes[1] = (addr >> 8) & 0xFF;
    ip->bytes[2] = (addr >> 16) & 0xFF;
    ip->bytes[3] = (addr >> 24) & 0xFF;
    return 0;
}

int network_set_ipv4_address(const ipv4_address_t* ip) {
    if (!lwip_initialized) return -1;
    ip4_addr_t ipaddr;
    IP4_ADDR(&ipaddr, ip->bytes[0], ip->bytes[1], ip->bytes[2], ip->bytes[3]);
    netif_set_ipaddr(&nic_netif, &ipaddr);
    return 0;
}

static spinlock_t network_lock = SPINLOCK_INIT;
static void network_poll_internal(void) {
    nic_netif_poll(&nic_netif);
    sys_check_timeouts();
}

void network_process_frames(void) {
    if (!lwip_initialized) return;
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    network_poll_internal();
    spinlock_release_irqrestore(&network_lock, flags);
}

void network_force_unlock(void) {
    network_lock = 0;
}

void network_cleanup(void) {
    extern uint32_t process_get_current_pid(void);
    uint32_t my_pid = process_get_current_pid();
    if (tcp_owner_pid != 0 && tcp_owner_pid != my_pid) return;

    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    if (tcp_recv_queue) {
        pbuf_free(tcp_recv_queue);
        tcp_recv_queue = NULL;
    }
    if (current_tcp_pcb) {
        tcp_abort(current_tcp_pcb);
        current_tcp_pcb = NULL;
    }
    tcp_owner_pid = 0;
    spinlock_release_irqrestore(&network_lock, flags);
}

int network_dhcp_acquire(void) {
    if (!lwip_initialized) return -1;
    
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    serial_write("[DHCP] Starting...\n");
    dhcp_start(&nic_netif);
    spinlock_release_irqrestore(&network_lock, flags);
    
    uint32_t start = sys_now();
    asm volatile("sti");
    while (sys_now() - start < 10000) { // 10 second timeout
        network_process_frames();
        flags = spinlock_acquire_irqsave(&network_lock);
        if (dhcp_supplied_address(&nic_netif)) {
            serial_write("[DHCP] Bound!\n");
            spinlock_release_irqrestore(&network_lock, flags);
            return 0;
        }
        spinlock_release_irqrestore(&network_lock, flags);
        k_delay(500); // 5ms delay
    }
    serial_write("[DHCP] Timeout\n");
    return -1;
}

int network_tcp_connect(const ipv4_address_t *ip, uint16_t port) {
    if (!lwip_initialized) return -1;
    
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    if (current_tcp_pcb) tcp_abort(current_tcp_pcb);
    
    current_tcp_pcb = tcp_new();
    if (!current_tcp_pcb) { spinlock_release_irqrestore(&network_lock, flags); return -1; }

    extern uint32_t process_get_current_pid(void);
    tcp_owner_pid = process_get_current_pid();
    
    ip4_addr_t dest_addr;
    IP4_ADDR(&dest_addr, ip->bytes[0], ip->bytes[1], ip->bytes[2], ip->bytes[3]);
    
    tcp_connect_done = 0;
    tcp_connect_error = 0;
    tcp_closed = 0;
    tcp_recv_queue = NULL;
    
    tcp_recv(current_tcp_pcb, tcp_recv_callback);
    tcp_err(current_tcp_pcb, tcp_err_callback);
    err_t err = tcp_connect(current_tcp_pcb, &dest_addr, port, tcp_connected_callback);
    spinlock_release_irqrestore(&network_lock, flags);
    
    if (err != ERR_OK) return -1;
    
    uint32_t start = sys_now();
    asm volatile("sti");
    while (sys_now() - start < 15000) { // 15 second timeout
        network_process_frames();
        flags = spinlock_acquire_irqsave(&network_lock);
        if (tcp_connect_done) { spinlock_release_irqrestore(&network_lock, flags); return 0; }
        if (tcp_connect_error) { spinlock_release_irqrestore(&network_lock, flags); return -1; }
        spinlock_release_irqrestore(&network_lock, flags);
        k_delay(10);
    }
    return -1;
}

int network_tcp_send(const void *data, size_t len) {
    if (!current_tcp_pcb) return -1;
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    err_t err = tcp_write(current_tcp_pcb, data, len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) { spinlock_release_irqrestore(&network_lock, flags); return -1; }
    tcp_output(current_tcp_pcb);
    spinlock_release_irqrestore(&network_lock, flags);
    return (int)len;
}

int network_tcp_recv(void *buf, size_t max_len) {
    if (!lwip_initialized) return -1;

    uint32_t start = sys_now();
    asm volatile("sti");
    while (1) {
        uint64_t flags = spinlock_acquire_irqsave(&network_lock);
        if (tcp_recv_queue) {
            size_t to_copy = max_len;
            if (to_copy > tcp_recv_queue->tot_len) to_copy = tcp_recv_queue->tot_len;
            if (to_copy > 0xFFFF) to_copy = 0xFFFF;

            size_t copied = pbuf_copy_partial(tcp_recv_queue, buf, (u16_t)to_copy, 0);
            struct pbuf *remainder = pbuf_free_header(tcp_recv_queue, (u16_t)copied);
            if (current_tcp_pcb) tcp_recved(current_tcp_pcb, (u16_t)copied);
            tcp_recv_queue = remainder;
            spinlock_release_irqrestore(&network_lock, flags);
            return (int)copied;
        }
        if (tcp_closed) { spinlock_release_irqrestore(&network_lock, flags); return 0; }
        if (tcp_connect_error) { spinlock_release_irqrestore(&network_lock, flags); return -1; }
        spinlock_release_irqrestore(&network_lock, flags);

        if (sys_now() - start >= 30000) return 0;
        network_process_frames();
        k_delay(10);
    }
}

int network_tcp_recv_nb(void *buf, size_t max_len) {
    if (!lwip_initialized) return -1;
    network_process_frames();

    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    if (!tcp_recv_queue) {
        int ret = tcp_closed ? -2 : 0;
        spinlock_release_irqrestore(&network_lock, flags);
        return ret;
    }
    
    size_t to_copy = max_len;
    if (to_copy > tcp_recv_queue->tot_len) to_copy = tcp_recv_queue->tot_len;
    if (to_copy > 0xFFFF) to_copy = 0xFFFF;

    size_t copied = pbuf_copy_partial(tcp_recv_queue, buf, (u16_t)to_copy, 0);
    struct pbuf *remainder = pbuf_free_header(tcp_recv_queue, (u16_t)copied);
    if (current_tcp_pcb) tcp_recved(current_tcp_pcb, (u16_t)copied);
    tcp_recv_queue = remainder;
    spinlock_release_irqrestore(&network_lock, flags);
    return (int)copied;
}

int network_tcp_close(void) {
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    if (tcp_recv_queue) {
        pbuf_free(tcp_recv_queue);
        tcp_recv_queue = NULL;
    }
    if (current_tcp_pcb) {
        tcp_abort(current_tcp_pcb);
        current_tcp_pcb = NULL;
    }
    tcp_closed = 0;
    tcp_connect_done = 0;
    tcp_connect_error = 0;
    spinlock_release_irqrestore(&network_lock, flags);
    return 0;
}

static ip_addr_t dns_resolved_ip;
static int dns_done = 0;
static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    (void)name; (void)callback_arg;
    serial_write("[DNS] Callback triggered\n");
    if (ipaddr) {
        dns_resolved_ip = *ipaddr;
        dns_done = 1;
    } else {
        dns_done = -1;
    }
}

int network_dns_lookup(const char *name, ipv4_address_t *out_ip) {
    if (!lwip_initialized) return -1;
    
    dns_done = 0;
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    err_t err = dns_gethostbyname(name, &dns_resolved_ip, dns_callback, NULL);
    spinlock_release_irqrestore(&network_lock, flags);

    if (err == ERR_OK) {
        flags = spinlock_acquire_irqsave(&network_lock);
        u32_t addr = ip4_addr_get_u32(ip_2_ip4(&dns_resolved_ip));
        out_ip->bytes[0] = (addr >> 0) & 0xFF;
        out_ip->bytes[1] = (addr >> 8) & 0xFF;
        out_ip->bytes[2] = (addr >> 16) & 0xFF;
        out_ip->bytes[3] = (addr >> 24) & 0xFF;
        spinlock_release_irqrestore(&network_lock, flags);
        return 0;
    } else if (err == ERR_INPROGRESS) {
        uint32_t start = sys_now();
        asm volatile("sti");
        while (sys_now() - start < 10000) {
            network_process_frames();
            flags = spinlock_acquire_irqsave(&network_lock);
            if (dns_done == 1) {
                u32_t addr = ip4_addr_get_u32(ip_2_ip4(&dns_resolved_ip));
                out_ip->bytes[0] = (addr >> 0) & 0xFF;
                out_ip->bytes[1] = (addr >> 8) & 0xFF;
                out_ip->bytes[2] = (addr >> 16) & 0xFF;
                out_ip->bytes[3] = (addr >> 24) & 0xFF;
                spinlock_release_irqrestore(&network_lock, flags);
                return 0;
            }
            if (dns_done == -1) { 
                spinlock_release_irqrestore(&network_lock, flags);
                return -1; 
            }
            spinlock_release_irqrestore(&network_lock, flags);
            k_delay(10);
        }
    }
    return -1;
}

int network_set_dns_server(const ipv4_address_t *ip) {
    if (!lwip_initialized) return -1;
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    ip_addr_t addr;
    IP4_ADDR(ip_2_ip4(&addr), ip->bytes[0], ip->bytes[1], ip->bytes[2], ip->bytes[3]);
    dns_setserver(0, &addr);
    spinlock_release_irqrestore(&network_lock, flags);
    return 0;
}

int network_get_gateway_ip(ipv4_address_t *ip) {
    if (!lwip_initialized) return -1;
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    u32_t addr = ip4_addr_get_u32(netif_ip4_gw(&nic_netif));
    ip->bytes[0] = (addr >> 0) & 0xFF;
    ip->bytes[1] = (addr >> 8) & 0xFF;
    ip->bytes[2] = (addr >> 16) & 0xFF;
    ip->bytes[3] = (addr >> 24) & 0xFF;
    spinlock_release_irqrestore(&network_lock, flags);
    return 0;
}

int network_get_dns_ip(ipv4_address_t *ip) {
    if (!lwip_initialized) return -1;
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    const ip_addr_t *dns = dns_getserver(0);
    u32_t addr = ip4_addr_get_u32(ip_2_ip4(dns));
    ip->bytes[0] = (addr >> 0) & 0xFF;
    ip->bytes[1] = (addr >> 8) & 0xFF;
    ip->bytes[2] = (addr >> 16) & 0xFF;
    ip->bytes[3] = (addr >> 24) & 0xFF;
    spinlock_release_irqrestore(&network_lock, flags);
    return 0;
}

int network_is_initialized(void) { return lwip_initialized; }
int network_has_ip(void) { return lwip_initialized && !ip4_addr_isany_val(*netif_ip4_addr(&nic_netif)); }

int network_send_frame(const void* data, size_t length) { return nic_send_packet(data, length); }
int network_receive_frame(void* buffer, size_t buffer_size) { return nic_receive_packet(buffer, buffer_size); }

static u16_t icmp_cksum(void *data, int len) {
    u32_t sum = 0;
    u16_t *p = (u16_t *)data;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(u8_t *)p;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (u16_t)(~sum);
}

int udp_send_packet(const ipv4_address_t* dest_ip, uint16_t dest_port, uint16_t src_port, const void* data, size_t data_length) {
    (void)dest_ip; (void)dest_port; (void)src_port; (void)data; (void)data_length;
    return -1; 
}

static int ping_replies = 0;
static u16_t ping_seq = 0;
static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
    (void)arg; (void)pcb; (void)addr;
    if (p->tot_len >= 8) {
        u8_t *data = (u8_t *)p->payload;
        if (data[0] == 0) {
            ping_replies++;
        } else if (p->tot_len >= 28 && data[0] == 0x45 && data[20] == 0) {
            ping_replies++;
        }
    }
    pbuf_free(p);
    return 1;
}

int network_icmp_single_ping(ipv4_address_t *dest) {
    if (!lwip_initialized) return -2;
    
    // Synchronize network state during ICMP request to prevent re-entrancy issues
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb) { spinlock_release_irqrestore(&network_lock, flags); return -1; }
    raw_recv(pcb, ping_recv, NULL);
    raw_bind(pcb, IP_ADDR_ANY);
    ip_addr_t dest_addr;
    IP4_ADDR(ip_2_ip4(&dest_addr), dest->bytes[0], dest->bytes[1], dest->bytes[2], dest->bytes[3]);
    
    struct pbuf *p = pbuf_alloc(PBUF_IP, 8 + 56, PBUF_RAM); // 64 bytes total
    if (!p) { raw_remove(pcb); spinlock_release_irqrestore(&network_lock, flags); return -1; }
    u8_t *data = (u8_t *)p->payload;
    data[0] = 8; data[1] = 0; data[2] = 0; data[3] = 0;
    data[4] = 0; data[5] = 1; data[6] = (u8_t)(ping_seq >> 8); data[7] = (u8_t)(ping_seq & 0xFF);
    ping_seq++;
    for (int j = 0; j < 56; j++) data[8+j] = (u8_t)('a' + (j % 26));
    
    // Calculate ICMP Checksum
    u16_t chk = icmp_cksum(data, 8 + 56);
    data[2] = (u8_t)(chk & 0xFF);
    data[3] = (u8_t)(chk >> 8);
    
    ping_replies = 0;
    uint32_t start = sys_now();
    raw_sendto(pcb, p, &dest_addr);
    pbuf_free(p);
    spinlock_release_irqrestore(&network_lock, flags);
    
    asm volatile("sti");
    int rtt = -1;
    while (sys_now() - start < 1000) {
        network_process_frames();
        flags = spinlock_acquire_irqsave(&network_lock);
        if (ping_replies > 0) {
            rtt = (int)(sys_now() - start);
            spinlock_release_irqrestore(&network_lock, flags);
            break;
        }
        spinlock_release_irqrestore(&network_lock, flags);
        k_delay(10);
    }
    
    flags = spinlock_acquire_irqsave(&network_lock);
    raw_remove(pcb);
    spinlock_release_irqrestore(&network_lock, flags);
    return rtt;
}

int network_get_frames_received(void) { return (int)lwip_stats.link.recv; }
int network_get_udp_packets_received(void) { return (int)lwip_stats.udp.recv; }
int network_get_frames_sent(void) { return (int)lwip_stats.link.xmit; }
int network_get_udp_callbacks_called(void) { return 0; }
int network_get_e1000_receive_calls(void) { return 0; }
int network_get_e1000_receive_empty(void) { return 0; }
int network_get_process_calls(void) { return (int)lwip_stats.link.drop; }

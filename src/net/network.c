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

#define NETWORK_MAX_SOCKETS 32
#define NETWORK_SOCKET_FD_BASE 3
#define NETWORK_SOCKET_TYPE_TCP 1

typedef struct network_socket {
    int in_use;
    int fd;
    int type;
    uint32_t owner_pid;
    struct tcp_pcb *tcp_pcb;
    struct pbuf *recv_queue;
    int connect_done;
    int connect_error;
    int last_error;
    int closed;
} network_socket_t;

static network_socket_t network_sockets[NETWORK_MAX_SOCKETS];
static int legacy_tcp_fd = -1;

static network_socket_t *network_socket_from_fd(int fd) {
    int index = fd - NETWORK_SOCKET_FD_BASE;
    if (index < 0 || index >= NETWORK_MAX_SOCKETS) return NULL;
    if (!network_sockets[index].in_use) return NULL;

    extern uint32_t process_get_current_pid(void);
    uint32_t pid = process_get_current_pid();
    if (network_sockets[index].owner_pid != pid) return NULL;

    return &network_sockets[index];
}

static network_socket_t *network_socket_alloc(int type) {
    extern uint32_t process_get_current_pid(void);
    uint32_t pid = process_get_current_pid();

    for (int i = 0; i < NETWORK_MAX_SOCKETS; i++) {
        if (network_sockets[i].in_use) continue;

        network_socket_t *sock = &network_sockets[i];
        sock->in_use = 1;
        sock->fd = NETWORK_SOCKET_FD_BASE + i;
        sock->type = type;
        sock->owner_pid = pid;
        sock->tcp_pcb = NULL;
        sock->recv_queue = NULL;
        sock->connect_done = 0;
        sock->connect_error = 0;
        sock->last_error = 0;
        sock->closed = 0;
        return sock;
    }
    return NULL;
}

static void network_socket_reset_locked(network_socket_t *sock) {
    if (!sock) return;
    if (sock->recv_queue) {
        pbuf_free(sock->recv_queue);
        sock->recv_queue = NULL;
    }
    if (sock->tcp_pcb) {
        tcp_arg(sock->tcp_pcb, NULL);
        tcp_recv(sock->tcp_pcb, NULL);
        tcp_err(sock->tcp_pcb, NULL);
        tcp_abort(sock->tcp_pcb);
        sock->tcp_pcb = NULL;
    }
    sock->connect_done = 0;
    sock->connect_error = 0;
    sock->last_error = 0;
    sock->closed = 0;
}

static void network_socket_free_locked(network_socket_t *sock) {
    if (!sock) return;
    network_socket_reset_locked(sock);
    sock->in_use = 0;
    sock->fd = 0;
    sock->type = 0;
    sock->owner_pid = 0;
}

static err_t tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)tpcb; (void)err;
    network_socket_t *sock = (network_socket_t *)arg;
    if (!sock) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }
    if (p == NULL) {
        sock->closed = 1;
        return ERR_OK;
    }
    if (sock->recv_queue == NULL) {
        sock->recv_queue = p;
    } else {
        pbuf_cat(sock->recv_queue, p);
    }
    return ERR_OK;
}

static void tcp_err_callback(void *arg, err_t err) {
    network_socket_t *sock = (network_socket_t *)arg;
    if (!sock) return;
    sock->tcp_pcb = NULL;
    sock->connect_error = 1;
    sock->last_error = err != ERR_OK ? -(int)err : 1;
    sock->closed = 1;
}

static err_t tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
    (void)tpcb;
    network_socket_t *sock = (network_socket_t *)arg;
    if (!sock) return ERR_ABRT;
    if (err == ERR_OK) {
        sock->connect_done = 1;
    } else {
        sock->connect_error = 1;
        sock->last_error = -(int)err;
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
        ipv4_address_t ip = {{0, 0, 0, 0}};
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

    const ip_addr_t *dns = dns_getserver(0);
    if (ip4_addr_get_u32(ip_2_ip4(dns)) == 0) {
        ip_addr_t dns1;
        IP_ADDR4(&dns1, 1, 1, 1, 1);
        dns_setserver(0, &dns1);
        serial_write("[NET] DNS fallback: 1.1.1.1\n");
    } else {
        serial_write("[NET] DNS server from DHCP retained\n");
    }

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

    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    for (int i = 0; i < NETWORK_MAX_SOCKETS; i++) {
        if (network_sockets[i].in_use && network_sockets[i].owner_pid == my_pid) {
            if (legacy_tcp_fd == network_sockets[i].fd) legacy_tcp_fd = -1;
            network_socket_free_locked(&network_sockets[i]);
        }
    }
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

int network_socket_create(int type) {
    if (!lwip_initialized) return -1;

    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    network_socket_t *sock = network_socket_alloc(type);
    int fd = sock ? sock->fd : -1;
    spinlock_release_irqrestore(&network_lock, flags);
    return fd;
}

int network_socket_connect_start(int fd, const ipv4_address_t *ip, uint16_t port) {
    if (!lwip_initialized) return -1;

    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    network_socket_t *sock = network_socket_from_fd(fd);
    if (!sock || sock->type != NETWORK_SOCKET_TYPE_TCP) {
        spinlock_release_irqrestore(&network_lock, flags);
        return -1;
    }

    network_socket_reset_locked(sock);

    sock->tcp_pcb = tcp_new();
    if (!sock->tcp_pcb) {
        spinlock_release_irqrestore(&network_lock, flags);
        return -1;
    }

    ip4_addr_t dest_addr;
    IP4_ADDR(&dest_addr, ip->bytes[0], ip->bytes[1], ip->bytes[2], ip->bytes[3]);

    sock->connect_done = 0;
    sock->connect_error = 0;
    sock->last_error = 0;
    sock->closed = 0;
    sock->recv_queue = NULL;

    tcp_arg(sock->tcp_pcb, sock);
    tcp_recv(sock->tcp_pcb, tcp_recv_callback);
    tcp_err(sock->tcp_pcb, tcp_err_callback);
    err_t err = tcp_connect(sock->tcp_pcb, &dest_addr, port, tcp_connected_callback);
    spinlock_release_irqrestore(&network_lock, flags);

    if (err != ERR_OK) return -1;
    network_process_frames();
    return 0;
}

int network_socket_connect(int fd, const ipv4_address_t *ip, uint16_t port) {
    if (network_socket_connect_start(fd, ip, port) != 0) return -1;

    uint32_t start = sys_now();
    asm volatile("sti");
    while (sys_now() - start < 3000) {
        network_process_frames();
        uint64_t flags = spinlock_acquire_irqsave(&network_lock);
        network_socket_t *sock = network_socket_from_fd(fd);
        if (!sock) {
            spinlock_release_irqrestore(&network_lock, flags);
            return -1;
        }
        if (sock->connect_done) {
            spinlock_release_irqrestore(&network_lock, flags);
            return 0;
        }
        if (sock->connect_error) {
            spinlock_release_irqrestore(&network_lock, flags);
            return -1;
        }
        spinlock_release_irqrestore(&network_lock, flags);
        k_delay(2);
    }
    return -1;
}

int network_socket_send(int fd, const void *data, size_t len) {
    if (!lwip_initialized) return -1;
    if (!data || len == 0) return 0;

    network_process_frames();

    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    network_socket_t *sock = network_socket_from_fd(fd);
    if (!sock || !sock->tcp_pcb) {
        spinlock_release_irqrestore(&network_lock, flags);
        return -1;
    }

    size_t writable = tcp_sndbuf(sock->tcp_pcb);
    if (writable == 0) {
        spinlock_release_irqrestore(&network_lock, flags);
        network_process_frames();
        return 0;
    }

    if (writable > len) writable = len;
    if (writable > 4096) writable = 4096;

    err_t err = tcp_write(sock->tcp_pcb, data, (u16_t)writable, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        spinlock_release_irqrestore(&network_lock, flags);
        network_process_frames();
        if (err == ERR_MEM) return 0;
        return -1;
    }
    tcp_output(sock->tcp_pcb);
    spinlock_release_irqrestore(&network_lock, flags);
    network_process_frames();
    return (int)writable;
}

int network_socket_recv_nb(int fd, void *buf, size_t max_len) {
    if (!lwip_initialized) return -1;
    network_process_frames();

    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    network_socket_t *sock = network_socket_from_fd(fd);
    if (!sock || !sock->tcp_pcb) {
        spinlock_release_irqrestore(&network_lock, flags);
        return -1;
    }
    if (!sock->recv_queue) {
        int ret = sock->closed ? -2 : 0;
        spinlock_release_irqrestore(&network_lock, flags);
        return ret;
    }

    size_t to_copy = max_len;
    if (to_copy > sock->recv_queue->tot_len) to_copy = sock->recv_queue->tot_len;
    if (to_copy > 0xFFFF) to_copy = 0xFFFF;

    size_t copied = pbuf_copy_partial(sock->recv_queue, buf, (u16_t)to_copy, 0);
    struct pbuf *remainder = pbuf_free_header(sock->recv_queue, (u16_t)copied);
    if (sock->tcp_pcb) tcp_recved(sock->tcp_pcb, (u16_t)copied);
    sock->recv_queue = remainder;
    spinlock_release_irqrestore(&network_lock, flags);
    return (int)copied;
}

int network_socket_recv(int fd, void *buf, size_t max_len) {
    if (!lwip_initialized) return -1;

    uint32_t start = sys_now();
    asm volatile("sti");
    while (1) {
        int ret = network_socket_recv_nb(fd, buf, max_len);
        if (ret != 0) return ret;
        if (sys_now() - start >= 30000) return 0;
        k_delay(10);
    }
}

int network_socket_close(int fd) {
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    network_socket_t *sock = network_socket_from_fd(fd);
    if (!sock) {
        spinlock_release_irqrestore(&network_lock, flags);
        return -1;
    }
    if (legacy_tcp_fd == fd) legacy_tcp_fd = -1;
    network_socket_free_locked(sock);
    spinlock_release_irqrestore(&network_lock, flags);
    return 0;
}

int network_socket_poll(int fd) {
    if (!lwip_initialized) return -1;
    network_process_frames();

    int mask = 0;
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    network_socket_t *sock = network_socket_from_fd(fd);
    if (!sock) {
        spinlock_release_irqrestore(&network_lock, flags);
        return -1;
    }
    if (sock->recv_queue || sock->closed || sock->connect_error) mask |= 1;
    if (sock->tcp_pcb && sock->connect_done && !sock->closed && !sock->connect_error) mask |= 2;
    if (sock->connect_error) mask |= 4;
    spinlock_release_irqrestore(&network_lock, flags);
    return mask;
}

int network_socket_error(int fd) {
    if (!lwip_initialized) return 1;
    network_process_frames();

    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    network_socket_t *sock = network_socket_from_fd(fd);
    if (!sock) {
        spinlock_release_irqrestore(&network_lock, flags);
        return 1;
    }
    int error = sock->connect_error ? (sock->last_error != 0 ? sock->last_error : 1) : 0;
    spinlock_release_irqrestore(&network_lock, flags);
    return error;
}

int network_tcp_connect(const ipv4_address_t *ip, uint16_t port) {
    if (legacy_tcp_fd >= 0) network_socket_close(legacy_tcp_fd);
    legacy_tcp_fd = network_socket_create(NETWORK_SOCKET_TYPE_TCP);
    if (legacy_tcp_fd < 0) return -1;
    if (network_socket_connect(legacy_tcp_fd, ip, port) != 0) {
        network_socket_close(legacy_tcp_fd);
        legacy_tcp_fd = -1;
        return -1;
    }
    return 0;
}

int network_tcp_send(const void *data, size_t len) {
    if (legacy_tcp_fd < 0) return -1;
    return network_socket_send(legacy_tcp_fd, data, len);
}

int network_tcp_recv(void *buf, size_t max_len) {
    if (legacy_tcp_fd < 0) return -1;
    return network_socket_recv(legacy_tcp_fd, buf, max_len);
}

int network_tcp_recv_nb(void *buf, size_t max_len) {
    if (legacy_tcp_fd < 0) return -1;
    return network_socket_recv_nb(legacy_tcp_fd, buf, max_len);
}

int network_tcp_close(void) {
    if (legacy_tcp_fd < 0) return 0;
    int ret = network_socket_close(legacy_tcp_fd);
    legacy_tcp_fd = -1;
    return ret;
}

static ip_addr_t dns_resolved_ip;
static int dns_done = 0;
static uintptr_t dns_active_token = 0;
static uintptr_t dns_next_token = 1;
static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    (void)name;
    uintptr_t token = (uintptr_t)callback_arg;
    if (token == 0 || token != dns_active_token) {
        serial_write("[DNS] Stale callback ignored\n");
        return;
    }
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
    uintptr_t token = dns_next_token++;
    if (dns_next_token == 0) dns_next_token = 1;
    dns_active_token = token;
    uint64_t flags = spinlock_acquire_irqsave(&network_lock);
    err_t err = dns_gethostbyname(name, &dns_resolved_ip, dns_callback, (void *)token);
    spinlock_release_irqrestore(&network_lock, flags);

    if (err == ERR_OK) {
        dns_active_token = 0;
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
        while (sys_now() - start < 8000) {
            network_process_frames();
            flags = spinlock_acquire_irqsave(&network_lock);
            if (dns_done == 1) {
                u32_t addr = ip4_addr_get_u32(ip_2_ip4(&dns_resolved_ip));
                out_ip->bytes[0] = (addr >> 0) & 0xFF;
                out_ip->bytes[1] = (addr >> 8) & 0xFF;
                out_ip->bytes[2] = (addr >> 16) & 0xFF;
                out_ip->bytes[3] = (addr >> 24) & 0xFF;
                dns_active_token = 0;
                spinlock_release_irqrestore(&network_lock, flags);
                return 0;
            }
            if (dns_done == -1) { 
                dns_active_token = 0;
                spinlock_release_irqrestore(&network_lock, flags);
                return -1; 
            }
            spinlock_release_irqrestore(&network_lock, flags);
            k_delay(2);
        }
    }
    dns_active_token = 0;
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

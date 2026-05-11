// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdlib.h>
#include <syscall.h>

static int term_cols = 116;
static int term_rows = 41;

#define IAC   255
#define DONT  254
#define DO    253
#define WONT  252
#define WILL  251
#define SB    250   // sub-negotiation begin
#define SE    240   // sub-negotiation end
#define GA    249
#define EL    248
#define EC    247
#define AYT   246
#define AO    245
#define IP    244
#define BRK   243
#define DM    242
#define NOP   241

// Telnet options
#define OPT_ECHO           1
#define OPT_SUPPRESS_GA    3
#define OPT_STATUS         5
#define OPT_TIMING_MARK    6
#define OPT_NAWS           31   // Negotiate About Window Size
#define OPT_NEW_ENVIRON    39
#define OPT_TERMINAL_TYPE  24

// ─── IAC send helpers ────────────────────────────────────────────────────────

static void telnet_send(const uint8_t *data, int len) {
    sys_tcp_send(data, len);
}

static void telnet_send_3(uint8_t a, uint8_t b, uint8_t c) {
    uint8_t buf[3] = { a, b, c };
    telnet_send(buf, 3);
}

// Send NAWS subnegotiation with current terminal dimensions
static void telnet_send_naws(void) {
    uint8_t buf[9];
    buf[0] = IAC;
    buf[1] = SB;
    buf[2] = OPT_NAWS;
    buf[3] = (uint8_t)((term_cols >> 8) & 0xFF);
    buf[4] = (uint8_t)(term_cols & 0xFF);
    buf[5] = (uint8_t)((term_rows >> 8) & 0xFF);
    buf[6] = (uint8_t)(term_rows & 0xFF);
    buf[7] = IAC;
    buf[8] = SE;
    telnet_send(buf, 9);
}

static void telnet_handle_option(uint8_t cmd, uint8_t opt) {
    switch (cmd) {
        case DO:
            if (opt == OPT_NAWS) {
                telnet_send_3(IAC, WILL, OPT_NAWS);
                telnet_send_naws();
            } else if (opt == OPT_TERMINAL_TYPE) {
                telnet_send_3(IAC, WILL, OPT_TERMINAL_TYPE);
            } else {
                telnet_send_3(IAC, WONT, opt);
            }
            break;

        case DONT:
            telnet_send_3(IAC, WONT, opt);
            break;

        case WILL:
            if (opt == OPT_SUPPRESS_GA) {
                telnet_send_3(IAC, DO, OPT_SUPPRESS_GA);
            } else if (opt == OPT_ECHO) {
                telnet_send_3(IAC, DO, OPT_ECHO);
            } else {
                telnet_send_3(IAC, DONT, opt);
            }
            break;

        case WONT:
            telnet_send_3(IAC, DONT, opt);
            break;

        default:
            break;
    }
}

static void telnet_handle_sb_terminal_type(const uint8_t *sb_data, int sb_len) {
    if (sb_len < 1 || sb_data[0] != 1) return; // 1 = SEND
    uint8_t reply[12];
    int i = 0;
    reply[i++] = IAC;
    reply[i++] = SB;
    reply[i++] = OPT_TERMINAL_TYPE;
    reply[i++] = 0; // IS
    reply[i++] = 'A'; reply[i++] = 'N'; reply[i++] = 'S'; reply[i++] = 'I';
    reply[i++] = IAC;
    reply[i++] = SE;
    telnet_send(reply, i);
}


typedef enum {
    TS_DATA = 0,
    TS_IAC,
    TS_CMD,
    TS_OPT,
    TS_SB,
    TS_SB_IAC
} TelnetParseState;

static TelnetParseState ts_state = TS_DATA;
static uint8_t ts_cmd = 0;
static uint8_t ts_sb_opt = 0;
static uint8_t ts_sb_buf[256];
static int ts_sb_pos = 0;

// Output buffer — accumulate non-IAC bytes to write in bulk
static char out_buf[4096];
static int out_pos = 0;

static void flush_out(void) {
    if (out_pos > 0) {
        sys_write(1, out_buf, out_pos);
        out_pos = 0;
    }
}

static void out_char(char c) {
    if (out_pos >= (int)(sizeof(out_buf) - 1)) {
        flush_out();
    }
    out_buf[out_pos++] = c;
}

// Process a chunk of raw TCP data from server. Returns false if connection lost.
static int telnet_process(const uint8_t *data, int len) {
    for (int i = 0; i < len; i++) {
        uint8_t b = data[i];

        switch (ts_state) {
            case TS_DATA:
                if (b == IAC) {
                    ts_state = TS_IAC;
                } else {
                    out_char((char)b);
                }
                break;

            case TS_IAC:
                switch (b) {
                    case IAC:
                        out_char((char)0xFF);
                        ts_state = TS_DATA;
                        break;
                    case DO: case DONT: case WILL: case WONT:
                        ts_cmd = b;
                        ts_state = TS_OPT;
                        break;
                    case SB:
                        ts_sb_pos = 0;
                        ts_state = TS_CMD;
                        break;
                    case GA: case NOP: case DM: case BRK: case IP:
                    case AO: case AYT: case EC: case EL:
                        ts_state = TS_DATA;
                        break;
                    default:
                        ts_state = TS_DATA;
                        break;
                }
                break;

            case TS_CMD:
                ts_sb_opt = b;
                ts_sb_pos = 0;
                ts_state = TS_SB;
                break;

            case TS_OPT:
                flush_out();
                telnet_handle_option(ts_cmd, b);
                ts_state = TS_DATA;
                break;

            case TS_SB:
                if (b == IAC) {
                    ts_state = TS_SB_IAC;
                } else {
                    if (ts_sb_pos < (int)sizeof(ts_sb_buf) - 1) {
                        ts_sb_buf[ts_sb_pos++] = b;
                    }
                }
                break;

            case TS_SB_IAC:
                if (b == SE) {
                    flush_out();
                    if (ts_sb_opt == OPT_TERMINAL_TYPE) {
                        telnet_handle_sb_terminal_type(ts_sb_buf, ts_sb_pos);
                    }
                    ts_state = TS_DATA;
                } else if (b == IAC) {
                    if (ts_sb_pos < (int)sizeof(ts_sb_buf) - 1) {
                        ts_sb_buf[ts_sb_pos++] = IAC;
                    }
                    ts_state = TS_SB;
                } else {
                    ts_state = TS_DATA;
                }
                break;
        }
    }
    flush_out();
    return 1;
}

static int map_key(char c, uint8_t *key_out) {
    if (c == 29) {
        // Ctrl+]
        return -1;
    }
    if (c == 17) {
        // UP arrow
        key_out[0] = 0x1b; key_out[1] = '['; key_out[2] = 'A';
        return 3;
    }
    if (c == 18) {
        // DOWN arrow
        key_out[0] = 0x1b; key_out[1] = '['; key_out[2] = 'B';
        return 3;
    }
    if (c == 20) {
        // RIGHT arrow
        key_out[0] = 0x1b; key_out[1] = '['; key_out[2] = 'C';
        return 3;
    }
    if (c == 19) {
        // LEFT arrow
        key_out[0] = 0x1b; key_out[1] = '['; key_out[2] = 'D';
        return 3;
    }
    if (c == '\n') {
        // Enter
        key_out[0] = '\r'; key_out[1] = '\n';
        return 2;
    }
    if (c == '\b') {
        // Backspace
        key_out[0] = '\x7f';
        return 1;
    }
    // Normal printable character
    key_out[0] = (uint8_t)c;
    return 1;
}

static int my_atoi(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

static int parse_ip(const char *s, net_ipv4_address_t *ip) {
    int part = 0, val = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            if (val > 255) return -1;
        } else if (*s == '.') {
            if (part > 3) return -1;
            ip->bytes[part++] = (uint8_t)val;
            val = 0;
        } else {
            return -1;
        }
        s++;
    }
    if (part != 3) return -1;
    ip->bytes[3] = (uint8_t)val;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: telnet <host> [port]\n");
        printf("  Connect to a Telnet BBS or server.\n");
        printf("  Default port: 23\n");
        printf("  Press Ctrl+] to disconnect.\n");
        return 1;
    }

    const char *host = argv[1];
    int port = (argc >= 3) ? my_atoi(argv[2]) : 23;
    if (port <= 0 || port > 65535) port = 23;

    if (!sys_network_is_initialized()) {
        printf("Initializing network...\n");
        sys_network_init();
    }
    if (!sys_network_has_ip()) {
        printf("Acquiring DHCP...\n");
        if (sys_network_dhcp_acquire() != 0) {
            printf("DHCP failed.\n");
            return 1;
        }
    }

    net_ipv4_address_t ip;
    if (parse_ip(host, &ip) != 0) {
        printf("Resolving %s...\n", host);
        if (sys_dns_lookup(host, &ip) != 0) {
            printf("Failed to resolve: %s\n", host);
            return 1;
        }
    }

    printf("Connecting to %s:%d...\n", host, port);
    if (sys_tcp_connect(&ip, (uint16_t)port) != 0) {
        printf("Connection failed.\n");
        return 1;
    }
    printf("Connected. Press Ctrl+] to disconnect.\n\n");

    uint8_t recv_buf[4096];
    int total = 0;
    int idle_count = 0;
    int connected = 1;

    while (connected) {
        char ch = 0;
        int got = sys_tty_read_in(&ch, 1);
        if (got > 0) {
            uint8_t key_data[16];
            int key_len = map_key(ch, key_data);
            if (key_len < 0) {
                connected = 0;
                break;
            }
            telnet_send(key_data, key_len);
        }

        int len = sys_tcp_recv_nb(recv_buf, sizeof(recv_buf) - 1);
        if (len < 0) {
            printf("\r\n[Connection error]\r\n");
            connected = 0;
            break;
        }
        if (len == 0) {
            idle_count++;
            if (idle_count > 10000000) {
                printf("\r\n[Connection timed out]\r\n");
                connected = 0;
                break;
            }
            sys_system(SYSTEM_CMD_SLEEP, 10, 0, 0, 0);
            continue;
        }

        idle_count = 0;
        total += len;

        if (total > 10000000) {
            printf("\r\n[Data limit reached]\r\n");
            connected = 0;
            break;
        }

        if (!telnet_process(recv_buf, len)) {
            connected = 0;
        }
    }

    sys_tcp_close();
    printf("\r\n[Telnet session ended]\r\n");
    return 0;
}

#include "novaproto.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

int nova_connect(const char *socket_path) {
    const char *path = socket_path;
    if (!path) {
        path = getenv("NOVA_SOCKET");
    }
    if (!path) {
        path = "/tmp/nova.sock";
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_all(int fd, const void *buf, size_t size) {
    size_t written = 0;
    while (written < size) {
        ssize_t rc = send(fd, (const char *)buf + written, size - written, 0);
        if (rc <= 0) return -1;
        written += rc;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t size) {
    size_t read_bytes = 0;
    while (read_bytes < size) {
        ssize_t rc = recv(fd, (char *)buf + read_bytes, size - read_bytes, 0);
        if (rc <= 0) return -1;
        read_bytes += rc;
    }
    return 0;
}

static int send_frame(int fd, uint32_t type, const void *payload, uint32_t size) {
    NovaFrameHeader header;
    header.magic = NOVA_MAGIC;
    header.version = NOVA_VERSION;
    header.flags = 0;
    header.msg_type = type;
    header.payload_size = size;

    if (send_all(fd, &header, sizeof(header)) < 0) {
        return -1;
    }

    if (size > 0 && payload) {
        if (send_all(fd, payload, size) < 0) {
            return -1;
        }
    }
    return 0;
}

static int recv_frame(int fd, NovaFrameHeader *header_out, void *payload_out, uint32_t max_size) {
    if (recv_all(fd, header_out, sizeof(NovaFrameHeader)) < 0) {
        return -1;
    }

    if (header_out->magic != NOVA_MAGIC) {
        return -1;
    }

    if (header_out->payload_size > 0) {
        uint32_t to_read = header_out->payload_size;
        if (to_read > max_size) to_read = max_size;

        if (recv_all(fd, payload_out, to_read) < 0) {
            return -1;
        }
    }
    return 0;
}

#define MAX_QUEUED_EVENTS 64
static NovaEvent event_queue[MAX_QUEUED_EVENTS];
static int event_queue_head = 0;
static int event_queue_tail = 0;

static void push_event(const NovaEvent *ev) {
    int next = (event_queue_tail + 1) % MAX_QUEUED_EVENTS;
    if (next != event_queue_head) {
        event_queue[event_queue_tail] = *ev;
        event_queue_tail = next;
    }
}

static bool pop_event(NovaEvent *ev) {
    if (event_queue_head == event_queue_tail) return false;
    *ev = event_queue[event_queue_head];
    event_queue_head = (event_queue_head + 1) % MAX_QUEUED_EVENTS;
    return true;
}

static void parse_event_from_frame(uint32_t msg_type, const uint8_t *buffer, NovaEvent *event_out) {
    event_out->type = msg_type;
    
    switch (msg_type) {
        case EVT_KEY: {
            struct {
                uint32_t surface_id;
                uint32_t keycode;
                uint32_t modifiers;
                uint8_t pressed;
            } __attribute__((packed)) *p = (void *)buffer;
            event_out->surface_id = p->surface_id;
            event_out->data.key.keycode = p->keycode;
            event_out->data.key.modifiers = p->modifiers;
            event_out->data.key.pressed = p->pressed;
            break;
        }
        case EVT_POINTER: {
            struct {
                uint32_t surface_id;
                int x, y;
                uint32_t buttons;
            } __attribute__((packed)) *p = (void *)buffer;
            event_out->surface_id = p->surface_id;
            event_out->data.pointer.x = p->x;
            event_out->data.pointer.y = p->y;
            event_out->data.pointer.buttons = p->buttons;
            break;
        }
        case EVT_RESIZE_REQUEST: {
            struct {
                uint32_t surface_id;
                uint32_t w, h;
            } __attribute__((packed)) *p = (void *)buffer;
            event_out->surface_id = p->surface_id;
            event_out->data.resize.w = p->w;
            event_out->data.resize.h = p->h;
            break;
        }
        case EVT_CLOSE_REQUEST:
        case EVT_FOCUS_IN:
        case EVT_FOCUS_OUT:
        case EVT_WINDOW_DESTROYED:
        case EVT_WINDOW_LIST_END: {
            uint32_t *p = (void *)buffer;
            event_out->surface_id = *p;
            break;
        }
        case EVT_WINDOW_CREATED:
        case EVT_WINDOW_TITLE_CHANGED: {
            struct {
                uint32_t surface_id;
                char title[128];
                uint32_t state_flags;
                char icon_path[256];
            } __attribute__((packed)) *p = (void *)buffer;
            event_out->surface_id = p->surface_id;
            strncpy(event_out->data.window.title, p->title, 127);
            event_out->data.window.title[127] = '\0';
            event_out->data.window.state_flags = p->state_flags;
            strncpy(event_out->data.window.icon_path, p->icon_path, 255);
            event_out->data.window.icon_path[255] = '\0';
            break;
        }
        case EVT_STATE_CHANGED: {
            struct {
                uint32_t surface_id;
                uint32_t state_flags;
            } __attribute__((packed)) *p = (void *)buffer;
            event_out->surface_id = p->surface_id;
            event_out->data.state.state_flags = p->state_flags;
            break;
        }
        case EVT_THEME_UPDATE: {
            // Re-broadcast theme update inside process
            break;
        }
        default:
            break;
    }
}

static int recv_sync_reply(int fd, uint32_t expected_type, void *payload_out, uint32_t max_size) {
    while (1) {
        NovaFrameHeader header;
        uint8_t buffer[512];
        if (recv_frame(fd, &header, buffer, sizeof(buffer)) < 0) {
            return -1;
        }

        if (header.msg_type == expected_type) {
            if (header.payload_size > 0 && payload_out) {
                uint32_t copy_sz = header.payload_size;
                if (copy_sz > max_size) copy_sz = max_size;
                memcpy(payload_out, buffer, copy_sz);
            }
            return 0;
        }

        // Parse and queue the out-of-order event
        NovaEvent ev;
        parse_event_from_frame(header.msg_type, buffer, &ev);
        push_event(&ev);
    }
}

int nova_create_surface(int fd, uint32_t w, uint32_t h, uint8_t layer, uint32_t flags, uint32_t *surf_id_out, char *shm_path_out) {
    struct {
        uint32_t w, h;
        uint8_t layer;
        uint32_t flags;
    } __attribute__((packed)) payload = {w, h, layer, flags};

    if (send_frame(fd, MSG_CREATE_SURFACE, &payload, sizeof(payload)) < 0) {
        return -1;
    }

    struct {
        uint32_t surface_id;
        char shm_path[108];
    } __attribute__((packed)) reply;

    if (recv_sync_reply(fd, MSG_CREATE_SURFACE, &reply, sizeof(reply)) < 0) {
        return -1;
    }

    if (surf_id_out) *surf_id_out = reply.surface_id;
    if (shm_path_out) strcpy(shm_path_out, reply.shm_path);
    return 0;
}

int nova_resize_surface(int fd, uint32_t surf_id, uint32_t w, uint32_t h, char *new_shm_path_out) {
    struct {
        uint32_t surface_id;
        uint32_t w, h;
    } __attribute__((packed)) payload = {surf_id, w, h};

    if (send_frame(fd, MSG_RESIZE_SURFACE, &payload, sizeof(payload)) < 0) {
        return -1;
    }

    struct {
        char shm_path[108];
    } __attribute__((packed)) reply;

    if (recv_sync_reply(fd, MSG_RESIZE_SURFACE, &reply, sizeof(reply)) < 0) {
        return -1;
    }

    if (new_shm_path_out) strcpy(new_shm_path_out, reply.shm_path);
    return 0;
}

int nova_damage_surface(int fd, uint32_t surf_id, int rect_count, const NovaRect *rects) {
    uint32_t header_size = 4 + 4;
    uint32_t payload_size = header_size + rect_count * sizeof(NovaRect);
    uint8_t *buffer = (uint8_t *)malloc(payload_size);
    if (!buffer) return -1;

    *(uint32_t *)(buffer) = surf_id;
    *(uint32_t *)(buffer + 4) = (uint32_t)rect_count;
    if (rect_count > 0 && rects) {
        memcpy(buffer + header_size, rects, rect_count * sizeof(NovaRect));
    }

    int rc = send_frame(fd, MSG_DAMAGE, buffer, payload_size);
    free(buffer);
    return rc;
}

int nova_move_surface(int fd, uint32_t surf_id, int x, int y) {
    struct {
        uint32_t surface_id;
        int x, y;
    } __attribute__((packed)) payload = {surf_id, x, y};

    return send_frame(fd, MSG_MOVE_SURFACE, &payload, sizeof(payload));
}

int nova_set_state(int fd, uint32_t surf_id, uint32_t state_flags) {
    struct {
        uint32_t surface_id;
        uint32_t state_flags;
    } __attribute__((packed)) payload = {surf_id, state_flags};

    return send_frame(fd, MSG_SET_STATE, &payload, sizeof(payload));
}

int nova_set_flags(int fd, uint32_t surf_id, uint32_t flags) {
    struct {
        uint32_t surface_id;
        uint32_t flags;
    } __attribute__((packed)) payload = {surf_id, flags};

    return send_frame(fd, MSG_SET_FLAGS, &payload, sizeof(payload));
}

int nova_set_icon(int fd, uint32_t surf_id, const char *icon_path) {
    struct {
        uint32_t surface_id;
        char icon_path[256];
    } __attribute__((packed)) payload;
    
    payload.surface_id = surf_id;
    memset(payload.icon_path, 0, 256);
    if (icon_path) {
        strncpy(payload.icon_path, icon_path, 255);
    }

    return send_frame(fd, MSG_SET_ICON, &payload, sizeof(payload));
}

int nova_set_title(int fd, uint32_t surf_id, const char *title) {
    struct {
        uint32_t surface_id;
        char title[128];
    } __attribute__((packed)) payload;
    
    payload.surface_id = surf_id;
    memset(payload.title, 0, 128);
    if (title) {
        strncpy(payload.title, title, 127);
    }

    return send_frame(fd, MSG_SET_TITLE, &payload, sizeof(payload));
}

int nova_destroy_surface(int fd, uint32_t surf_id) {
    uint32_t payload = surf_id;
    return send_frame(fd, MSG_DESTROY_SURFACE, &payload, sizeof(payload));
}

int nova_query_windows(int fd) {
    return send_frame(fd, MSG_QUERY_WINDOWS, NULL, 0);
}

int nova_quit(int fd) {
    return send_frame(fd, MSG_QUIT, NULL, 0);
}

int nova_poll_event(int fd, NovaEvent *event_out) {
    if (!event_out) return -1;

    if (pop_event(event_out)) {
        return 0;
    }

    NovaFrameHeader header;
    uint8_t buffer[512];
    if (recv_frame(fd, &header, buffer, sizeof(buffer)) < 0) {
        return -1;
    }

    parse_event_from_frame(header.msg_type, buffer, event_out);
    return 0;
}

int nova_pending_events(void) {
    return event_queue_head != event_queue_tail;
}


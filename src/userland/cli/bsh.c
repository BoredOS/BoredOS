// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <syscall.h>
#include <stdbool.h>
#include <unistd.h>
#include "utf-8.h"

#define MAX_LINE 512
#define MAX_ARGS 32
#define MAX_PATHS 16
#define MAX_PATH_LEN 128
#define MAX_HISTORY 256
#define MAX_MATCHES 64
#define MAX_MATCH_LEN 128
#define MAX_ALIASES 32
#define MAX_ALIAS_NAME 32
#define MAX_ALIAS_VALUE 256

#define DEFAULT_PROMPT "%n@%h:%~$ "

typedef struct {
    char path[256];
    char startup[256];
    char boot_script[256];
    char prompt_left[128];
    char prompt_right[128];
    char prompt_minimal_prefix[64];
    char history_file[128];
    int history_size;
    bool prompt_minimal_history;
    bool glob_enabled;
    bool complete_enabled;
    bool suggest_enabled;
} bsh_config_t;

static bsh_config_t g_cfg;
static char g_paths[MAX_PATHS][MAX_PATH_LEN];
static int g_path_count = 0;
static char g_resolved_command_path[256];
static int g_resolve_status = -1;

static char g_history[MAX_HISTORY][MAX_LINE];
static int g_history_count = 0;

typedef struct {
    char name[MAX_ALIAS_NAME];
    char value[MAX_ALIAS_VALUE];
} alias_t;

static alias_t g_aliases[MAX_ALIASES];
static int g_alias_count = 0;

static int g_tty_id = -1;
static uint32_t g_color_dir = 0;
static uint32_t g_color_file = 0;
static uint32_t g_color_size = 0;
static uint32_t g_color_error = 0;
static uint32_t g_color_success = 0;
static uint32_t g_color_default = 0;

static bool str_eq(const char *a, const char *b);
static void get_time_string(char *out, int max_len);
static int delete_recursive(const char *path);

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool parse_hex_color(const char *s, int len, uint32_t *out) {
    if (!s || len <= 0 || !out) return false;
    if (len == 7 && s[0] == '#') {
        uint32_t rgb = 0;
        for (int i = 1; i < 7; i++) {
            int d = hex_digit(s[i]);
            if (d < 0) return false;
            rgb = (rgb << 4) | (uint32_t)d;
        }
        *out = 0xFF000000 | rgb;
        return true;
    }
    if (len == 8 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        uint32_t rgb = 0;
        for (int i = 2; i < 8; i++) {
            int d = hex_digit(s[i]);
            if (d < 0) return false;
            rgb = (rgb << 4) | (uint32_t)d;
        }
        *out = 0xFF000000 | rgb;
        return true;
    }
    if (len == 10 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        uint32_t argb = 0;
        for (int i = 2; i < 10; i++) {
            int d = hex_digit(s[i]);
            if (d < 0) return false;
            argb = (argb << 4) | (uint32_t)d;
        }
        *out = argb;
        return true;
    }
    return false;
}

static bool parse_named_color(const char *s, uint32_t *out) {
    if (!s || !out) return false;
    if (str_eq(s, "default")) {
        *out = g_color_default;
        return true;
    }
    if (str_eq(s, "black")) { *out = 0xFF000000; return true; }
    if (str_eq(s, "red")) { *out = 0xFFFF4444; return true; }
    if (str_eq(s, "green")) { *out = 0xFF6A9955; return true; }
    if (str_eq(s, "yellow")) { *out = 0xFFFFCC00; return true; }
    if (str_eq(s, "blue")) { *out = 0xFF569CD6; return true; }
    if (str_eq(s, "magenta")) { *out = 0xFFC586C0; return true; }
    if (str_eq(s, "cyan")) { *out = 0xFF4EC9B0; return true; }
    if (str_eq(s, "white")) { *out = 0xFFFFFFFF; return true; }
    if (str_eq(s, "gray")) { *out = 0xFFCCCCCC; return true; }
    return false;
}

static void str_copy(char *dst, const char *src, int max_len) {
    int i = 0;
    if (max_len <= 0) return;
    while (i < max_len - 1 && src && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void str_append(char *dst, const char *src, int max_len) {
    if (!dst || !src || max_len <= 0) return;
    int dlen = (int)strlen(dst);
    int i = 0;
    while (dlen + i < max_len - 1 && src[i]) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = 0;
}

static bool str_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static bool starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    while (*prefix) {
        if (*s != *prefix) return false;
        s++;
        prefix++;
    }
    return true;
}

static bool ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    int sl = (int)strlen(s);
    int tl = (int)strlen(suffix);
    if (tl > sl) return false;
    return strcmp(s + sl - tl, suffix) == 0;
}

static void trim(char *s) {
    if (!s) return;
    int len = (int)strlen(s);
    int start = 0;
    while (s[start] == ' ' || s[start] == '\t') start++;
    int end = len - 1;
    while (end >= start && (s[end] == ' ' || s[end] == '\t' || s[end] == '\r' || s[end] == '\n')) end--;

    int out = 0;
    for (int i = start; i <= end; i++) {
        s[out++] = s[i];
    }
    s[out] = 0;
}

static void strip_quotes(char *s) {
    if (!s) return;
    int len = (int)strlen(s);
    if (len >= 2 && ((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\''))) {
        for (int i = 1; i < len - 1; i++) {
            s[i - 1] = s[i];
        }
        s[len - 2] = 0;
    }
}

static void expand_path_value(const char *val, char *out, int max_len) {
    if (!out || max_len <= 0) return;
    out[0] = 0;
    if (!val) return;

    const char *needle1 = "$PATH";
    const char *needle2 = "${PATH}";
    int i = 0;
    while (val[i] && (int)strlen(out) < max_len - 1) {
        if (starts_with(&val[i], needle1)) {
            str_append(out, g_cfg.path, max_len);
            i += (int)strlen(needle1);
            continue;
        }
        if (starts_with(&val[i], needle2)) {
            str_append(out, g_cfg.path, max_len);
            i += (int)strlen(needle2);
            continue;
        }
        char ch[2] = { val[i], 0 };
        str_append(out, ch, max_len);
        i++;
    }
}

static void alias_add(const char *name, const char *value) {
    if (!name || !name[0] || !value) return;
    for (int i = 0; i < g_alias_count; i++) {
        if (str_eq(g_aliases[i].name, name)) {
            str_copy(g_aliases[i].value, value, sizeof(g_aliases[i].value));
            return;
        }
    }
    if (g_alias_count >= MAX_ALIASES) return;
    str_copy(g_aliases[g_alias_count].name, name, sizeof(g_aliases[g_alias_count].name));
    str_copy(g_aliases[g_alias_count].value, value, sizeof(g_aliases[g_alias_count].value));
    g_alias_count++;
}

static void alias_remove(const char *name) {
    if (!name || !name[0]) return;
    for (int i = 0; i < g_alias_count; i++) {
        if (str_eq(g_aliases[i].name, name)) {
            for (int j = i; j < g_alias_count - 1; j++) {
                g_aliases[j] = g_aliases[j + 1];
            }
            g_alias_count--;
            return;
        }
    }
}

static const char *alias_get(const char *name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < g_alias_count; i++) {
        if (str_eq(g_aliases[i].name, name)) return g_aliases[i].value;
    }
    return NULL;
}

static void config_defaults(void) {
    str_copy(g_cfg.path, "/bin", sizeof(g_cfg.path));
    str_copy(g_cfg.startup, "", sizeof(g_cfg.startup));
    str_copy(g_cfg.boot_script, "", sizeof(g_cfg.boot_script));
    str_copy(g_cfg.prompt_left, DEFAULT_PROMPT, sizeof(g_cfg.prompt_left));
    str_copy(g_cfg.prompt_right, "", sizeof(g_cfg.prompt_right));
    str_copy(g_cfg.prompt_minimal_prefix, "> ", sizeof(g_cfg.prompt_minimal_prefix));
    str_copy(g_cfg.history_file, "/Library/bsh/history", sizeof(g_cfg.history_file));
    g_cfg.history_size = 200;
    g_cfg.prompt_minimal_history = false;
    g_cfg.glob_enabled = true;
    g_cfg.complete_enabled = true;
    g_cfg.suggest_enabled = true;
    g_alias_count = 0;
}

static void parse_bool(const char *val, bool *out) {
    if (!val || !out) return;
    if (str_eq(val, "true") || str_eq(val, "1") || str_eq(val, "yes")) *out = true;
    else *out = false;
}

static void split_path(const char *path_str) {
    g_path_count = 0;
    if (!path_str) return;
    int i = 0;
    int start = 0;
    while (1) {
        if (path_str[i] == ':' || path_str[i] == 0) {
            int len = i - start;
            if (len > 0 && g_path_count < MAX_PATHS) {
                int copy_len = len < (MAX_PATH_LEN - 1) ? len : (MAX_PATH_LEN - 1);
                for (int j = 0; j < copy_len; j++) {
                    g_paths[g_path_count][j] = path_str[start + j];
                }
                g_paths[g_path_count][copy_len] = 0;
                g_path_count++;
            }
            start = i + 1;
        }
        if (path_str[i] == 0) break;
        i++;
    }
}

static void load_shell_colors(void) {
    g_color_dir = (uint32_t)sys_get_shell_config("dir_color");
    g_color_file = (uint32_t)sys_get_shell_config("file_color");
    g_color_size = (uint32_t)sys_get_shell_config("size_color");
    g_color_error = (uint32_t)sys_get_shell_config("error_color");
    g_color_success = (uint32_t)sys_get_shell_config("success_color");
    g_color_default = (uint32_t)sys_get_shell_config("default_text_color");

    if (g_color_default == 0) g_color_default = 0xFFCCCCCC;
    if (g_color_error == 0) g_color_error = 0xFFFF4444;
    if (g_color_success == 0) g_color_success = 0xFF6A9955;
    if (g_color_dir == 0) g_color_dir = 0xFF569CD6;
    if (g_color_file == 0) g_color_file = g_color_default;
    if (g_color_size == 0) g_color_size = 0xFF6A9955;
}

static void set_color(uint32_t color) {
    sys_set_text_color(color);
}

static void reset_color(void) {
    sys_set_text_color(g_color_default);
}

static void shell_write(const char *buf, int len) {
    if (!buf || len <= 0) return;
    write(1, buf, (size_t)len);
}

static void shell_writeln(const char *buf) {
    if (!buf) return;
    shell_write(buf, (int)strlen(buf));
    shell_write("\n", 1);
}

static void prompt_emit(const char *text, int len, char *out, int *out_idx, int max_len, bool do_write) {
    if (!text || len <= 0) return;
    if (do_write) sys_write(1, text, len);
    if (!out || max_len <= 0 || !out_idx) return;
    for (int i = 0; i < len && *out_idx < max_len - 1; i++) {
        out[(*out_idx)++] = text[i];
    }
}

static void render_prompt(const char *tmpl, char *out, int max_len, bool do_write) {
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) str_copy(cwd, "/", sizeof(cwd));

    int out_idx = 0;
    for (int i = 0; tmpl[i] && (!out || out_idx < max_len - 1); i++) {
        if (tmpl[i] == '%' && tmpl[i + 1]) {
            if (tmpl[i + 1] == '{') {
                int j = i + 2;
                while (tmpl[j] && tmpl[j] != '}') j++;
                if (tmpl[j] == '}') {
                    char color_buf[32];
                    int len = j - (i + 2);
                    if (len > (int)sizeof(color_buf) - 1) len = (int)sizeof(color_buf) - 1;
                    for (int k = 0; k < len; k++) color_buf[k] = tmpl[i + 2 + k];
                    color_buf[len] = 0;
                    trim(color_buf);

                    uint32_t color = 0;
                    if (parse_hex_color(color_buf, (int)strlen(color_buf), &color) ||
                        parse_named_color(color_buf, &color)) {
                        if (do_write) set_color(color);
                        i = j;
                        continue;
                    }
                }
            }

            char token = tmpl[i + 1];
            if (token == '~') {
                if (starts_with(cwd, "/root") && (cwd[5] == 0 || cwd[5] == '/')) {
                    prompt_emit("~", 1, out, &out_idx, max_len, do_write);
                    int j = 5;
                    while (cwd[j]) {
                        prompt_emit(&cwd[j], 1, out, &out_idx, max_len, do_write);
                        j++;
                    }
                } else {
                    int j = 0;
                    while (cwd[j]) {
                        prompt_emit(&cwd[j], 1, out, &out_idx, max_len, do_write);
                        j++;
                    }
                }
                i++;
                continue;
            }
            if (token == 'n') {
                const char *user = "root";
                prompt_emit(user, (int)strlen(user), out, &out_idx, max_len, do_write);
                i++;
                continue;
            }
            if (token == 'h') {
                const char *host = "boredos";
                prompt_emit(host, (int)strlen(host), out, &out_idx, max_len, do_write);
                i++;
                continue;
            }
            if (token == 'T') {
                char time_buf[16];
                get_time_string(time_buf, sizeof(time_buf));
                prompt_emit(time_buf, (int)strlen(time_buf), out, &out_idx, max_len, do_write);
                i++;
                continue;
            }
        }
        prompt_emit(&tmpl[i], 1, out, &out_idx, max_len, do_write);
    }
    if (out && max_len > 0) out[out_idx] = 0;
    if (do_write) reset_color();
}

static void config_load(void) {
    config_defaults();

    int fd = sys_open("/Library/bsh/bshrc", "r");
    if (fd < 0) {
        split_path(g_cfg.path);
        return;
    }

    char buf[4096];
    int bytes = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (bytes <= 0) {
        split_path(g_cfg.path);
        return;
    }
    buf[bytes] = 0;

    char *line = buf;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n' && *end != '\r') end++;
        char saved = *end;
        *end = 0;

        trim(line);
        if (line[0] == '#' || line[0] == 0) {
            line = end + (saved ? 1 : 0);
            if (saved == '\r' && *line == '\n') line++;
            continue;
        }

        if (starts_with(line, "alias ") || starts_with(line, "alias\t")) {
            char *def = line + 5;
            while (*def == ' ' || *def == '\t') def++;
            char *eq = def;
            while (*eq && *eq != '=') eq++;
            if (*eq == '=') {
                *eq = 0;
                char *name = def;
                char *val = eq + 1;
                trim(name);
                trim(val);
                if (name[0]) alias_add(name, val);
            }
            line = end + (saved ? 1 : 0);
            if (saved == '\r' && *line == '\n') line++;
            continue;
        }

        char *assign = line;
        if (starts_with(assign, "export ") || starts_with(assign, "export\t")) {
            assign += 6;
            while (*assign == ' ' || *assign == '\t') assign++;
        }

        char *sep = assign;
        while (*sep && *sep != '=') sep++;
        if (*sep == '=') {
            *sep = 0;
            char *key = assign;
            char *val = sep + 1;
            trim(key);
            trim(val);
            strip_quotes(val);

            if (str_eq(key, "PATH")) {
                char expanded[256];
                expand_path_value(val, expanded, sizeof(expanded));
                str_copy(g_cfg.path, expanded[0] ? expanded : val, sizeof(g_cfg.path));
            }
            else if (str_eq(key, "STARTUP")) str_copy(g_cfg.startup, val, sizeof(g_cfg.startup));
            else if (str_eq(key, "BOOT_SCRIPT")) str_copy(g_cfg.boot_script, val, sizeof(g_cfg.boot_script));
            else if (str_eq(key, "PROMPT_LEFT")) str_copy(g_cfg.prompt_left, val, sizeof(g_cfg.prompt_left));
            else if (str_eq(key, "PROMPT_RIGHT")) str_copy(g_cfg.prompt_right, val, sizeof(g_cfg.prompt_right));
            else if (str_eq(key, "PROMPT_MINIMAL_HISTORY")) parse_bool(val, &g_cfg.prompt_minimal_history);
            else if (str_eq(key, "PROMPT_MINIMAL_PREFIX")) str_copy(g_cfg.prompt_minimal_prefix, val, sizeof(g_cfg.prompt_minimal_prefix));
            else if (str_eq(key, "HISTORY_FILE")) str_copy(g_cfg.history_file, val, sizeof(g_cfg.history_file));
            else if (str_eq(key, "HISTORY_SIZE")) g_cfg.history_size = atoi(val);
            else if (str_eq(key, "GLOB")) parse_bool(val, &g_cfg.glob_enabled);
            else if (str_eq(key, "COMPLETE")) parse_bool(val, &g_cfg.complete_enabled);
            else if (str_eq(key, "SUGGEST")) parse_bool(val, &g_cfg.suggest_enabled);
        }

        line = end + (saved ? 1 : 0);
        if (saved == '\r' && *line == '\n') line++;
    }

    split_path(g_cfg.path);
}

static void history_load(void) {
    g_history_count = 0;
    if (!g_cfg.history_file[0]) return;

    int fd = sys_open(g_cfg.history_file, "r");
    if (fd < 0) return;

    char buf[4096];
    int bytes = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (bytes <= 0) return;
    buf[bytes] = 0;

    char *line = buf;
    while (*line && g_history_count < MAX_HISTORY) {
        char *end = line;
        while (*end && *end != '\n' && *end != '\r') end++;
        char saved = *end;
        *end = 0;
        trim(line);
        if (line[0]) {
            str_copy(g_history[g_history_count++], line, MAX_LINE);
        }
        line = end + (saved ? 1 : 0);
        if (saved == '\r' && *line == '\n') line++;
    }
}

static void history_save(void) {
    if (!g_cfg.history_file[0]) return;

    int fd = sys_open(g_cfg.history_file, "w");
    if (fd < 0) return;

    for (int i = 0; i < g_history_count; i++) {
        int len = (int)strlen(g_history[i]);
        if (len > 0) {
            sys_write_fs(fd, g_history[i], len);
            sys_write_fs(fd, "\n", 1);
        }
    }
    sys_close(fd);
}

static void history_add(const char *line) {
    if (!line || !line[0]) return;
    if (g_history_count > 0 && str_eq(g_history[g_history_count - 1], line)) return;

    if (g_history_count < MAX_HISTORY) {
        str_copy(g_history[g_history_count++], line, MAX_LINE);
    } else {
        for (int i = 1; i < MAX_HISTORY; i++) {
            str_copy(g_history[i - 1], g_history[i], MAX_LINE);
        }
        str_copy(g_history[MAX_HISTORY - 1], line, MAX_LINE);
    }
    if (g_history_count > g_cfg.history_size && g_cfg.history_size > 0) {
        int excess = g_history_count - g_cfg.history_size;
        for (int i = excess; i < g_history_count; i++) {
            str_copy(g_history[i - excess], g_history[i], MAX_LINE);
        }
        g_history_count -= excess;
    }
    history_save();
}

static void get_time_string(char *out, int max_len) {
    int dt[6] = {0};
    sys_system(SYSTEM_CMD_RTC_GET, (uint64_t)dt, 0, 0, 0);
    char hh[4], mm[4];
    itoa(dt[3], hh);
    itoa(dt[4], mm);
    out[0] = 0;
    if (dt[3] < 10) str_copy(out, "0", max_len);
    str_append(out, hh, max_len);
    str_append(out, ":", max_len);
    if (dt[4] < 10) str_append(out, "0", max_len);
    str_append(out, mm, max_len);
}


static int split_args(char *line, char *argv[], int max_args) {
    int argc = 0;
    int i = 0;
    while (line[i] && argc < max_args) {
        while (line[i] == ' ' || line[i] == '\t') i++;
        if (!line[i]) break;

        if (line[i] == '"') {
            i++;
            argv[argc++] = &line[i];
            while (line[i] && line[i] != '"') i++;
            if (line[i]) {
                line[i] = 0;
                i++;
            }
        } else {
            argv[argc++] = &line[i];
            while (line[i] && line[i] != ' ' && line[i] != '\t') i++;
            if (line[i]) {
                line[i] = 0;
                i++;
            }
        }
    }
    return argc;
}

static bool is_file_path(const char *path) {
    FAT32_FileInfo info;
    if (sys_get_file_info(path, &info) == 0 && !info.is_directory) return true;
    return false;
}

static bool run_script(const char *path);

static void resolve_path(const char *input, char *out, int max_len) {
    if (!out || max_len <= 0) return;
    if (!input || !input[0]) {
        out[0] = 0;
        return;
    }
    if (input[0] == '/') {
        str_copy(out, input, max_len);
        return;
    }

    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) {
        str_copy(out, input, max_len);
        return;
    }

    str_copy(out, cwd, max_len);
    int len = (int)strlen(out);
    if (len > 0 && out[len - 1] != '/') str_append(out, "/", max_len);
    str_append(out, input, max_len);
}

static bool resolve_script_path(const char *input, char *out, int max_len) {
    char candidate[256];
    resolve_path(input, candidate, sizeof(candidate));
    if (is_file_path(candidate)) {
        str_copy(out, candidate, max_len);
        return true;
    }
    if (!ends_with(candidate, ".bsh")) {
        char with_ext[256];
        str_copy(with_ext, candidate, sizeof(with_ext));
        str_append(with_ext, ".bsh", sizeof(with_ext));
        if (is_file_path(with_ext)) {
            str_copy(out, with_ext, max_len);
            return true;
        }
    }
    return false;
}

static bool is_elf_file(const char *path) {
    unsigned char hdr[4];
    int fd = sys_open(path, "r");
    if (fd < 0) return false;
    int bytes = sys_read(fd, hdr, 4);
    sys_close(fd);
    if (bytes < 4) return false;
    return hdr[0] == 0x7F && hdr[1] == 'E' && hdr[2] == 'L' && hdr[3] == 'F';
}

static bool contains_slash(const char *cmd) {
    if (!cmd) return false;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '/') return true;
    }
    return false;
}

static const char *env_get_value(char *const envp[], const char *name) {
    int name_len;

    if (!envp || !name || !name[0]) return NULL;

    name_len = (int)strlen(name);

    for (int i = 0; envp[i]; i++) {
        if (starts_with(envp[i], name) && envp[i][name_len] == '=') {
            return envp[i] + name_len + 1;
        }
    }

    return NULL;
}

static void build_path_candidate(char *out, int out_len, const char *dir, int dir_len, const char *cmd) {
    int pos = 0;

    if (!out || out_len <= 0) return;
    out[0] = 0;
    if (!dir || !cmd) return;

    for (int i = 0; i < dir_len && dir[i] && pos < out_len - 1; i++) {
        out[pos++] = dir[i];
    }
    out[pos] = 0;

    if (pos > 0 && out[pos - 1] != '/' && pos < out_len - 1) {
        out[pos++] = '/';
        out[pos] = 0;
    }

    for (int i = 0; cmd[i] && pos < out_len - 1; i++) {
        out[pos++] = cmd[i];
    }
    out[pos] = 0;
}

static int accept_command_candidate(const char *candidate) {
    if (access(candidate, X_OK) != 0) return -1;
    if (!is_file_path(candidate)) return -2;
    if (!is_elf_file(candidate)) return -3;

    str_copy(g_resolved_command_path, candidate, sizeof(g_resolved_command_path));
    return 0;
}

static char *resolve_in_path_string(const char *cmd, const char *path_str) {
    int first_error = -1;
    int start = 0;
    int i = 0;

    if (!path_str || !path_str[0]) {
        g_resolve_status = -1;
        return NULL;
    }

    while (1) {
        if (path_str[i] == ':' || path_str[i] == 0) {
            int len = i - start;

            if (len > 0) {
                char candidate[256];
                int res;

                build_path_candidate(candidate, sizeof(candidate), path_str + start, len, cmd);
                res = accept_command_candidate(candidate);
                if (res == 0) {
                    g_resolve_status = 0;
                    return g_resolved_command_path;
                }
                if (res != -1 && first_error == -1) first_error = res;

                if (!ends_with(cmd, ".elf")) {
                    str_append(candidate, ".elf", sizeof(candidate));
                    res = accept_command_candidate(candidate);
                    if (res == 0) {
                        g_resolve_status = 0;
                        return g_resolved_command_path;
                    }
                    if (res != -1 && first_error == -1) first_error = res;
                }
            }

            start = i + 1;
        }

        if (path_str[i] == 0) break;
        i++;
    }

    g_resolve_status = first_error;
    return NULL;
}

static char *resolve_command_path(const char *cmd, char *const envp[]) {
    const char *path;
    int res;

    g_resolved_command_path[0] = 0;
    g_resolve_status = -1;

    if (!cmd || !cmd[0]) return NULL;

    if (contains_slash(cmd)) {
        res = accept_command_candidate(cmd);
        g_resolve_status = res;
        return (res == 0) ? g_resolved_command_path : NULL;
    }

    path = env_get_value(envp, "PATH");
    if (!path) path = g_cfg.path;

    return resolve_in_path_string(cmd, path);
}

static int resolve_command(const char *cmd, char *out, int out_len) {
    char *resolved = resolve_command_path(cmd, NULL);

    if (!resolved) return g_resolve_status;

    str_copy(out, resolved, out_len);
    return 0;
}

static void build_args_string(int argc, char *argv[], int start, char *out, int out_len) {
    int pos = 0;
    for (int i = start; i < argc; i++) {
        if (i > start && pos < out_len - 1) out[pos++] = ' ';

        bool need_quotes = false;
        for (int j = 0; argv[i][j]; j++) {
            if (argv[i][j] == ' ') { need_quotes = true; break; }
        }
        if (need_quotes && pos < out_len - 1) out[pos++] = '"';

        for (int j = 0; argv[i][j] && pos < out_len - 1; j++) {
            out[pos++] = argv[i][j];
        }

        if (need_quotes && pos < out_len - 1) out[pos++] = '"';
    }
    out[pos] = 0;
}

static bool is_space_char(char c) {
    return c == ' ' || c == '\t';
}

static int find_token_start(const char *line, int len) {
    int i = len - 1;
    while (i >= 0 && !is_space_char(line[i])) i--;
    return i + 1;
}

static int common_prefix_len(char matches[][MAX_MATCH_LEN], int count) {
    if (count <= 0) return 0;
    int len = (int)strlen(matches[0]);
    for (int i = 1; i < count; i++) {
        int j = 0;
        while (j < len && matches[i][j] && matches[i][j] == matches[0][j]) j++;
        len = j;
        if (len == 0) break;
    }
    return len;
}

static int add_match_unique(char matches[][MAX_MATCH_LEN], int count, const char *candidate) {
    if (!candidate || !candidate[0]) return count;
    for (int i = 0; i < count; i++) {
        if (str_eq(matches[i], candidate)) return count;
    }
    if (count >= MAX_MATCHES) return count;
    str_copy(matches[count], candidate, MAX_MATCH_LEN);
    return count + 1;
}

static void build_path_match(const char *dir_part, const char *entry, bool is_dir, char *out, int out_len) {
    out[0] = 0;
    if (dir_part && dir_part[0] && !(dir_part[0] == '.' && dir_part[1] == 0)) {
        str_copy(out, dir_part, out_len);
        int len = (int)strlen(out);
        if (len > 0 && out[len - 1] != '/') str_append(out, "/", out_len);
    }
    str_append(out, entry, out_len);
    if (is_dir) str_append(out, "/", out_len);
}

static int collect_command_matches(const char *prefix, char matches[][MAX_MATCH_LEN]) {
    int count = 0;
    const char *builtins[] = {
        "cd", "pwd", "ls", "cat", "echo", "clear", "mkdir", "rm",
        "touch", "cp", "mv", "man", "alias", "unalias", "time", ".", "exit"
    };
    for (int i = 0; i < (int)(sizeof(builtins) / sizeof(builtins[0])); i++) {
        if (starts_with(builtins[i], prefix)) count = add_match_unique(matches, count, builtins[i]);
    }

    for (int i = 0; i < g_alias_count; i++) {
        if (starts_with(g_aliases[i].name, prefix)) {
            count = add_match_unique(matches, count, g_aliases[i].name);
        }
    }

    FAT32_FileInfo entries[128];
    for (int i = 0; i < g_path_count; i++) {
        int got = sys_list(g_paths[i], entries, 128);
        if (got <= 0) continue;
        for (int j = 0; j < got; j++) {
            if (entries[j].is_directory) continue;
            if (starts_with(entries[j].name, prefix)) {
                count = add_match_unique(matches, count, entries[j].name);
            }
        }
    }
    return count;
}

static int collect_path_matches(const char *dir_part, const char *prefix, char matches[][MAX_MATCH_LEN]) {
    const char *list_dir = (dir_part && dir_part[0]) ? dir_part : ".";
    FAT32_FileInfo entries[128];
    int got = sys_list(list_dir, entries, 128);
    if (got <= 0) return 0;

    int count = 0;
    for (int i = 0; i < got; i++) {
        if (!starts_with(entries[i].name, prefix)) continue;
        char full[MAX_MATCH_LEN];
        build_path_match(dir_part, entries[i].name, entries[i].is_directory, full, sizeof(full));
        count = add_match_unique(matches, count, full);
    }
    return count;
}

static void prompt_write(const char *tmpl) {
    render_prompt(tmpl, NULL, 0, true);
}

static void prompt_write_with_right(const char *left_tmpl, const char *right_tmpl) {
    char left_buf[128];
    render_prompt(left_tmpl, left_buf, sizeof(left_buf), false);
    int left_len = (int)strlen(left_buf);

    prompt_write(left_tmpl);
    if (!right_tmpl || !right_tmpl[0]) return;

    char right_buf[128];
    render_prompt(right_tmpl, right_buf, sizeof(right_buf), false);
    int right_len = (int)strlen(right_buf);
    if (right_len <= 0) return;

    sys_write(1, "\r", 1);
    sys_write(1, "\x1b[999C", 6);
    if (right_len > 0) {
        char num[8];
        char seq[16];
        itoa(right_len, num);
        str_copy(seq, "\x1b[", sizeof(seq));
        str_append(seq, num, sizeof(seq));
        str_append(seq, "D", sizeof(seq));
        sys_write(1, seq, (int)strlen(seq));
    }
    prompt_write(right_tmpl);
    sys_write(1, "\r", 1);
    if (left_len > 0) {
        char num[8];
        char seq[16];
        itoa(left_len, num);
        str_copy(seq, "\x1b[", sizeof(seq));
        str_append(seq, num, sizeof(seq));
        str_append(seq, "C", sizeof(seq));
        sys_write(1, seq, (int)strlen(seq));
    }
}

static void redraw_input(const char *prompt_tmpl, const char *line, int len, int cursor) {
    sys_write(1, "\r", 1);
    prompt_write_with_right(prompt_tmpl, g_cfg.prompt_right);
    sys_write(1, line, len);
    sys_write(1, "\x1b[K", 3);
    
    int diff = len - cursor;
    if (diff > 0) {
        int char_diff = text_strlen_utf8(line + cursor);
        if (char_diff > 0) {
            char num[16];
            char seq[32];
            itoa(char_diff, num);
            str_copy(seq, "\x1b[", sizeof(seq));
            str_append(seq, num, sizeof(seq));
            str_append(seq, "D", sizeof(seq));
            sys_write(1, seq, (int)strlen(seq));
        }
    }
}

static void show_matches(const char *prompt_tmpl, const char *line, int len, char matches[][MAX_MATCH_LEN], int count) {
    sys_write(1, "\n", 1);
    for (int i = 0; i < count; i++) {
        sys_write(1, matches[i], (int)strlen(matches[i]));
        sys_write(1, "  ", 2);
    }
    sys_write(1, "\n", 1);
    redraw_input(prompt_tmpl, line, len, len);
}

static int wait_for_pid_status(int pid, int *status) {
    while (1) {
        int child_status = 0;

        int rc = sys_waitpid(pid, &child_status, 1);

        if (rc == pid) {
            if (status) *status = child_status;
            return 0;
        }

        if (rc < 0) return -1;

        if (g_tty_id >= 0) {
            int fg = sys_tty_get_fg(g_tty_id);
            if (fg != pid) return -1;
        }

        sleep(10);
    }
}

static int wait_for_pid(int pid) {
    int status = 0;

    if (wait_for_pid_status(pid, &status) != 0)
        return -1;

    return status;
}

static int bsh_open_file(const char *path, const char *mode, bool *is_kernel) {
    if (!path || !mode) return -1;
    if (is_kernel) *is_kernel = true;
    return sys_open(path, mode);
}

static void cmd_clear(void) {
    sys_write(1, "\x1b[2J\x1b[H", 7);
}

static void print_command_resolution_error(const char *who, const char *cmd, int res) {
    set_color(g_color_error);
    if (res == -2) {
        printf("%s: is a directory: %s\n", who, cmd);
    } else if (res == -3) {
        printf("%s: not executable: %s\n", who, cmd);
    } else {
        printf("%s: command not found: %s\n", who, cmd);
    }
    reset_color();
}

static void builtin_time_usage(void) {
    printf("Usage: time <command> [args...]\n");
    printf("\n");
    printf("Examples:\n");
    printf("  time ls\n");
    printf("  time hexdump file.txt\n");
    printf("  time /bin/hexdump.elf file.txt\n");
}

// Reads the system uptime in milliseconds by parsing /proc/uptime
// before and after running the command, then calculating the difference.
static unsigned long long read_uptime_ms(void) {
    char buf[64];
    int fd;
    int bytes;
    int seconds;

    fd = sys_open("/proc/uptime", "r");
    if (fd < 0) return 0;

    bytes = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);

    if (bytes <= 0) return 0;

    buf[bytes] = 0;
    seconds = atoi(buf);

    return (unsigned long long)seconds * 1000ULL;
}

static int builtin_time(int argc, char *argv[]) {
    char *resolved;
    char full_path[256];
    char args_buf[256];
    char cmdline[MAX_LINE];
    unsigned long long start;
    unsigned long long end;
    unsigned long long elapsed;
    int pid = -1;
    int ret = -1;

    if (argc < 2) {
        builtin_time_usage();
        return 1;
    }

    if (str_eq(argv[1], "-h") || str_eq(argv[1], "--help")) {
        builtin_time_usage();
        return 0;
    }

    resolved = resolve_command_path(argv[1], NULL);
    if (!resolved) {
        print_command_resolution_error("time", argv[1], g_resolve_status);
        return 1;
    }
    str_copy(full_path, resolved, sizeof(full_path));

    build_args_string(argc, argv, 2, args_buf, sizeof(args_buf));

    str_copy(cmdline, full_path, sizeof(cmdline));
    if (args_buf[0]) {
        str_append(cmdline, " ", sizeof(cmdline));
        str_append(cmdline, args_buf, sizeof(cmdline));
    }

    start = read_uptime_ms();

    for (int attempt = 0; attempt < 5; attempt++) {
        pid = sys_spawn(full_path, args_buf[0] ? args_buf : NULL, SPAWN_FLAG_TERMINAL | SPAWN_FLAG_INHERIT_TTY, 0);
        if (pid >= 0) break;
        sleep(10);
    }

    if (pid >= 0) {
        if (g_tty_id >= 0) sys_tty_set_fg(g_tty_id, pid);
        if (wait_for_pid_status(pid, &ret) != 0) ret = -1;
        if (g_tty_id >= 0) sys_tty_set_fg(g_tty_id, 0);
    }

    end = read_uptime_ms();

    if (end >= start) elapsed = end - start;
    else elapsed = 0;

    printf("\n");
    printf("Command: %s\n", cmdline);
    printf("Exit code: %d\n", ret);

    if (ret == -1) {
        printf("Command failed with non-zero exit code, not reporting time.\n");
        return ret;
    }

    printf("Elapsed: %llu ms\n", elapsed);

    sys_system(SYSTEM_CMD_SLEEP, 1, 0, 0, 0);

    return ret;
}

static int builtin_cd(int argc, char *argv[]) {
    const char *path = (argc > 1) ? argv[1] : "/";
    if (chdir(path) != 0) {
        set_color(g_color_error);
        printf("cd: no such directory: %s\n", path);
        reset_color();
        return 1;
    }
    return 0;
}

static int builtin_pwd(void) {
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        shell_writeln(cwd);
        return 0;
    }
    return 1;
}

static int builtin_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) shell_write(" ", 1);
        shell_write(argv[i], (int)strlen(argv[i]));
    }
    shell_write("\n", 1);
    return 0;
}

static int builtin_ls(int argc, char *argv[]) {
    char path[256];
    if (argc > 1) str_copy(path, argv[1], sizeof(path));
    else if (!getcwd(path, sizeof(path))) str_copy(path, "/", sizeof(path));

    FAT32_FileInfo info;
    if (sys_get_file_info(path, &info) < 0) {
        set_color(g_color_error);
        printf("ls: cannot access %s\n", path);
        reset_color();
        return 1;
    }
    if (!info.is_directory) {
        set_color(g_color_file);
        shell_writeln(info.name);
        reset_color();
        return 0;
    }

    FAT32_FileInfo entries[128];
    int count = sys_list(path, entries, 128);
    if (count < 0) {
        set_color(g_color_error);
        printf("ls: cannot list %s\n", path);
        reset_color();
        return 1;
    }

    for (int i = 0; i < count; i++) {
        if (entries[i].is_directory) {
            set_color(g_color_dir);
            shell_write("[DIR]  ", 7);
            shell_writeln(entries[i].name);
        } else {
            set_color(g_color_file);
            shell_write("[FILE] ", 7);
            shell_write(entries[i].name, (int)strlen(entries[i].name));
            set_color(g_color_size);
            char size_buf[32];
            itoa((int)entries[i].size, size_buf);
            shell_write(" (", 2);
            shell_write(size_buf, (int)strlen(size_buf));
            shell_write(" bytes)\n", 8);
        }
    }
    reset_color();
    return 0;
}

static int builtin_cat(int argc, char *argv[]) {
    if (argc < 2) {
        char buffer[4096];
        int bytes;
        while ((bytes = read(0, buffer, sizeof(buffer))) > 0) {
            shell_write(buffer, bytes);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = sys_open(argv[i], "r");
        if (fd < 0) {
            set_color(g_color_error);
            printf("cat: cannot open %s\n", argv[i]);
            reset_color();
            continue;
        }
        char buffer[4096];
        int bytes;
        while ((bytes = sys_read(fd, buffer, sizeof(buffer))) > 0) {
            shell_write(buffer, bytes);
        }
        sys_close(fd);
    }
    reset_color();
    return 0;
}

static int builtin_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        set_color(g_color_error);
        printf("Usage: mkdir <dir>\n");
        reset_color();
        return 1;
    }
    if (sys_mkdir(argv[1]) == 0) return 0;
    set_color(g_color_error);
    printf("mkdir: cannot create %s\n", argv[1]);
    reset_color();
    return 1;
}

static int builtin_rm(int argc, char *argv[]) {
    if (argc < 2) {
        set_color(g_color_error);
        printf("Usage: rm <path>\n");
        reset_color();
        return 1;
    }
    if (delete_recursive(argv[1]) == 0) return 0;
    set_color(g_color_error);
    printf("rm: cannot delete %s\n", argv[1]);
    reset_color();
    return 1;
}

static int builtin_touch(int argc, char *argv[]) {
    if (argc < 2) {
        set_color(g_color_error);
        printf("Usage: touch <file>\n");
        reset_color();
        return 1;
    }
    int fd = sys_open(argv[1], "w");
    if (fd < 0) {
        set_color(g_color_error);
        printf("touch: cannot create %s\n", argv[1]);
        reset_color();
        return 1;
    }
    sys_close(fd);
    return 0;
}

static void combine_path(char *dest, const char *path1, const char *path2) {
    int i = 0;
    while (path1[i]) {
        dest[i] = path1[i];
        i++;
    }
    if (i > 0 && dest[i - 1] != '/') dest[i++] = '/';
    int j = 0;
    while (path2[j]) dest[i++] = path2[j++];
    dest[i] = 0;
}

static const char* get_basename(const char *path) {
    const char *last = NULL;
    int len = 0;
    while (path[len]) {
        if (path[len] == '/') last = path + len;
        len++;
    }
    if (!last) return path;
    if (last[1] == 0) {
        if (len <= 1) return path;
        int i = len - 2;
        while (i >= 0 && path[i] != '/') i--;
        if (i < 0) return path;
        return path + i + 1;
    }
    return last + 1;
}

static void copy_recursive(const char *src, const char *dst) {
    FAT32_FileInfo info;
    if (sys_get_file_info(src, &info) < 0) return;

    if (info.is_directory) {
        sys_mkdir(dst);
        FAT32_FileInfo entries[64];
        int count = sys_list(src, entries, 64);
        for (int i = 0; i < count; i++) {
            if (str_eq(entries[i].name, ".") || str_eq(entries[i].name, "..")) continue;
            char sub_src[512], sub_dst[512];
            combine_path(sub_src, src, entries[i].name);
            combine_path(sub_dst, dst, entries[i].name);
            copy_recursive(sub_src, sub_dst);
        }
    } else {
        int fd_in = sys_open(src, "r");
        if (fd_in < 0) return;
        int fd_out = sys_open(dst, "w");
        if (fd_out < 0) { sys_close(fd_in); return; }
        char buffer[4096];
        int bytes;
        while ((bytes = sys_read(fd_in, buffer, sizeof(buffer))) > 0) {
            sys_write_fs(fd_out, buffer, bytes);
        }
        sys_close(fd_in);
        sys_close(fd_out);
    }
}

static int delete_recursive(const char *path) {
    FAT32_FileInfo info;
    if (sys_get_file_info(path, &info) < 0) return -1;

    if (info.is_directory) {
        while (1) {
            FAT32_FileInfo *entries = (FAT32_FileInfo*)malloc(sizeof(FAT32_FileInfo) * 256);
            if (!entries) return -1;

            int count = sys_list(path, entries, 256);
            if (count < 0) {
                free(entries);
                return -1;
            }
            if (count == 0) {
                free(entries);
                break;
            }

            int deleted_any = 0;
            for (int i = 0; i < count; i++) {
                if (str_eq(entries[i].name, ".") || str_eq(entries[i].name, "..")) continue;
                char sub_path[512];
                combine_path(sub_path, path, entries[i].name);
                if (delete_recursive(sub_path) != 0) {
                    free(entries);
                    return -1;
                }
                deleted_any = 1;
            }

            free(entries);
            if (!deleted_any) break;
        }
        return sys_delete(path);
    } else {
        return sys_delete(path);
    }
}

static int builtin_cp(int argc, char *argv[]) {
    bool recursive = false;
    char *src = NULL;
    char *dst = NULL;
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-r")) recursive = true;
        else if (!src) src = argv[i];
        else if (!dst) dst = argv[i];
    }
    if (!src || !dst) {
        set_color(g_color_error);
        printf("Usage: cp [-r] <source> <dest>\n");
        reset_color();
        return 1;
    }

    FAT32_FileInfo info_src;
    if (sys_get_file_info(src, &info_src) < 0) {
        set_color(g_color_error);
        printf("cp: source does not exist\n");
        reset_color();
        return 1;
    }
    if (info_src.is_directory && !recursive) {
        set_color(g_color_error);
        printf("cp: %s is a directory (use -r)\n", src);
        reset_color();
        return 1;
    }

    char actual_dst[512];
    FAT32_FileInfo info_dst;
    if (sys_get_file_info(dst, &info_dst) == 0 && info_dst.is_directory) {
        const char *base = get_basename(src);
        combine_path(actual_dst, dst, base);
    } else {
        str_copy(actual_dst, dst, sizeof(actual_dst));
    }

    if (recursive) copy_recursive(src, actual_dst);
    else copy_recursive(src, actual_dst);
    return 0;
}

static int builtin_mv(int argc, char *argv[]) {
    if (argc < 3) {
        set_color(g_color_error);
        printf("Usage: mv <source> <dest>\n");
        reset_color();
        return 1;
    }

    char *src = argv[1];
    char *dst = argv[2];

    char actual_dst[512];
    FAT32_FileInfo info_dst;
    if (sys_get_file_info(dst, &info_dst) == 0 && info_dst.is_directory) {
        const char *base = get_basename(src);
        combine_path(actual_dst, dst, base);
    } else {
        str_copy(actual_dst, dst, sizeof(actual_dst));
    }

    copy_recursive(src, actual_dst);
    delete_recursive(src);
    return 0;
}

static int builtin_man(int argc, char *argv[]) {
    if (argc < 2) {
        set_color(g_color_error);
        printf("What manual page do you want? Example: man ls\n");
        reset_color();
        return 0;
    }

    char path[128];
    str_copy(path, "/Library/man/", sizeof(path));
    str_append(path, argv[1], sizeof(path));
    str_append(path, ".txt", sizeof(path));

    int fd = sys_open(path, "r");
    if (fd < 0) {
        set_color(g_color_error);
        printf("No manual entry for %s\n", argv[1]);
        reset_color();
        return 1;
    }

    char buffer[4096];
    int bytes;
    while ((bytes = sys_read(fd, buffer, sizeof(buffer))) > 0) {
        shell_write(buffer, bytes);
    }
    sys_close(fd);
    shell_write("\n", 1);
    return 0;
}

static int builtin_alias(int argc, char *argv[]) {
    if (argc == 1) {
        for (int i = 0; i < g_alias_count; i++) {
            shell_write("alias ", 6);
            shell_write(g_aliases[i].name, (int)strlen(g_aliases[i].name));
            shell_write("=", 1);
            shell_writeln(g_aliases[i].value);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *eq = argv[i];
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') {
            const char *val = alias_get(argv[i]);
            if (val) {
                shell_write("alias ", 6);
                shell_write(argv[i], (int)strlen(argv[i]));
                shell_write("=", 1);
                shell_writeln(val);
                continue;
            }
            set_color(g_color_error);
            printf("alias: invalid definition: %s\n", argv[i]);
            reset_color();
            return 1;
        }
        *eq = 0;
        alias_add(argv[i], eq + 1);
    }
    return 0;
}

static int builtin_unalias(int argc, char *argv[]) {
    if (argc < 2) {
        set_color(g_color_error);
        printf("Usage: unalias <name>\n");
        reset_color();
        return 1;
    }
    for (int i = 1; i < argc; i++) alias_remove(argv[i]);
    return 0;
}

static int execute_builtin(int argc, char *argv[]) {
    if (argc == 0) return 0;
    if (str_eq(argv[0], "cd")) return builtin_cd(argc, argv);
    if (str_eq(argv[0], "pwd")) return builtin_pwd();
    if (str_eq(argv[0], "ls")) return builtin_ls(argc, argv);
    if (str_eq(argv[0], "cat")) return builtin_cat(argc, argv);
    if (str_eq(argv[0], "echo")) return builtin_echo(argc, argv);
    if (str_eq(argv[0], "clear")) { cmd_clear(); return 0; }
    if (str_eq(argv[0], "mkdir")) return builtin_mkdir(argc, argv);
    if (str_eq(argv[0], "rm")) return builtin_rm(argc, argv);
    if (str_eq(argv[0], "touch")) return builtin_touch(argc, argv);
    if (str_eq(argv[0], "cp")) return builtin_cp(argc, argv);
    if (str_eq(argv[0], "mv")) return builtin_mv(argc, argv);
    if (str_eq(argv[0], "man")) return builtin_man(argc, argv);
    if (str_eq(argv[0], "alias")) return builtin_alias(argc, argv);
    if (str_eq(argv[0], "unalias")) return builtin_unalias(argc, argv);
    if (str_eq(argv[0], "time")) return builtin_time(argc, argv);
    if (str_eq(argv[0], ".")) {
        if (argc < 2) {
            set_color(g_color_error);
            printf("Usage: . <script>\n");
            reset_color();
            return 1;
        }
        if (!run_script(argv[1])) {
            set_color(g_color_error);
            printf("bsh: cannot run script: %s\n", argv[1]);
            reset_color();
            return 1;
        }
        return 0;
    }
    if (str_eq(argv[0], "exit")) return 2;
    return -1;
}

static bool is_builtin_name(const char *name) {
    return str_eq(name, "cd") || str_eq(name, "pwd") || str_eq(name, "ls") ||
           str_eq(name, "cat") || str_eq(name, "echo") || str_eq(name, "clear") ||
           str_eq(name, "mkdir") || str_eq(name, "rm") || str_eq(name, "touch") ||
           str_eq(name, "cp") || str_eq(name, "mv") || str_eq(name, "man") ||
           str_eq(name, "alias") || str_eq(name, "unalias") || str_eq(name, ".") ||
           str_eq(name, "exit");
}

typedef enum {
    TOK_WORD = 0,
    TOK_PIPE,
    TOK_GT,
    TOK_GTGT,
    TOK_LT,
    TOK_AMP,
    TOK_ANDAND,
    TOK_OROR,
    TOK_SEMI,
    TOK_END
} bsh_tok_type_t;

typedef struct {
    bsh_tok_type_t type;
    char text[MAX_MATCH_LEN];
} bsh_token_t;

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    char *redir_in;
    char *redir_out;
    bool redir_append;
} bsh_simple_cmd_t;

#define MAX_TOKENS 128
#define MAX_PIPE_CMDS 16

static bool is_op_char(char c) {
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}

static void print_syntax_error(const char *msg) {
    set_color(g_color_error);
    printf("bsh: syntax error: %s\n", msg);
    reset_color();
}

static void alias_snapshot_save(alias_t out_aliases[], int *out_count) {
    if (!out_aliases || !out_count) return;
    *out_count = g_alias_count;
    for (int i = 0; i < g_alias_count && i < MAX_ALIASES; i++) {
        out_aliases[i] = g_aliases[i];
    }
}

static void alias_snapshot_restore(const alias_t saved_aliases[], int saved_count) {
    g_alias_count = 0;
    if (!saved_aliases || saved_count <= 0) return;
    int copy_count = saved_count < MAX_ALIASES ? saved_count : MAX_ALIASES;
    for (int i = 0; i < copy_count; i++) {
        g_aliases[i] = saved_aliases[i];
    }
    g_alias_count = copy_count;
}

static int tokenize_line(const char *line, bsh_token_t toks[], int max_toks) {
    if (!line || !toks || max_toks < 2) return -1;
    int tcount = 0;
    int i = 0;

    while (line[i]) {
        while (line[i] == ' ' || line[i] == '\t') i++;
        if (!line[i]) break;
        if (tcount >= max_toks - 1) {
            print_syntax_error("too many tokens");
            return -1;
        }

        if (line[i] == '&' && line[i + 1] == '&') {
            toks[tcount].type = TOK_ANDAND;
            toks[tcount].text[0] = 0;
            tcount++;
            i += 2;
            continue;
        }
        if (line[i] == '|' && line[i + 1] == '|') {
            toks[tcount].type = TOK_OROR;
            toks[tcount].text[0] = 0;
            tcount++;
            i += 2;
            continue;
        }
        if (line[i] == '>' && line[i + 1] == '>') {
            toks[tcount].type = TOK_GTGT;
            toks[tcount].text[0] = 0;
            tcount++;
            i += 2;
            continue;
        }

        if (line[i] == '|') {
            toks[tcount].type = TOK_PIPE;
            toks[tcount].text[0] = 0;
            tcount++;
            i++;
            continue;
        }
        if (line[i] == '&') {
            toks[tcount].type = TOK_AMP;
            toks[tcount].text[0] = 0;
            tcount++;
            i++;
            continue;
        }
        if (line[i] == ';') {
            toks[tcount].type = TOK_SEMI;
            toks[tcount].text[0] = 0;
            tcount++;
            i++;
            continue;
        }
        if (line[i] == '<') {
            toks[tcount].type = TOK_LT;
            toks[tcount].text[0] = 0;
            tcount++;
            i++;
            continue;
        }
        if (line[i] == '>') {
            toks[tcount].type = TOK_GT;
            toks[tcount].text[0] = 0;
            tcount++;
            i++;
            continue;
        }

        toks[tcount].type = TOK_WORD;
        int out = 0;
        while (line[i] && line[i] != ' ' && line[i] != '\t' && !is_op_char(line[i])) {
            if (line[i] == '"' || line[i] == '\'') {
                char quote = line[i++];
                while (line[i] && line[i] != quote) {
                    if (out < MAX_MATCH_LEN - 1) toks[tcount].text[out++] = line[i];
                    i++;
                }
                if (line[i] == quote) i++;
                continue;
            }
            if (out < MAX_MATCH_LEN - 1) toks[tcount].text[out++] = line[i];
            i++;
        }
        toks[tcount].text[out] = 0;
        if (out == 0) {
            print_syntax_error("unexpected token");
            return -1;
        }
        tcount++;
    }

    toks[tcount].type = TOK_END;
    toks[tcount].text[0] = 0;
    return tcount;
}

static bool parse_simple_command(bsh_token_t toks[], int *idx, bsh_simple_cmd_t *cmd) {
    if (!toks || !idx || !cmd) return false;
    cmd->argc = 0;
    cmd->redir_in = NULL;
    cmd->redir_out = NULL;
    cmd->redir_append = false;

    while (1) {
        bsh_tok_type_t t = toks[*idx].type;
        if (t == TOK_WORD) {
            if (cmd->argc >= MAX_ARGS - 1) {
                print_syntax_error("too many arguments");
                return false;
            }
            cmd->argv[cmd->argc++] = toks[*idx].text;
            (*idx)++;
            continue;
        }
        if (t == TOK_LT || t == TOK_GT || t == TOK_GTGT) {
            bsh_tok_type_t redir = t;
            (*idx)++;
            if (toks[*idx].type != TOK_WORD) {
                print_syntax_error("missing filename after redirection");
                return false;
            }
            if (redir == TOK_LT) {
                cmd->redir_in = toks[*idx].text;
            } else {
                cmd->redir_out = toks[*idx].text;
                cmd->redir_append = (redir == TOK_GTGT);
            }
            (*idx)++;
            continue;
        }
        break;
    }

    cmd->argv[cmd->argc] = NULL;
    if (cmd->argc <= 0) {
        print_syntax_error("expected command");
        return false;
    }
    return true;
}

static int execute_argv_inner(int argc, char *argv[], int depth, bool isolated, bool background, bool *want_exit, int *out_pid) {
    if (argc <= 0) return 0;
    if (depth > 8) return 1;
    if (out_pid) *out_pid = -1;

    if (!str_eq(argv[0], "alias") && !str_eq(argv[0], "unalias")) {
        const char *alias_val = alias_get(argv[0]);
        if (alias_val) {
            char expanded[MAX_LINE];
            str_copy(expanded, alias_val, sizeof(expanded));

            char tail[MAX_LINE];
            build_args_string(argc, argv, 1, tail, sizeof(tail));
            if (tail[0]) {
                str_append(expanded, " ", sizeof(expanded));
                str_append(expanded, tail, sizeof(expanded));
            }

            char split_buf[MAX_LINE];
            str_copy(split_buf, expanded, sizeof(split_buf));
            char *expanded_argv[MAX_ARGS];
            int expanded_argc = split_args(split_buf, expanded_argv, MAX_ARGS);
            if (expanded_argc <= 0) return 0;
            return execute_argv_inner(expanded_argc, expanded_argv, depth + 1, isolated, background, want_exit, out_pid);
        }
    }

    int bi = -1;
    if (isolated) {
        alias_t saved_aliases[MAX_ALIASES];
        int saved_alias_count = 0;
        char saved_cwd[256];
        bool had_cwd = getcwd(saved_cwd, sizeof(saved_cwd)) != NULL;

        alias_snapshot_save(saved_aliases, &saved_alias_count);
        bi = execute_builtin(argc, argv);

        alias_snapshot_restore(saved_aliases, saved_alias_count);
        if (had_cwd) chdir(saved_cwd);
    } else {
        bi = execute_builtin(argc, argv);
    }

    if (bi == 2) {
        if (!isolated && want_exit) *want_exit = true;
        return 0;
    }
    if (bi >= 0) return bi == 0 ? 0 : 1;

    char full_path[256];
    int cmd_res = resolve_command(argv[0], full_path, sizeof(full_path));
    if (cmd_res != 0) {
        print_command_resolution_error("bsh", argv[0], cmd_res);
        return 1;
    }

    char args_buf[256];
    build_args_string(argc, argv, 1, args_buf, sizeof(args_buf));

    uint64_t spawn_flags = background
        ? (SPAWN_FLAG_INHERIT_TTY | SPAWN_FLAG_BACKGROUND)
        : (SPAWN_FLAG_TERMINAL | SPAWN_FLAG_INHERIT_TTY);

    int pid = -1;
    for (int attempt = 0; attempt < 5; attempt++) {
        pid = sys_spawn(full_path, args_buf[0] ? args_buf : NULL, spawn_flags, 0);
        if (pid >= 0) break;
        sleep(10);
    }
    if (pid < 0) {
        set_color(g_color_error);
        printf("bsh: failed to launch: %s\n", full_path);
        reset_color();
        return 1;
    }

    if (out_pid) *out_pid = pid;
    if (background) return 0;

    if (g_tty_id >= 0) sys_tty_set_fg(g_tty_id, pid);
    int status = wait_for_pid(pid);
    if (g_tty_id >= 0) sys_tty_set_fg(g_tty_id, 0);
    return status;
}

static int run_simple_command_with_fds(
    bsh_simple_cmd_t *cmd,
    int in_fd,
    int out_fd,
    bool isolate,
    bool background,
    bool *want_exit,
    int *out_pid
) {
    if (out_pid) *out_pid = -1;
    if (!cmd || cmd->argc <= 0) return 0;

    int saved_in = sys_dup(0);
    int saved_out = sys_dup(1);

    if (saved_in < 0 || saved_out < 0) {
        if (saved_in >= 0) sys_close(saved_in);
        if (saved_out >= 0) sys_close(saved_out);
        set_color(g_color_error);
        printf("bsh: redirection/pipeline is unavailable in this session\n");
        reset_color();
        return 1;
    }

    int redir_in_fd = -1;
    int redir_out_fd = -1;
    bool in_is_kernel = false;
    bool out_is_kernel = false;
    int rc = 0;

    // 1. Handle Pipe Input
    if (in_fd >= 0 && sys_dup2(in_fd, 0) < 0) {
        set_color(g_color_error);
        printf("bsh: failed to set pipeline input\n");
        reset_color();
        rc = 1;
        goto done;
    }

    // 2. Handle File Redirection Input (overrides pipe)
    if (cmd->redir_in) {
        redir_in_fd = bsh_open_file(cmd->redir_in, "r", &in_is_kernel);
        if (redir_in_fd < 0 || sys_dup2(redir_in_fd, 0) < 0) {
            set_color(g_color_error);
            printf("bsh: cannot read from %s\n", cmd->redir_in);
            reset_color();
            rc = 1;
            goto done;
        }
    }

    // 3. Handle Pipe Output
    if (out_fd >= 0 && sys_dup2(out_fd, 1) < 0) {
        set_color(g_color_error);
        printf("bsh: failed to set pipeline output\n");
        reset_color();
        rc = 1;
        goto done;
    }

    // 4. Handle File Redirection Output (overrides pipe)
    if (cmd->redir_out) {
        const char *mode = cmd->redir_append ? "a" : "w";
        redir_out_fd = bsh_open_file(cmd->redir_out, mode, &out_is_kernel);
        if (redir_out_fd < 0 || sys_dup2(redir_out_fd, 1) < 0) {
            set_color(g_color_error);
            printf("bsh: cannot write to %s\n", cmd->redir_out);
            reset_color();
            rc = 1;
            goto done;
        }
    }

    rc = execute_argv_inner(cmd->argc, cmd->argv, 0, isolate, background, want_exit, out_pid);

done:
    if (redir_in_fd >= 0) sys_close(redir_in_fd);
    if (redir_out_fd >= 0) sys_close(redir_out_fd);

    sys_dup2(saved_in, 0);
    sys_dup2(saved_out, 1);
    sys_close(saved_in);
    sys_close(saved_out);
    return rc;
}

static bool parse_pipeline(
    bsh_token_t toks[],
    int *idx,
    bsh_simple_cmd_t cmds[],
    int *cmd_count
) {
    if (!toks || !idx || !cmds || !cmd_count) return false;
    *cmd_count = 0;

    if (!parse_simple_command(toks, idx, &cmds[*cmd_count])) return false;
    (*cmd_count)++;

    while (toks[*idx].type == TOK_PIPE) {
        (*idx)++;
        if (*cmd_count >= MAX_PIPE_CMDS) {
            print_syntax_error("pipeline too long");
            return false;
        }
        if (!parse_simple_command(toks, idx, &cmds[*cmd_count])) return false;
        (*cmd_count)++;
    }
    return true;
}

static int execute_pipeline_cmds(
    bsh_simple_cmd_t cmds[],
    int cmd_count,
    bool background,
    bool *want_exit
) {
    if (!cmds || cmd_count <= 0) return 0;

    if (cmd_count == 1) {
        int pid = -1;
        int res = run_simple_command_with_fds(&cmds[0], -1, -1, background, background, want_exit, &pid);
        return background ? 0 : res;
    }

    int pipes[MAX_PIPE_CMDS - 1][2];
    int pids[MAX_PIPE_CMDS];
    for (int i = 0; i < cmd_count; i++) pids[i] = -1;

    for (int i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            set_color(g_color_error);
            printf("bsh: failed to create pipe\n");
            reset_color();
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }
    }

    for (int i = 0; i < cmd_count; i++) {
        int in_fd = (i == 0) ? -1 : pipes[i - 1][0];
        int out_fd = (i == cmd_count - 1) ? -1 : pipes[i][1];

        run_simple_command_with_fds(&cmds[i], in_fd, out_fd, true, true, want_exit, &pids[i]);

        if (in_fd >= 0) close(in_fd);
        if (out_fd >= 0) close(out_fd);
    }

    if (background) return 0;

    int last_status = 0;
    for (int i = 0; i < cmd_count; i++) {
        if (pids[i] > 0) {
            int s = wait_for_pid(pids[i]);
            if (i == cmd_count - 1) last_status = s;
        }
    }

    return last_status;
}

static bool parse_conditional_end_index(bsh_token_t toks[], int start_idx, int *out_end_idx) {
    int idx = start_idx;
    bsh_simple_cmd_t cmds[MAX_PIPE_CMDS];
    int cmd_count = 0;

    if (!parse_pipeline(toks, &idx, cmds, &cmd_count)) return false;

    while (toks[idx].type == TOK_ANDAND || toks[idx].type == TOK_OROR) {
        idx++;
        if (!parse_pipeline(toks, &idx, cmds, &cmd_count)) return false;
    }

    *out_end_idx = idx;
    return true;
}

static int execute_conditional_range(
    bsh_token_t toks[],
    int start_idx,
    int end_idx,
    bool background,
    bool *want_exit
) {
    int idx = start_idx;
    int status = 0;
    bool execute_next = true;

    while (idx < end_idx) {
        bsh_simple_cmd_t cmds[MAX_PIPE_CMDS];
        int cmd_count = 0;
        if (!parse_pipeline(toks, &idx, cmds, &cmd_count)) return 1;

        if (execute_next) {
            status = execute_pipeline_cmds(cmds, cmd_count, background, want_exit);
            if (*want_exit) return 2;
        }

        if (idx >= end_idx) break;

        bsh_tok_type_t op = toks[idx].type;
        idx++;
        if (op == TOK_ANDAND) {
            execute_next = (status == 0);
        } else if (op == TOK_OROR) {
            execute_next = (status != 0);
        } else {
            print_syntax_error("unexpected conditional operator");
            return 1;
        }
    }

    return status;
}

static int execute_line(const char *line) {
    if (!line || !line[0]) return 0;

    char line_copy[MAX_LINE];
    str_copy(line_copy, line, sizeof(line_copy));
    trim(line_copy);
    if (!line_copy[0]) return 0;
    if (line_copy[0] == '#') return 0;

    bsh_token_t toks[MAX_TOKENS];
    int tcount = tokenize_line(line_copy, toks, MAX_TOKENS);
    if (tcount < 0) return 1;
    if (tcount == 0 || toks[0].type == TOK_END) return 0;

    int idx = 0;
    int status = 0;
    bool want_exit = false;

    while (toks[idx].type != TOK_END) {
        if (toks[idx].type == TOK_SEMI || toks[idx].type == TOK_AMP) {
            print_syntax_error("unexpected separator");
            return 1;
        }

        int end_idx = idx;
        if (!parse_conditional_end_index(toks, idx, &end_idx)) return 1;

        bool background = false;
        if (toks[end_idx].type == TOK_AMP) background = true;

        status = execute_conditional_range(toks, idx, end_idx, background, &want_exit);
        if (want_exit) return 2;

        idx = end_idx;
        if (toks[idx].type == TOK_SEMI || toks[idx].type == TOK_AMP) {
            idx++;
            continue;
        }
        if (toks[idx].type != TOK_END) {
            print_syntax_error("unexpected token after command list");
            return 1;
        }
    }

    return status;
}

static bool run_script(const char *path) {
    if (!path || !path[0]) return false;
    char resolved[256];
    if (!resolve_script_path(path, resolved, sizeof(resolved))) return false;

    int fd = sys_open(resolved, "r");
    if (fd < 0) return false;

    char buf[4096];
    int bytes = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (bytes <= 0) return true;

    buf[bytes] = 0;
    char *line = buf;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n' && *end != '\r') end++;
        char saved = *end;
        *end = 0;

        trim(line);
        if (line[0]) execute_line(line);

        line = end + (saved ? 1 : 0);
        if (saved == '\r' && *line == '\n') line++;
    }
    return true;
}

static int read_line(char *out, int max_len, const char *prompt_tmpl) {
    int len = 0;
    int cursor = 0;
    int hist_index = g_history_count;
    bool search_mode = false;
    char saved_line[MAX_LINE];
    saved_line[0] = 0;
    char search_prefix[MAX_LINE];
    search_prefix[0] = 0;
    out[0] = 0;

    while (1) {
        struct pollfd pfd = { .fd = 0, .events = POLLIN, .revents = 0 };
        sys_poll(&pfd, 1, -1); // Wait indefinitely

        char ch = 0;
        int got = sys_tty_read_in(&ch, 1);
        if (got <= 0) continue;

        if (ch == 3) { // Ctrl+C
            sys_write(1, "^C\n", 3);
            out[0] = 0;
            return 0;
        }

        if (ch == '\r' || ch == '\n') {
            if (g_cfg.prompt_minimal_history) {
                const char *minimal_tmpl = g_cfg.prompt_minimal_prefix[0] ? g_cfg.prompt_minimal_prefix : "> ";
                sys_write(1, "\r", 1);
                sys_write(1, "\x1b[K", 3);
                prompt_write(minimal_tmpl);
                sys_write(1, out, len);
                sys_write(1, "\n", 1);
            } else {
                sys_write(1, "\n", 1);
            }
            out[len] = 0;
            return len;
        }

        if (ch == '\b' || ch == 127) {
            if (cursor > 0) {
                const char *prev = text_prev_utf8(out, out + cursor);
                int shift = (int)(out + cursor - prev);
                for (int i = cursor; i <= len; i++) {
                    out[i - shift] = out[i];
                }
                cursor -= shift;
                len -= shift;
                redraw_input(prompt_tmpl, out, len, cursor);
            }
            search_mode = false;
            hist_index = g_history_count;
            continue;
        }

        if (ch == '\t') {
            if (!g_cfg.complete_enabled) continue;
            int token_start = find_token_start(out, len);
            int token_len = len - token_start;
            if (token_len <= 0) continue;

            char token[MAX_MATCH_LEN];
            int copy_len = token_len < (int)sizeof(token) - 1 ? token_len : (int)sizeof(token) - 1;
            for (int i = 0; i < copy_len; i++) token[i] = out[token_start + i];
            token[copy_len] = 0;

            bool has_slash = false;
            for (int i = 0; token[i]; i++) {
                if (token[i] == '/') { has_slash = true; break; }
            }

            char matches[MAX_MATCHES][MAX_MATCH_LEN];
            int match_count = 0;

            if (token_start == 0 && !has_slash) {
                match_count = collect_command_matches(token, matches);
            } else {
                char dir_part[MAX_PATH_LEN];
                char prefix[MAX_PATH_LEN];
                int last_slash = -1;
                for (int i = 0; token[i]; i++) if (token[i] == '/') last_slash = i;
                if (last_slash >= 0) {
                    int dlen = last_slash + 1;
                    if (dlen >= (int)sizeof(dir_part)) dlen = (int)sizeof(dir_part) - 1;
                    for (int i = 0; i < dlen; i++) dir_part[i] = token[i];
                    dir_part[dlen] = 0;
                    str_copy(prefix, token + last_slash + 1, sizeof(prefix));
                } else {
                    dir_part[0] = 0;
                    str_copy(prefix, token, sizeof(prefix));
                }

                match_count = collect_path_matches(dir_part, prefix, matches);
            }

            if (match_count == 0) continue;

            if (match_count == 1) {
                out[token_start] = 0;
                str_append(out, matches[0], max_len);
                len = (int)strlen(out);
                cursor = len;
                redraw_input(prompt_tmpl, out, len, cursor);
            } else {
                int common_len = common_prefix_len(matches, match_count);
                if (common_len > token_len) {
                    char prefix_buf[MAX_MATCH_LEN];
                    int p = common_len < (int)sizeof(prefix_buf) - 1 ? common_len : (int)sizeof(prefix_buf) - 1;
                    for (int i = 0; i < p; i++) prefix_buf[i] = matches[0][i];
                    prefix_buf[p] = 0;
                    out[token_start] = 0;
                    str_append(out, prefix_buf, max_len);
                    len = (int)strlen(out);
                    cursor = len;
                    redraw_input(prompt_tmpl, out, len, cursor);
                } else {
                    show_matches(prompt_tmpl, out, len, matches, match_count);
                    cursor = len;
                }
            }
            search_mode = false;
            hist_index = g_history_count;
            continue;
        }

        if (ch == 27) {
            char seq[2];
            int g1 = 0, g2 = 0;
            struct pollfd pfd = { .fd = 0, .events = POLLIN, .revents = 0 };
            if (sys_poll(&pfd, 1, 50) > 0) g1 = sys_tty_read_in(&seq[0], 1);
            if (g1 > 0 && sys_poll(&pfd, 1, 50) > 0) g2 = sys_tty_read_in(&seq[1], 1);
            
            if (g1 > 0 && g2 > 0 && seq[0] == '[') {
                if (seq[1] == 'A') ch = 17;
                else if (seq[1] == 'B') ch = 18;
                else if (seq[1] == 'C') ch = 19;
                else if (seq[1] == 'D') ch = 20;
            }
            if (ch == 27) continue;
        }

        if (ch == 17) { // Up
            if (g_history_count == 0) continue;
            if (!search_mode) {
                str_copy(saved_line, out, sizeof(saved_line));
                str_copy(search_prefix, out, sizeof(search_prefix));
                search_mode = true;
                hist_index = g_history_count;
            }

            int found = -1;
            for (int i = hist_index - 1; i >= 0; i--) {
                if (starts_with(g_history[i], search_prefix)) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                hist_index = found;
                str_copy(out, g_history[hist_index], max_len);
                len = (int)strlen(out);
                cursor = len;
                redraw_input(prompt_tmpl, out, len, cursor);
            }
            continue;
        }

        if (ch == 18) { // Down
            if (g_history_count == 0 || !search_mode) continue;
            int found = -1;
            for (int i = hist_index + 1; i < g_history_count; i++) {
                if (starts_with(g_history[i], search_prefix)) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                hist_index = found;
                str_copy(out, g_history[hist_index], max_len);
                len = (int)strlen(out);
                cursor = len;
            } else {
                search_mode = false;
                hist_index = g_history_count;
                str_copy(out, saved_line, max_len);
                len = (int)strlen(out);
                cursor = len;
            }
            redraw_input(prompt_tmpl, out, len, cursor);
            continue;
        }

        if (ch == 20) { // Left
            if (cursor > 0) {
                const char *prev = text_prev_utf8(out, out + cursor);
                if (prev) cursor = (int)(prev - out);
                redraw_input(prompt_tmpl, out, len, cursor);
            }
            continue;
        }

        if (ch == 19) { // Right
            if (cursor < len) {
                const char *next = text_next_utf8(out + cursor);
                if (next && *next) cursor = (int)(next - out);
                else cursor = len;
                redraw_input(prompt_tmpl, out, len, cursor);
            }
            continue;
        }

        if (((unsigned char)ch >= 32 || (signed char)ch < 0) && len < max_len - 1) {
            for (int i = len; i >= cursor; i--) {
                out[i + 1] = out[i];
            }
            out[cursor++] = ch;
            len++;
            redraw_input(prompt_tmpl, out, len, cursor);
            search_mode = false;
            hist_index = g_history_count;
        }
    }
}

int main(int argc, char **argv) {
    char start_dir[256];
    start_dir[0] = 0;
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-t") && i + 1 < argc) {
            g_tty_id = atoi(argv[i + 1]);
            i++;
        } else if (str_eq(argv[i], "-d") && i + 1 < argc) {
            str_copy(start_dir, argv[i + 1], sizeof(start_dir));
            i++;
        }
    }

    config_load();
    load_shell_colors();
    if (start_dir[0]) {
        chdir(start_dir);
    } else if (sys_exists("/root")) {
        chdir("/root");
    }
    history_load();

    if (g_cfg.boot_script[0]) {
        if (!sys_exists("/Library/bsh/.boot_ran")) {
            run_script(g_cfg.boot_script);
            int fd = sys_open("/Library/bsh/.boot_ran", "w");
            if (fd >= 0) sys_close(fd);
        }
    }

    if (g_cfg.startup[0]) {
        run_script(g_cfg.startup);
    }

    while (1) {
        if (g_tty_id >= 0) sys_tty_set_fg(g_tty_id, 0);

        const char *prompt_tmpl = g_cfg.prompt_left[0] ? g_cfg.prompt_left : DEFAULT_PROMPT;
        sys_write(1, "\r", 1);
        sys_write(1, "\x1b[K", 3);
        prompt_write_with_right(prompt_tmpl, g_cfg.prompt_right);

        char line[MAX_LINE];
        int len = read_line(line, sizeof(line), prompt_tmpl);
        if (len <= 0) continue;

        history_add(line);
        int res = execute_line(line);
        if (res == 2) break;
    }

    return 0;
}

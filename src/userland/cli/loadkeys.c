// Copyright (c) 2026 Lluciocc (https://github.com/lluciocc)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Change the active keyboard layout.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

enum {
    LOADKEYS_QWERTY = 0,
    LOADKEYS_AZERTY = 1,
    LOADKEYS_QWERTZ = 2,
    LOADKEYS_DVORAK = 3,
};

static void print_usage(void) {
    printf("Usage: loadkeys [option|layout]\n");
    printf("Options:\n");
    printf("  -h, --help      Show this help message and exit\n");
    printf("  -l, --list      List available keyboard layouts\n");
    printf("  -c, --current   Show the current keyboard layout\n");
}

static int print_available_layouts(void) {
    int count = sys_system(SYSTEM_CMD_GET_KEYBOARD_LAYOUTS, 0, 0, 0, 0);
    if (count <= 0) {
        printf("No keyboard layouts available.\n");
        return 1;
    }

    sys_keyboard_layout_t *layouts = malloc(sizeof(sys_keyboard_layout_t) * (size_t)count);
    if (!layouts) {
        printf("loadkeys: not enough memory\n");
        return 1;
    }

    int total = sys_system(SYSTEM_CMD_GET_KEYBOARD_LAYOUTS, (uint64_t)layouts, (uint64_t)count, 0, 0);
    int shown = total;
    if (shown > count) shown = count;
    if (shown < 0) shown = 0;

    printf("Available keyboard layouts:\n");
    for (int i = 0; i < shown; i++) {
        printf("  %u: %s\n", layouts[i].id, layouts[i].name);
    }

    free(layouts);
    return 0;
}

static int parse_layout(const char *arg, uint64_t *layout_id) {
    if (!arg || !layout_id) return 0;

    if (strcasecmp(arg, "us") == 0 || strcasecmp(arg, "qwerty") == 0) {
        *layout_id = LOADKEYS_QWERTY;
        return 1;
    }
    if (strcasecmp(arg, "fr") == 0 || strcasecmp(arg, "azerty") == 0) {
        *layout_id = LOADKEYS_AZERTY;
        return 1;
    }
    if (strcasecmp(arg, "de") == 0 || strcasecmp(arg, "qwertz") == 0) {
        *layout_id = LOADKEYS_QWERTZ;
        return 1;
    }
    if (strcasecmp(arg, "dvorak") == 0) {
        *layout_id = LOADKEYS_DVORAK;
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    uint64_t layout_id;
    int keyboard_layout = 0;

    // help message
    if (argc != 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        return (argc == 2) ? 0 : 1;
    }

    // -l for list available layouts 
    if (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "--list") == 0) {
        return print_available_layouts();
    }

    // -c for current layout
    if (strcmp(argv[1], "-c") == 0 || strcmp(argv[1], "--current") == 0) {
        keyboard_layout = sys_system(SYSTEM_CMD_GET_KEYBOARD_LAYOUT, 0, 0, 0, 0);
        printf("Current keyboard layout: %d\n", keyboard_layout);
        return 0;
    }


    if (!parse_layout(argv[1], &layout_id)) {
        printf("loadkeys: unknown layout: %s\n", argv[1]);
        print_usage();
        return 1;
    }

    // set the keyboard layout
    sys_system(SYSTEM_CMD_SET_KEYBOARD_LAYOUT, layout_id, 0, 0, 0);
    return 0;
}

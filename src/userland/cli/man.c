// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Manual pages CLI utility
#include <stdlib.h>
#include <syscall.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("What manual page do you want?\nExample: man ls\n");
        return 0;
    }
    
    char path[128];
    printf("Manual for: %s\n", argv[1]);
    printf("---------------------------\n");
    
    strcpy(path, "/Library/man/");
    strcat(path, argv[1]);
    strcat(path, ".txt");
    
    int fd = sys_open(path, "r");
    if (fd < 0) {
        printf("No manual entry for %s\n", argv[1]);
        return 1;
    }
    
    char buffer[4096];
    int bytes;
    while ((bytes = sys_read(fd, buffer, sizeof(buffer))) > 0) {
        sys_write(1, buffer, bytes);
    }
    
    sys_close(fd);
    printf("\n");
    return 0;
}

// Copyright (c) 2026 janevers (https://github.com/zeyadhost)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

// reverse function
void reverse(char *str) {
    int len = strlen(str);
    for (int i = 0; i < len / 2; i++) {
        char tmp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = tmp;
    }
}

int main(int argc, char *argv[]) {
    // check if there are too much flags
    if (argc != 2) {
        printf("Wrong input, use --help for more info\n");
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    // check if the argument is a file or not by opening it
    if (fp == NULL) {
        reverse(argv[1]);
        printf("%s\n", argv[1]);
        return 0;
    }
    // reverse if flag is a file
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        reverse(line);
        printf("%s\n", line);
    }
    fclose(fp);
    return 0;
}

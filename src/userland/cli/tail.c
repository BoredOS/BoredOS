// Copyright (c) 2026 janevers (https://github.com/janevers-sys)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc == 2) {
        if (strcmp(argv[1], "--help") == 0) {
            printf("-This command reads from the front of the file\n");
            printf("-Use: tail [-n] [-number of lines] [file]\n");
            return 0;
        }
        //print everything
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL) {
            printf("File not found\n");
            return 1;
        }
        char buf[1024];
        while (fgets(buf, sizeof(buf), fp)) {
            printf("%s", buf);
        }
        fclose(fp);
        return 0;
    }
    // only print first lines
    if (argc == 4 && strcmp(argv[1], "-n") == 0) {
        int lines = atoi(argv[2]);
        if (lines <= 0) {
            printf("Number of lines must be positive\n");
            return 1;
        }

        FILE *fp = fopen(argv[3], "r");
        if (fp == NULL) {
            printf("File not found\n");
            return 1;
        }

        char buf[1024];
        while (lines > 0 && fgets(buf, sizeof(buf), fp)) {
            printf("%s", buf);
            lines--;
        }

        fclose(fp);
        return 0;
    }

    printf("Wrong flag, --help for more info\n");
    return 1;
}

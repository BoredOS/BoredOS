// Copyright (c) 2026 janevers — Discord: viper0727 (https://github.com/janevers-sys)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <string.h>
#include "syscall.h"

int type = 0;           // 0 is nothing, 1 is file, 2 is dir
char *name = NULL;      
char *dir = "."; // random char bc it needs to have anything in it

// match exact name
int match(const char *a, const char *b) {
    return strcmp(a, b) == 0; 
}

// check if its a file with fopen
int isfile(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp) { 
        fclose(fp); 
        return 1; }
    return 0;
}

// recursive find
void find(const char *path) {
    FAT32_FileInfo ents[128];
    int n = sys_list(path, ents, 128);
    if (n < 0) return;

    for (int i = 0; i < n; i++) {
        const char *filename = ents[i].name; 
        if (match(filename, ".") || match(filename, "..")) {
            continue;
        }
        char full[512];
        strcpy(full, path);
        strcat(full, "/");
        strcat(full, filename); 
        
        int f = isfile(full); 
        if (match(filename, name)) {
            if (type == 0 || (type == 1 && f) || (type == 2 && !f)) {
                printf("%s\n", full);
            }
        }
        if (!f) {
            find(full);
        }
    }
}

int main(int argc, char *argv[]) {
    // check if user needs help 
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: find [DIR] [-name] [pattern] [-type] [d/f]\n");
        printf("Options:\n");
        printf("-type d:    only read dir\n");
        printf("-type f:    only read files\n");
        return 0;
    }
    // check for flags
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (match(argv[i], "-name") && i+1 < argc) {
                name = argv[++i];
            }
            else if (match(argv[i], "-type") && i+1 < argc) {
                i++;
                if (match(argv[i], "f")) {
                    type = 1;
                }
                else if (match(argv[i], "d")) {
                    type = 2;
                }
                else {
                    printf("-only type f or d\n");
                    return 1;
                }
            }
        }
        // if dir hasnt been set yet
        else if (dir[0] == '.') {
            dir = argv[i];
        }
    }

    if (!name) {
        printf("Wrong usage, --help for more info\n");
        return 1;
    }

    find(dir);
    return 0;
}

// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Show command and system help.
#include <stdlib.h>
#include <syscall.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t help_color = sys_get_shell_config("help_color");
    if (help_color != 0) sys_set_text_color(help_color);

    printf("BoredOS CLI Help\n");
    printf("---------------------------\n");
    printf("ls [path]      - List directory contents\n");
    printf("cd <path>      - Change current directory (built-in)\n");
    printf("pwd            - Print current directory\n");
    printf("mkdir <dir>    - Create directory\n");
    printf("rm <path>      - Remove file or directory\n");
    printf("cat <file>     - Print file contents\n");
    printf("echo [text]    - Print text\n");
    printf("touch <file>   - Create empty file\n");
    printf("cp <src> <dst> - Copy file\n");
    printf("mv <src> <dst> - Move file\n");
    printf("date           - Print current date and time\n");
    printf("uptime         - Print system uptime\n");
    printf("meminfo        - Print memory information\n");
    printf("cowsay [msg]   - Fun cow says something\n");
    printf("beep           - Make a beep sound\n");
    printf("reboot         - Reboot the system\n");
    printf("shutdown       - Shutdown the system\n");
    printf("sysfetch       - Show system information\n");
    printf("cc <file.c>    - C Compiler\n");
    printf("man <cmd>      - Show manual page\n");
    printf("clear          - Clear the screen\n");
    printf("exit           - Exit the terminal\n");
    printf("net            - Network tools\n");
    printf("\nHint: Use Ctrl+C to force quit any running application.\n");
    return 0;
}

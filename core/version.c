// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "version.h"
#include <stddef.h>

#if __has_include("kernel_version.h")
#include "kernel_version.h"
#endif

#ifndef BOREDOS_OS_NAME
#define BOREDOS_OS_NAME "BoredOS"
#endif

#ifndef BOREDOS_KERNEL_NAME
#define BOREDOS_KERNEL_NAME "Boredkernel"
#endif

#ifndef BOREDOS_KERNEL_VERSION
#define BOREDOS_KERNEL_VERSION "2026.09.08-g664f0d46"
#endif

void get_os_info(os_info_t *info) {
    if (!info) return;

    char *p = (char *)info;
    for (size_t i = 0; i < sizeof(os_info_t); i++) p[i] = 0;

    const char *os_name = BOREDOS_OS_NAME;
    const char *kernel_name = BOREDOS_KERNEL_NAME;
    const char *kernel_version = BOREDOS_KERNEL_VERSION;
    const char *build_date = __DATE__;
    const char *build_time = __TIME__;
    const char *build_arch = "x86_64";

    int j;
    j = 0; while (os_name[j] && j < 63) { info->os_name[j] = os_name[j]; j++; } info->os_name[j] = '\0';
    j = 0; while (kernel_name[j] && j < 63) { info->kernel_name[j] = kernel_name[j]; j++; } info->kernel_name[j] = '\0';
    j = 0; while (kernel_version[j] && j < 63) { info->kernel_version[j] = kernel_version[j]; j++; } info->kernel_version[j] = '\0';
    j = 0; while (build_date[j] && j < 63) { info->build_date[j] = build_date[j]; j++; } info->build_date[j] = '\0';
    j = 0; while (build_time[j] && j < 63) { info->build_time[j] = build_time[j]; j++; } info->build_time[j] = '\0';
    j = 0; while (build_arch[j] && j < 63) { info->build_arch[j] = build_arch[j]; j++; } info->build_arch[j] = '\0';
}

// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// wallpaper.h - Wallpaper management for BoredOS
#ifndef WALLPAPER_H
#define WALLPAPER_H

#include <stdint.h>
#include <stdbool.h>

void wallpaper_init(void);
void wallpaper_request_set_from_file(const char *path);
void wallpaper_save_setting(const char *path);
void wallpaper_load_setting(void);
void wallpaper_process_pending(void);
uint32_t* wallpaper_get_pixels(void);
int wallpaper_get_width(void);
int wallpaper_get_height(void);

#endif // WALLPAPER_H

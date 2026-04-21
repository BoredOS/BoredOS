// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Minesweeper puzzle game.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/gnome-mines.png;/Library/images/icons/colloid/applications-games.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "libc/stdlib.h"

#define COLOR_DARK_BG       0xFF121212
#define COLOR_DARK_PANEL    0xFF202020
#define COLOR_DARK_BORDER   0xFF404040
#define COLOR_DARK_TEXT     0xFFE0E0E0
#define COLOR_TRAFFIC_RED   0xFFFF6B6B

// Debugging helper
static void debug_print(const char *msg) {
    sys_write(1, msg, 0);
    int i = 0;
    while (msg[i]) i++;
    sys_write(1, msg, i);
    sys_write(1, "\n", 1);
}

// Game constants
#define GRID_WIDTH 10
#define GRID_HEIGHT 10
#define MINE_COUNT 10
#define CELL_SIZE 20

// Game state
static int grid[GRID_HEIGHT][GRID_WIDTH];  
static bool revealed[GRID_HEIGHT][GRID_WIDTH];
static bool flagged[GRID_HEIGHT][GRID_WIDTH];
static bool game_over = false;
static bool game_won = false;
static int revealed_count = 0;

static uint32_t random_seed = 12345;
static uint32_t random_next(void) {
    random_seed = random_seed * 1103515245 + 12345;
    return (random_seed / 65536) % 32768;
}

static void init_game(void) {
    // Clear arrays
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            grid[y][x] = 0;
            revealed[y][x] = false;
            flagged[y][x] = false;
        }
    }
    
    // Place mines randomly
    int mines_placed = 0;
    while (mines_placed < MINE_COUNT) {
        int x = random_next() % GRID_WIDTH;
        int y = random_next() % GRID_HEIGHT;
        
        if (grid[y][x] != -1) {
            grid[y][x] = -1;
            mines_placed++;
        }
    }
    
    // Calculate adjacent mine counts
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (grid[y][x] != -1) {
                int count = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int ny = y + dy;
                        int nx = x + dx;
                        if (ny >= 0 && ny < GRID_HEIGHT && nx >= 0 && nx < GRID_WIDTH) {
                            if (grid[ny][nx] == -1) count++;
                        }
                    }
                }
                grid[y][x] = count;
            }
        }
    }
    
    game_over = false;
    game_won = false;
    revealed_count = 0;
}

static void flood_fill(int x, int y) {
    if (x < 0 || x >= GRID_WIDTH || y < 0 || y >= GRID_HEIGHT) return;
    if (revealed[y][x] || flagged[y][x]) return;
    if (grid[y][x] == -1) return;
    
    revealed[y][x] = true;
    revealed_count++;
    
    // If cell is empty, reveal adjacent cells
    if (grid[y][x] == 0) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                flood_fill(x + dx, y + dy);
            }
        }
    }
}

static void reveal_cell(int x, int y) {
    if (x < 0 || x >= GRID_WIDTH || y < 0 || y >= GRID_HEIGHT) return;
    if (revealed[y][x] || flagged[y][x]) return;
    
    if (grid[y][x] == -1) {
        // Hit a mine - game over
        game_over = true;
        // Reveal all mines
        for (int yy = 0; yy < GRID_HEIGHT; yy++) {
            for (int xx = 0; xx < GRID_WIDTH; xx++) {
                if (grid[yy][xx] == -1) {
                    revealed[yy][xx] = true;
                }
            }
        }
    } else if (grid[y][x] == 0) {
        flood_fill(x, y);
    } else {
        revealed[y][x] = true;
        revealed_count++;
    }
    
    // Check win condition
    if (revealed_count == (GRID_WIDTH * GRID_HEIGHT - MINE_COUNT)) {
        game_won = true;
    }
}

static void flag_cell(int x, int y) {
    if (x < 0 || x >= GRID_WIDTH || y < 0 || y >= GRID_HEIGHT) return;
    if (revealed[y][x]) return;
    
    flagged[y][x] = !flagged[y][x];
}

static void minesweeper_paint(ui_window_t win) {
    int win_w = 240, win_h = 340;
    
    ui_draw_rect(win, 4, 0, win_w - 8, win_h, COLOR_DARK_BG);
    
    if (game_over) {
        ui_draw_string(win, 10, 4, "Game Over!", COLOR_TRAFFIC_RED);
    } else if (game_won) {
        ui_draw_string(win, 10, 4, "You Won!", 0xFF00FF00);  // Bright green
    } else {
        ui_draw_string(win, 10, 4, "", COLOR_DARK_TEXT);
    }
    
    // Draw grid
    int grid_start_x = 10;
    int grid_start_y = 22;
    
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int px = grid_start_x + x * CELL_SIZE;
            int py = grid_start_y + y * CELL_SIZE;
            
            if (revealed[y][x]) {
                // Revealed cell - dark mode
                ui_draw_rounded_rect_filled(win, px, py, CELL_SIZE, CELL_SIZE, 2, COLOR_DARK_PANEL);
                
                if (grid[y][x] == -1) {
                    // Mine
                    ui_draw_string(win, px + 8, py + 6, "*", COLOR_TRAFFIC_RED);
                } else if (grid[y][x] > 0) {
                    // Number
                    char num[2] = { '0' + grid[y][x], 0 };
                    ui_draw_string(win, px + 8, py + 6, num, COLOR_DARK_TEXT);
                }
            } else {
                // Unrevealed cell
                ui_draw_rounded_rect_filled(win, px, py, CELL_SIZE, CELL_SIZE, 2, COLOR_DARK_BORDER);
                
                if (flagged[y][x]) {
                    ui_draw_string(win, px + 7, py + 6, "F", COLOR_TRAFFIC_RED);
                }
            }
        }
    }
    
    // Draw new game button
    int btn_y = grid_start_y + GRID_HEIGHT * CELL_SIZE + 10;
    ui_draw_rounded_rect_filled(win, grid_start_x, btn_y, 70, 24, 4, COLOR_DARK_BORDER);
    ui_draw_string(win, grid_start_x + 6, btn_y + 8, "New Game", COLOR_DARK_TEXT);
}

static void minesweeper_handle_click(ui_window_t win, int x, int y, int button) {
    int grid_start_x = 10;
    int grid_start_y = 22;
    int btn_y = grid_start_y + GRID_HEIGHT * CELL_SIZE + 10;
    
    // Check "New Game" button
    if (x >= grid_start_x && x < grid_start_x + 70 &&
        y >= btn_y && y < btn_y + 24) {
        init_game();
        return;
    }
    
    // Check grid cells
    if (x >= grid_start_x && x < grid_start_x + GRID_WIDTH * CELL_SIZE &&
        y >= grid_start_y && y < grid_start_y + GRID_HEIGHT * CELL_SIZE) {
        
        if (game_over || game_won) return;
        
        int cell_x = (x - grid_start_x) / CELL_SIZE;
        int cell_y = (y - grid_start_y) / CELL_SIZE;
        
        if (button == GUI_EVENT_RIGHT_CLICK) {
            flag_cell(cell_x, cell_y);
        } else {
            reveal_cell(cell_x, cell_y);
        }
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ui_window_t win = ui_window_create("Minesweeper", 250, 100, 240, 340);
    if (!win) return 1;

    random_seed = 987654321;
    init_game();

    gui_event_t ev;
    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                minesweeper_paint(win);
                ui_mark_dirty(win, 0, 0, 240, 320);
            } else if (ev.type == GUI_EVENT_CLICK) {
                minesweeper_handle_click(win, ev.arg1, ev.arg2, ev.type);
                minesweeper_paint(win);
                ui_mark_dirty(win, 0, 0, 240, 320);
            } else if (ev.type == GUI_EVENT_RIGHT_CLICK) {
                minesweeper_handle_click(win, ev.arg1, ev.arg2, ev.type);
                minesweeper_paint(win);
                ui_mark_dirty(win, 0, 0, 240, 320);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        }
    }
    return 0;
}

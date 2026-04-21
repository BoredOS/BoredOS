// BOREDOS_APP_DESC: 2048 number puzzle game.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/applications-games.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "libc/input.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
@Lluciocc
2048 for BoredOS
Controls:
- WASD keys or arrow keys or numpad keys to move
- R key to restart
*/

#define WINDOW_W 300
#define WINDOW_H 430

#define BOARD_SIZE 4
#define TILE_SIZE 56
#define TILE_GAP 8
#define BOARD_X 18
#define BOARD_Y 96

#define BTN_W 54
#define BTN_H 30

#define COLOR_BG            0xFF121212
#define COLOR_PANEL         0xFF202020
#define COLOR_PANEL_2       0xFF2A2A2A
#define COLOR_BORDER        0xFF3D3D3D
#define COLOR_TEXT          0xFFF2F2F2
#define COLOR_TEXT_DARK     0xFF202020
#define COLOR_MUTED         0xFFBBBBBB
#define COLOR_ACCENT        0xFF6EA8FE
#define COLOR_GREEN         0xFF69DB7C
#define COLOR_RED           0xFFFF6B6B
#define COLOR_EMPTY_TILE    0xFF2D2D2D

static int board[BOARD_SIZE][BOARD_SIZE];
static int score = 0;
static int best_tile = 0;
static bool game_over = false;
static bool game_won = false;
static bool has_moved_last_turn = false;

static uint32_t random_seed = 0xC0FFEE12u;

static uint32_t random_next(void) {
    random_seed = random_seed * 1664525u + 1013904223u;
    return random_seed;
}

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

static void clear_board(void) {
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            board[y][x] = 0;
        }
    }
}

static int count_empty_cells(void) {
    int count = 0;
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (board[y][x] == 0) {
                count++;
            }
        }
    }
    return count;
}

static void add_random_tile(void) {
    int empty_count = count_empty_cells();
    if (empty_count <= 0) {
        return;
    }

    int pick = (int)(random_next() % (uint32_t)empty_count);

    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (board[y][x] == 0) {
                if (pick == 0) {
                    /* 90% chance of a 2, 10% chance of a 4 */
                    board[y][x] = ((random_next() % 10u) == 0u) ? 4 : 2;
                    best_tile = max_int(best_tile, board[y][x]);
                    return;
                }
                pick--;
            }
        }
    }
}

static bool can_make_any_move(void) {
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (board[y][x] == 0) {
                return true;
            }
            if (x + 1 < BOARD_SIZE && board[y][x] == board[y][x + 1]) {
                return true;
            }
            if (y + 1 < BOARD_SIZE && board[y][x] == board[y + 1][x]) {
                return true;
            }
        }
    }
    return false;
}

static void refresh_end_state(void) {
    game_won = false;
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (board[y][x] >= 2048) {
                game_won = true;
            }
            if (board[y][x] > best_tile) {
                best_tile = board[y][x];
            }
        }
    }

    game_over = !can_make_any_move();
}

static void init_game(void) {
    clear_board();
    score = 0;
    best_tile = 0;
    game_over = false;
    game_won = false;
    has_moved_last_turn = false;

    add_random_tile();
    add_random_tile();
    refresh_end_state();
}

static void copy_line_from_row(int row, int out[BOARD_SIZE]) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        out[i] = board[row][i];
    }
}

static void copy_line_to_row(int row, const int in[BOARD_SIZE]) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        board[row][i] = in[i];
    }
}

static void copy_line_from_col(int col, int out[BOARD_SIZE]) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        out[i] = board[i][col];
    }
}

static void copy_line_to_col(int col, const int in[BOARD_SIZE]) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        board[i][col] = in[i];
    }
}

static void reverse_line(int line[BOARD_SIZE]) {
    for (int i = 0; i < BOARD_SIZE / 2; i++) {
        int tmp = line[i];
        line[i] = line[BOARD_SIZE - 1 - i];
        line[BOARD_SIZE - 1 - i] = tmp;
    }
}

static bool slide_and_merge_line_left(int line[BOARD_SIZE]) {
    int compact[BOARD_SIZE];
    int merged[BOARD_SIZE];
    int compact_len = 0;
    int write_index = 0;
    bool changed = false;

    for (int i = 0; i < BOARD_SIZE; i++) {
        merged[i] = 0;
        if (line[i] != 0) {
            compact[compact_len++] = line[i];
        }
    }

    for (int i = 0; i < compact_len; i++) {
        if (i + 1 < compact_len && compact[i] == compact[i + 1]) {
            int value = compact[i] * 2;
            merged[write_index++] = value;
            score += value;
            best_tile = max_int(best_tile, value);
            i++; /* Skip the second tile, it has been merged. */
        } else {
            merged[write_index++] = compact[i];
        }
    }

    while (write_index < BOARD_SIZE) {
        merged[write_index++] = 0;
    }

    for (int i = 0; i < BOARD_SIZE; i++) {
        if (line[i] != merged[i]) {
            changed = true;
        }
        line[i] = merged[i];
    }

    return changed;
}

typedef enum {
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UP,
    MOVE_DOWN
} move_dir_t;

static bool apply_move(move_dir_t dir) {
    bool changed = false;

    if (game_over) {
        return false;
    }

    if (dir == MOVE_LEFT || dir == MOVE_RIGHT) {
        for (int row = 0; row < BOARD_SIZE; row++) {
            int line[BOARD_SIZE];
            copy_line_from_row(row, line);

            if (dir == MOVE_RIGHT) {
                reverse_line(line);
            }

            if (slide_and_merge_line_left(line)) {
                changed = true;
            }

            if (dir == MOVE_RIGHT) {
                reverse_line(line);
            }

            copy_line_to_row(row, line);
        }
    } else {
        for (int col = 0; col < BOARD_SIZE; col++) {
            int line[BOARD_SIZE];
            copy_line_from_col(col, line);

            if (dir == MOVE_DOWN) {
                reverse_line(line);
            }

            if (slide_and_merge_line_left(line)) {
                changed = true;
            }

            if (dir == MOVE_DOWN) {
                reverse_line(line);
            }

            copy_line_to_col(col, line);
        }
    }

    if (changed) {
        add_random_tile();
    }

    has_moved_last_turn = changed;
    refresh_end_state();
    return changed;
}

static uint32_t get_tile_color(int value) {
    switch (value) {
        case 0:    return COLOR_EMPTY_TILE;
        case 2:    return 0xFFEEE4DA;
        case 4:    return 0xFFEDE0C8;
        case 8:    return 0xFFF2B179;
        case 16:   return 0xFFF59563;
        case 32:   return 0xFFF67C5F;
        case 64:   return 0xFFF65E3B;
        case 128:  return 0xFFEDCF72;
        case 256:  return 0xFFEDCC61;
        case 512:  return 0xFFEDC850;
        case 1024: return 0xFFEDC53F;
        case 2048: return 0xFFEDC22E;
        default:   return 0xFF3C91E6;
    }
}

static uint32_t get_tile_text_color(int value) {
    (void)value;
    return 0xFF000000; // for visibility
}

static void int_to_text(int value, char *out) {
    itoa(value, out);
}

static void draw_centered_text(ui_window_t win, int x, int y, int w, int h,
                               const char *text, uint32_t color, float scale) {
    (void)scale;

    uint32_t text_w = ui_get_string_width(text);
    uint32_t text_h = ui_get_font_height();

    int draw_x = x + (w - (int)text_w) / 2;
    int draw_y = y + (h - (int)text_h) / 2;

    ui_draw_string(win, draw_x, draw_y, text, color);
}

static void draw_button(ui_window_t win, int x, int y, int w, int h,
                        const char *label, uint32_t color) {
    ui_draw_rounded_rect_filled(win, x, y, w, h, 6, color);
    draw_centered_text(win, x, y, w, h, label, COLOR_TEXT, 1.0f);
}

static void draw_score_box(ui_window_t win, int x, int y, int w, int h,
                           const char *title, int value) {
    char buf[16];
    int_to_text(value, buf);

    ui_draw_rounded_rect_filled(win, x, y, w, h, 8, COLOR_PANEL_2);
    draw_centered_text(win, x, y + 3, w, 14, title, COLOR_MUTED, 0.8f);
    draw_centered_text(win, x, y + 16, w, 18, buf, COLOR_TEXT, 1.0f);
}

static void draw_tile(ui_window_t win, int x, int y, int value) {
    char buf[16];
    float scale = 1.6f;

    ui_draw_rounded_rect_filled(win, x, y, TILE_SIZE, TILE_SIZE, 8, get_tile_color(value));

    if (value == 0) {
        return;
    }

    int_to_text(value, buf);

    if (value >= 1000) {
        scale = 1.2f;
    } else if (value >= 100) {
        scale = 1.4f;
    }

    draw_centered_text(
        win,
        x, y,
        TILE_SIZE, TILE_SIZE,
        buf,
        0xFF000000,
        scale
    );
}

static void draw_board(ui_window_t win) {
    int board_w = BOARD_SIZE * TILE_SIZE + (BOARD_SIZE + 1) * TILE_GAP;
    int board_h = board_w;

    ui_draw_rounded_rect_filled(win, BOARD_X, BOARD_Y, board_w, board_h, 10, COLOR_PANEL_2);

    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            int px = BOARD_X + TILE_GAP + x * (TILE_SIZE + TILE_GAP);
            int py = BOARD_Y + TILE_GAP + y * (TILE_SIZE + TILE_GAP);
            draw_tile(win, px, py, board[y][x]);
        }
    }
}

static void draw_status(ui_window_t win) {
    if (game_over) {
        ui_draw_string(win, 18, 70, "Game over", COLOR_RED);
    } else if (game_won) {
        ui_draw_string(win, 18, 70, "2048 reached - keep going", COLOR_GREEN);
    } else {
        ui_draw_string(win, 18, 70, "Combine tiles to reach 2048", COLOR_MUTED);
    }
}

static void game_paint(ui_window_t win) {
    ui_draw_rect(win, 0, 0, WINDOW_W, WINDOW_H, COLOR_BG);

    ui_draw_string_scaled(win, 18, 12, "2048", COLOR_TEXT, 1.6f);
    ui_draw_string(win, 18, 42, "Use WASD keys", COLOR_MUTED);

    draw_score_box(win, 155, 14, 56, 40, "SCORE", score);
    draw_score_box(win, 220, 14, 62, 40, "BEST", best_tile);
    draw_status(win);

    draw_board(win);
}

static bool point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static bool is_left_key(int key) {
    return key == 'a' || key == 'A' || key == KEY_LEFT;
}

static bool is_right_key(int key) {
    return key == 'd' || key == 'D' || key == KEY_RIGHT;
}

static bool is_up_key(int key) {
    return key == 'w' || key == 'W' || key == KEY_UP;
}

static bool is_down_key(int key) {
    return key == 's' || key == 'S' || key == KEY_DOWN;
}

static void handle_click(int x, int y) {
    if (point_in_rect(x, y, 18, 352, 86, 30)) {
        init_game();
        return;
    }
    if (point_in_rect(x, y, 123, 352, BTN_W, BTN_H)) {
        apply_move(MOVE_LEFT);
        return;
    }
    if (point_in_rect(x, y, 186, 352, BTN_W, BTN_H)) {
        apply_move(MOVE_RIGHT);
        return;
    }
    if (point_in_rect(x, y, 92, 390, BTN_W, BTN_H)) {
        apply_move(MOVE_UP);
        return;
    }
    if (point_in_rect(x, y, 155, 390, BTN_W, BTN_H)) {
        apply_move(MOVE_DOWN);
        return;
    }
}

static void handle_key(int key) {
    if (key == 'r' || key == 'R') {
        init_game();
        return;
    }

    if (is_left_key(key)) {
        apply_move(MOVE_LEFT);
    } else if (is_right_key(key)) {
        apply_move(MOVE_RIGHT);
    } else if (is_up_key(key)) {
        apply_move(MOVE_UP);
    } else if (is_down_key(key)) {
        apply_move(MOVE_DOWN);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    ui_window_t win = ui_window_create("2048", 240, 120, WINDOW_W, WINDOW_H);
    if (!win) {
        return 1;
    }

    init_game();

    gui_event_t ev;
    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                game_paint(win);
                ui_mark_dirty(win, 0, 0, WINDOW_W, WINDOW_H);
            } else if (ev.type == GUI_EVENT_CLICK) {
                handle_click(ev.arg1, ev.arg2);
                game_paint(win);
                ui_mark_dirty(win, 0, 0, WINDOW_W, WINDOW_H);
            } else if (ev.type == GUI_EVENT_KEY) {
                handle_key(ev.arg1);
                game_paint(win);
                ui_mark_dirty(win, 0, 0, WINDOW_W, WINDOW_H);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            } else if (ev.type == GUI_EVENT_RESIZE) {
                game_paint(win);
                ui_mark_dirty(win, 0, 0, WINDOW_W, WINDOW_H);
            }
        } else {
            sys_yield();
        }
    }

    return 0;
}

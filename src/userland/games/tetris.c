// Copyright (c) 2026 RicePollution (https://github.com/RicePollution)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Classic Tetris puzzle game.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/quadrapassel.png

#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "libc/input.h"
#include <stdbool.h>
#include <stdint.h>

// Layout

#define CELL       14
#define GAP        1
#define STEP       (CELL + GAP)

#define COLS       10
#define ROWS       20

#define BOARD_PW   (COLS * STEP - GAP)
#define BOARD_PH   (ROWS * STEP - GAP)

#define BOARD_X    10
#define BOARD_Y    48

#define PANEL_X    (BOARD_X + BOARD_PW + 14)
#define PANEL_Y    BOARD_Y
#define PANEL_W    74

#define WIN_W      (PANEL_X + PANEL_W + 10)
#define WIN_H      (BOARD_Y + BOARD_PH + 30)

// colors

#define C_BG       0xFF121212
#define C_TEXT     0xFFEAEAEA
#define C_MUTED    0xFFBBBBBB
#define C_PANEL    0xFF2A2A2A
#define C_EMPTY    0xFF1E1E1E
#define C_RED      0xFFFF6B6B
#define C_GHOST    0xFF333340

#define C_I  0xFF00CFCF
#define C_O  0xFFCFCF00
#define C_T  0xFFAA00CF
#define C_S  0xFF00CF00
#define C_Z  0xFFCF0000
#define C_J  0xFF3333FF
#define C_L  0xFFCF6F00

// Piece data

typedef struct { int r, c; } cell_t;

static const cell_t SHAPES[7][4][4] = {
    // I
    {
        {{1,0},{1,1},{1,2},{1,3}},
        {{0,2},{1,2},{2,2},{3,2}},
        {{2,0},{2,1},{2,2},{2,3}},
        {{0,1},{1,1},{2,1},{3,1}},
    },
    // O
    {
        {{0,1},{0,2},{1,1},{1,2}},
        {{0,1},{0,2},{1,1},{1,2}},
        {{0,1},{0,2},{1,1},{1,2}},
        {{0,1},{0,2},{1,1},{1,2}},
    },
    // T
    {
        {{0,1},{1,0},{1,1},{1,2}},
        {{0,1},{1,1},{1,2},{2,1}},
        {{1,0},{1,1},{1,2},{2,1}},
        {{0,1},{1,0},{1,1},{2,1}},
    },
    // S
    {
        {{0,1},{0,2},{1,0},{1,1}},
        {{0,1},{1,1},{1,2},{2,2}},
        {{1,1},{1,2},{2,0},{2,1}},
        {{0,0},{1,0},{1,1},{2,1}},
    },
    // Z
    {
        {{0,0},{0,1},{1,1},{1,2}},
        {{0,2},{1,1},{1,2},{2,1}},
        {{1,0},{1,1},{2,1},{2,2}},
        {{0,1},{1,0},{1,1},{2,0}},
    },
    // J
    {
        {{0,0},{1,0},{1,1},{1,2}},
        {{0,1},{0,2},{1,1},{2,1}},
        {{1,0},{1,1},{1,2},{2,2}},
        {{0,1},{1,1},{2,0},{2,1}},
    },
    // L
    {
        {{0,2},{1,0},{1,1},{1,2}},
        {{0,1},{1,1},{2,1},{2,2}},
        {{1,0},{1,1},{1,2},{2,0}},
        {{0,0},{0,1},{1,1},{2,1}},
    },
};

static const uint32_t COLORS[7] = { C_I, C_O, C_T, C_S, C_Z, C_J, C_L };

// SRS wall kick offsets (dx, dy) — +y = down
static const int KICK_JLSTZ[4][5][2] = {
    {{ 0, 0},{-1, 0},{-1,-1},{ 0, 2},{-1, 2}},
    {{ 0, 0},{ 1, 0},{ 1, 1},{ 0,-2},{ 1,-2}},
    {{ 0, 0},{ 1, 0},{ 1,-1},{ 0, 2},{ 1, 2}},
    {{ 0, 0},{-1, 0},{-1, 1},{ 0,-2},{-1,-2}},
};

static const int KICK_I[4][5][2] = {
    {{ 0, 0},{-2, 0},{ 1, 0},{-2, 1},{ 1,-2}},
    {{ 0, 0},{-1, 0},{ 2, 0},{-1,-2},{ 2, 1}},
    {{ 0, 0},{ 2, 0},{-1, 0},{ 2,-1},{-1, 2}},
    {{ 0, 0},{ 1, 0},{-2, 0},{ 1, 2},{-2,-1}},
};

// Game state

static uint32_t board[ROWS][COLS];

static int  cur_type, cur_rot, cur_x, cur_y;
static int  next_type;
static bool game_over, game_started;
static int  score, best_score, level, lines_cleared;
static int  drop_speed_ms;

static int elapsed_ms;

static uint32_t rng;

static void rng_seed(void) {
    // Seed from system tick counter so each game is different
    rng = (uint32_t)sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
    if (rng == 0) rng = 0xDEADBEEFu;
}

static uint32_t rng_next(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

// 7-bag randomizer
static int bag[7];
static int bag_pos = 7;

static void bag_shuffle(void) {
    for (int i = 0; i < 7; i++) bag[i] = i;
    for (int i = 6; i > 0; i--) {
        int j = (int)(rng_next() % (uint32_t)(i + 1));
        int tmp = bag[i]; bag[i] = bag[j]; bag[j] = tmp;
    }
    bag_pos = 0;
}

static int bag_next(void) {
    if (bag_pos >= 7) bag_shuffle();
    return bag[bag_pos++];
}

// Collision

static bool fits(int type, int rot, int px, int py) {
    for (int i = 0; i < 4; i++) {
        int r = py + SHAPES[type][rot][i].r;
        int c = px + SHAPES[type][rot][i].c;
        if (c < 0 || c >= COLS || r >= ROWS) return false;
        if (r >= 0 && board[r][c]) return false;
    }
    return true;
}

static void lock_piece(void) {
    uint32_t color = COLORS[cur_type];
    for (int i = 0; i < 4; i++) {
        int r = cur_y + SHAPES[cur_type][cur_rot][i].r;
        int c = cur_x + SHAPES[cur_type][cur_rot][i].c;
        if (r >= 0 && r < ROWS && c >= 0 && c < COLS)
            board[r][c] = color;
    }
}

static int clear_lines(void) {
    int cleared = 0;
    for (int r = ROWS - 1; r >= 0; r--) {
        bool full = true;
        for (int c = 0; c < COLS; c++)
            if (!board[r][c]) { full = false; break; }
        if (full) {
            for (int rr = r; rr > 0; rr--)
                for (int c = 0; c < COLS; c++)
                    board[rr][c] = board[rr - 1][c];
            for (int c = 0; c < COLS; c++)
                board[0][c] = 0;
            cleared++;
            r++;
        }
    }
    return cleared;
}

static int max_int(int a, int b) { return a > b ? a : b; }

static void update_score(int cleared) {
    static const int pts[5] = { 0, 100, 300, 500, 800 };
    if (cleared > 4) cleared = 4;
    score += pts[cleared] * (level + 1);
    lines_cleared += cleared;
    level = lines_cleared / 10;
    // Speed: start 500ms, get faster with level
    drop_speed_ms = 500 - level * 40;
    if (drop_speed_ms < 80) drop_speed_ms = 80;
    best_score = max_int(best_score, score);
}

static void spawn_piece(void) {
    cur_type = next_type;
    cur_rot  = 0;
    cur_x    = COLS / 2 - (cur_type == 0 ? 2 : 1);
    cur_y    = -1;
    next_type = bag_next();
    elapsed_ms = 0;
    if (!fits(cur_type, cur_rot, cur_x, cur_y)) {
        game_over = true;
        best_score = max_int(best_score, score);
    }
}

static void init_game(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            board[r][c] = 0;
    score = 0;
    level = 0;
    lines_cleared = 0;
    drop_speed_ms = 500;
    elapsed_ms = 0;
    game_over = false;
    game_started = true;
    bag_pos = 7;
    rng_seed();
    next_type = bag_next();
    spawn_piece();
}

static int ghost_y(void) {
    int gy = cur_y;
    while (fits(cur_type, cur_rot, cur_x, gy + 1)) gy++;
    return gy;
}

// Drawing

static void draw_cell_px(ui_window_t win, int px, int py, uint32_t color) {
    ui_draw_rounded_rect_filled(win, px, py, CELL, CELL, 2, color);
}

static void draw_board(ui_window_t win) {
    ui_draw_rounded_rect_filled(win, BOARD_X - 4, BOARD_Y - 4,
        BOARD_PW + 8, BOARD_PH + 8, 6, C_PANEL);

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            draw_cell_px(win, BOARD_X + c * STEP, BOARD_Y + r * STEP,
                board[r][c] ? board[r][c] : C_EMPTY);
}

static void draw_active(ui_window_t win, int type, int rot, int px, int py, uint32_t color) {
    for (int i = 0; i < 4; i++) {
        int r = py + SHAPES[type][rot][i].r;
        int c = px + SHAPES[type][rot][i].c;
        if (r < 0) continue;
        draw_cell_px(win, BOARD_X + c * STEP, BOARD_Y + r * STEP, color);
    }
}

static void draw_mini(ui_window_t win, int type, int ox, int oy) {
    int sz = 10;
    uint32_t color = COLORS[type];
    for (int i = 0; i < 4; i++) {
        int pr = SHAPES[type][0][i].r;
        int pc = SHAPES[type][0][i].c;
        ui_draw_rounded_rect_filled(win, ox + pc * (sz + 1), oy + pr * (sz + 1),
            sz, sz, 2, color);
    }
}

static void draw_info(ui_window_t win, int x, int y, const char *label, int val) {
    char buf[16];
    itoa(val, buf);
    ui_draw_rounded_rect_filled(win, x, y, PANEL_W, 34, 6, C_PANEL);
    ui_draw_string(win, x + 6, y + 3, label, C_MUTED);
    ui_draw_string(win, x + 6, y + 18, buf, C_TEXT);
}

static void game_paint(ui_window_t win) {
    ui_draw_rect(win, 0, 0, WIN_W, WIN_H, C_BG);

    {
        uint32_t tw = ui_get_string_width("Tetris");
        ui_draw_string(win, (WIN_W - (int)tw) / 2, 6, "Tetris", C_TEXT);
    }

    if (game_over)
        ui_draw_string(win, BOARD_X, 28, "Game Over!  R = restart", C_RED);
    else
        ui_draw_string(win, BOARD_X, 28, "WASD/Arrows Space=drop R=reset", C_MUTED);

    draw_board(win);

    if (!game_over && game_started) {
        int gy = ghost_y();
        if (gy != cur_y)
            draw_active(win, cur_type, cur_rot, cur_x, gy, C_GHOST);
        draw_active(win, cur_type, cur_rot, cur_x, cur_y, COLORS[cur_type]);
    }

    // Side panel — next piece
    ui_draw_string(win, PANEL_X, PANEL_Y, "Next", C_MUTED);
    ui_draw_rounded_rect_filled(win, PANEL_X, PANEL_Y + 16, PANEL_W, 52, 4, C_PANEL);
    draw_mini(win, next_type, PANEL_X + 8, PANEL_Y + 22);

    // Side panel — stats
    int sy = PANEL_Y + 76;
    draw_info(win, PANEL_X, sy,       "Score", score);
    draw_info(win, PANEL_X, sy + 42,  "Best",  best_score);
    draw_info(win, PANEL_X, sy + 84,  "Level", level);
    draw_info(win, PANEL_X, sy + 126, "Lines", lines_cleared);
}

// Input

static bool try_rotate(void) {
    int new_rot = (cur_rot + 1) % 4;
    const int (*kicks)[2];
    if (cur_type == 0)
        kicks = KICK_I[cur_rot];
    else
        kicks = KICK_JLSTZ[cur_rot];

    for (int k = 0; k < 5; k++) {
        int nx = cur_x + kicks[k][0];
        int ny = cur_y + kicks[k][1];
        if (fits(cur_type, new_rot, nx, ny)) {
            cur_x = nx;
            cur_y = ny;
            cur_rot = new_rot;
            return true;
        }
    }
    return false;
}

static void handle_key(int key) {
    if (key == 'r' || key == 'R') { init_game(); return; }
    if (game_over || !game_started) return;

    if (key == KEY_LEFT || key == 'a' || key == 'A') {
        if (fits(cur_type, cur_rot, cur_x - 1, cur_y)) cur_x--;
    } else if (key == KEY_RIGHT || key == 'd' || key == 'D') {
        if (fits(cur_type, cur_rot, cur_x + 1, cur_y)) cur_x++;
    } else if (key == KEY_DOWN || key == 's' || key == 'S') {
        if (fits(cur_type, cur_rot, cur_x, cur_y + 1)) cur_y++;
    } else if (key == KEY_UP || key == 'w' || key == 'W') {
        try_rotate();
    } else if (key == ' ') {
        cur_y = ghost_y();
        lock_piece();
        update_score(clear_lines());
        spawn_piece();
    }
}

// Game tick (gravity)

static void game_tick(void) {
    if (game_over || !game_started) return;

    if (fits(cur_type, cur_rot, cur_x, cur_y + 1)) {
        cur_y++;
    } else {
        lock_piece();
        update_score(clear_lines());
        spawn_piece();
    }
}

// Main loop
// Follows snake.c exactly: drain events, move, paint, sleep.
// Snake calls move_snake_step() every iteration with sleep(80).
// Tetris does the same but use sleep(20) and accumulate ms for the drop timer.

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    ui_window_t win = ui_window_create("Tetris", 200, 80, WIN_W, WIN_H);
    if (!win) return 1;

    ui_window_set_resizable(win, false);
    init_game();

    gui_event_t ev;

    while (1) {
        // Drain events — do NOT paint here to avoid paint-event feedback loop
        while (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_KEY) {
                handle_key(ev.arg1);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
            // GUI_EVENT_PAINT is handled by the repaint below
        }

        // Gravity
        elapsed_ms += 20;
        if (elapsed_ms >= drop_speed_ms) {
            elapsed_ms = 0;
            game_tick();
        }

        // Always repaint + mark dirty
        game_paint(win);
        ui_mark_dirty(win, 0, 0, WIN_W, WIN_H);
        sleep(20);
    }

    return 0;
}

// BOREDOS_APP_DESC: Classic snake arcade game.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/cartridges.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "libc/input.h"
#include "libc/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* @Lluciocc Snake for BoredOS
   Controls:
   - WASD keys or arrow keys or numpad keys to move
   - R key to restart
*/

#define WINDOW_W 340
#define WINDOW_H 420

#define GRID_W 18
#define GRID_H 18
#define CELL_SIZE 16

#define BOARD_X 26
#define BOARD_Y 70
#define BOARD_W (GRID_W * CELL_SIZE)
#define BOARD_H (GRID_H * CELL_SIZE)

#define MAX_SNAKE_LEN (GRID_W * GRID_H)

#define GAME_TICK_DELAY 6

#define COLOR_BG 0xFF121212
#define COLOR_PANEL 0xFF202020
#define COLOR_PANEL_2 0xFF2A2A2A
#define COLOR_BORDER 0xFF3D3D3D
#define COLOR_TEXT 0xFFEAEAEA
#define COLOR_MUTED 0xFFBBBBBB
#define COLOR_ACCENT 0xFF6EA8FE
#define COLOR_GREEN 0xFF69DB7C
#define COLOR_RED 0xFFFF6B6B
#define COLOR_FOOD 0xFFFF8FA3
#define COLOR_SNAKE_HEAD 0xFF7CFC8A
#define COLOR_SNAKE_BODY 0xFF43C463
#define COLOR_GRID_CELL 0xFF262626

typedef struct {
    int x;
    int y;
} point_t;

typedef enum {
    DIR_UP,
    DIR_RIGHT,
    DIR_DOWN,
    DIR_LEFT
} direction_t;

static point_t snake[MAX_SNAKE_LEN];
static int snake_len = 0;

static point_t food;

static direction_t current_dir = DIR_RIGHT;
static direction_t next_dir = DIR_RIGHT;

static bool game_over = false;
static bool game_started = false;

static int score = 0;
static int best_score = 0;

/* poor-man timer */
static uint32_t tick_counter = 0;
static int game_speed_ms = 80; // default (milliseconds per move)

static uint32_t random_seed = 0x1234ABCDu;

static uint32_t random_next(void) {
    random_seed = random_seed * 1664525u + 1013904223u;
    return random_seed;
}

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

static bool point_equals(point_t a, point_t b) {
    return a.x == b.x && a.y == b.y;
}

static bool snake_occupies_cell(int x, int y) {
    for (int i = 0; i < snake_len; i++) {
        if (snake[i].x == x && snake[i].y == y) {
            return true;
        }
    }
    return false;
}

static void spawn_food(void) {
    int free_count = 0;

    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (!snake_occupies_cell(x, y)) {
                free_count++;
            }
        }
    }

    if (free_count <= 0) {
        game_over = true;
        return;
    }

    int pick = (int)(random_next() % (uint32_t)free_count);

    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (!snake_occupies_cell(x, y)) {
                if (pick == 0) {
                    food.x = x;
                    food.y = y;
                    return;
                }
                pick--;
            }
        }
    }
}

static void init_game(void) {
    snake_len = 3;

    snake[0].x = GRID_W / 2;
    snake[0].y = GRID_H / 2;

    snake[1].x = snake[0].x - 1;
    snake[1].y = snake[0].y;

    snake[2].x = snake[0].x - 2;
    snake[2].y = snake[0].y;

    current_dir = DIR_RIGHT;
    next_dir = DIR_RIGHT;

    score = 0;
    game_over = false;
    game_started = true;

    tick_counter = 0;

    spawn_food();
}

static bool is_opposite_direction(direction_t a, direction_t b) {
    return (a == DIR_UP && b == DIR_DOWN) ||
           (a == DIR_DOWN && b == DIR_UP) ||
           (a == DIR_LEFT && b == DIR_RIGHT) ||
           (a == DIR_RIGHT && b == DIR_LEFT);
}

static void request_direction(direction_t dir) {
    if (snake_len > 1 && is_opposite_direction(current_dir, dir)) {
        return;
    }
    next_dir = dir;
}

static void move_snake_step(void) {
    if (!game_started || game_over) {
        return;
    }

    current_dir = next_dir;

    point_t new_head = snake[0];

    if (current_dir == DIR_UP) new_head.y--;
    else if (current_dir == DIR_RIGHT) new_head.x++;
    else if (current_dir == DIR_DOWN) new_head.y++;
    else if (current_dir == DIR_LEFT) new_head.x--;

    if (new_head.x < 0 || new_head.x >= GRID_W ||
        new_head.y < 0 || new_head.y >= GRID_H) {
        game_over = true;
        best_score = max_int(best_score, score);
        return;
    }

    bool grows = point_equals(new_head, food);
    int collision_check_len = grows ? snake_len : (snake_len - 1);

    for (int i = 0; i < collision_check_len; i++) {
        if (point_equals(new_head, snake[i])) {
            game_over = true;
            best_score = max_int(best_score, score);
            return;
        }
    }

    int old_len = snake_len;

    if (grows && snake_len < MAX_SNAKE_LEN) {
        snake_len++;
    }

    for (int i = snake_len - 1; i > 0; i--) {
        if (i - 1 < old_len) {
            snake[i] = snake[i - 1];
        }
    }

    snake[0] = new_head;

    if (grows) {
        score++;
        best_score = max_int(best_score, score);
        spawn_food();
    }
}

static void int_to_text(int value, char *out) {
    itoa(value, out);
}

static bool point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

/* Input helpers */

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


static void draw_board(ui_window_t win) {
    ui_draw_rounded_rect_filled(
        win,
        BOARD_X - 6,
        BOARD_Y - 6,
        BOARD_W + 12,
        BOARD_H + 12,
        8,
        COLOR_PANEL_2
    );

    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            int px = BOARD_X + x * CELL_SIZE;
            int py = BOARD_Y + y * CELL_SIZE;

            ui_draw_rect(win, px, py, CELL_SIZE - 1, CELL_SIZE - 1, COLOR_GRID_CELL);
        }
    }

    int fx = BOARD_X + food.x * CELL_SIZE;
    int fy = BOARD_Y + food.y * CELL_SIZE;

    ui_draw_rounded_rect_filled(win, fx + 2, fy + 2, CELL_SIZE - 5, CELL_SIZE - 5, 4, COLOR_FOOD);

    for (int i = snake_len - 1; i >= 0; i--) {
        int px = BOARD_X + snake[i].x * CELL_SIZE;
        int py = BOARD_Y + snake[i].y * CELL_SIZE;

        uint32_t color = (i == 0) ? COLOR_SNAKE_HEAD : COLOR_SNAKE_BODY;

        ui_draw_rounded_rect_filled(win, px + 1, py + 1, CELL_SIZE - 3, CELL_SIZE - 3, 4, color);
    }
}

static void snake_paint(ui_window_t win) {
    char score_buf[16] = {0};
    char best_buf[16] = {0};

    ui_draw_rect(win, 0, 0, WINDOW_W, WINDOW_H, COLOR_BG);

    ui_draw_string(win, 16, 14, "Snake", COLOR_TEXT);
    ui_draw_string(win, 16, 34, "WASD or Arrow Keys", COLOR_MUTED);

    int_to_text(score, score_buf);
    int_to_text(best_score, best_buf);

    ui_draw_rounded_rect_filled(win, 190, 12, 56, 24, 6, COLOR_PANEL);
    ui_draw_string(win, 198, 20, "Score", COLOR_MUTED);
    ui_draw_string(win, 250, 20, score_buf, COLOR_TEXT);

    ui_draw_rounded_rect_filled(win, 250, 12, 64, 24, 6, COLOR_PANEL);
    ui_draw_string(win, 258, 20, "Best", COLOR_MUTED);
    ui_draw_string(win, 292, 20, best_buf, COLOR_TEXT);

    if (game_over) {
        ui_draw_string(win, 16, 52, "Game Over - Press R to restart", COLOR_RED);
    } else {
        ui_draw_string(win, 16, 52, "Eat food and avoid walls and yourself", COLOR_MUTED);
    }

    draw_board(win);
}

static void handle_click(int x, int y) {
    if (point_in_rect(x, y, 16, 370, 72, 28)) {
        init_game();
    }
}

static void handle_key(int key) {
    if (key == 'r' || key == 'R') {
        init_game();
        return;
    }

    if (game_over) return;

    if (is_left_key(key)) request_direction(DIR_LEFT);
    else if (is_right_key(key)) request_direction(DIR_RIGHT);
    else if (is_up_key(key)) request_direction(DIR_UP);
    else if (is_down_key(key)) request_direction(DIR_DOWN);
}

int main(int argc, char **argv) {
    int game_speed_ms = 80;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-speed") == 0 && i + 1 < argc) {
            int val = atoi(argv[i + 1]);

            // Clamp to safe range
            if (val < 20) val = 20;
            if (val > 500) val = 500;

            game_speed_ms = val;
            i++;
        }
    }

    ui_window_t win = ui_window_create("Snake", 220, 120, WINDOW_W, WINDOW_H);
    if (!win) return 1;

    ui_window_set_resizable(win, false);

    init_game();

    snake_paint(win);
    ui_mark_dirty(win, 0, 0, WINDOW_W, WINDOW_H);

    gui_event_t ev;

    while (1) {
        while (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                snake_paint(win);
                ui_mark_dirty(win, 0, 0, WINDOW_W, WINDOW_H);

            } else if (ev.type == GUI_EVENT_CLICK) {
                handle_click(ev.arg1, ev.arg2);

            } else if (ev.type == GUI_EVENT_KEY) {
                handle_key(ev.arg1);

            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        }

        move_snake_step();

        snake_paint(win);
        ui_mark_dirty(win, 0, 0, WINDOW_W, WINDOW_H);

        sleep(game_speed_ms);
    }

    return 0;
}

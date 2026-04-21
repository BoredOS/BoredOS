// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

// BOREDOS_APP_DESC: 3/2D Graphing and plotting utility.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/se.sjoerd.Graphs.png;/Library/images/icons/colloid/app-icon-preview.png
#include "syscall.h"
#include "libui.h"
#include "../../wm/libwidget.h"
#include <stdbool.h>
#include "stdlib.h"
#include "math.h"

// External syscalls from libc
extern void sys_parallel_run(void (*fn)(void*), void **args, int count);

// =========
// Constants
// =========
// Adjust the below variables to suit your system specification and preferences.
// --- User Configuration ---
#define ROTATE 1  // Set to 0 to disable auto-rotation in 3D mode.
#define GRID_3D 41 // Grid resolution. Adjust on how much you want your PC to die (lmao)
// --------------------------
#define TOOLBAR_H 30
#define STATUSBAR_H 30
#define GRAPH_Y TOOLBAR_H
#define CLIENT_H (win_h - 20)

static int win_w = 750;
static int win_h = 600;
static int graph_w = 750;
static int graph_h = 520;
static int fb_capacity = 0;
static uint32_t *graph_fb = NULL;
static int32_t *graph_zb = NULL; 
static uint64_t *graph_czb = NULL;  

#define COLOR_BG        0xFF1A1A2E
#define COLOR_GRID      0xFF2A2A4A
#define COLOR_AXIS      0xFF6A6A8A
#define COLOR_CURVE     0xFF00FF88
#define COLOR_TEXT       0xFFE0E0E0
#define COLOR_TOOLBAR_BG 0xFF2D2D2D
#define COLOR_STATUS_BG  0xFF252535
#define COLOR_DARK_BG    0xFF1E1E1E
#define COLOR_DARK_PANEL 0xFF2D2D2D
#define COLOR_DARK_TEXT  0xFFF0F0F0
#define MAX_TOKENS 256
#define MAX_NODES  128

#define MODE_2D 0
#define MODE_3D 1


// =================
// Expression Parser
// =================
enum {
    TOK_NUM, TOK_VAR_X, TOK_VAR_Y, TOK_VAR_Z,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_CARET,
    TOK_LPAREN, TOK_RPAREN, TOK_EQUALS,
    TOK_FN_SIN, TOK_FN_COS, TOK_FN_TAN, TOK_FN_SQRT, TOK_FN_ABS, TOK_FN_LOG,
    TOK_END
};

enum {
    NODE_NUM, NODE_VAR,
    NODE_ADD, NODE_SUB, NODE_MUL, NODE_DIV, NODE_POW,
    NODE_NEG,
    NODE_SIN, NODE_COS, NODE_TAN, NODE_SQRT, NODE_ABS, NODE_LOG
};

typedef struct { int type; double value; } Token;
typedef struct { int type; double value; int var_idx; int left, right; } ASTNode;

enum {
    OP_PUSH_NUM, OP_PUSH_VAR,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW,
    OP_NEG,
    OP_SIN, OP_COS, OP_TAN, OP_SQRT, OP_ABS, OP_LOG
};

typedef struct {
    int op;
    double val;
    int var_idx;
} Instruction;

#define MAX_BC_SIZE 256
static Instruction lhs_bc[MAX_BC_SIZE], rhs_bc[MAX_BC_SIZE];
static int lhs_bc_len = 0, rhs_bc_len = 0;



static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_value_tok(int t) {
    return t == TOK_NUM || t == TOK_VAR_X || t == TOK_VAR_Y || t == TOK_VAR_Z || t == TOK_RPAREN;
}

static int tokenize(const char *input, Token *tokens) {
    int count = 0;
    const char *p = input;

    while (*p && count < MAX_TOKENS - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (count > 0 && is_value_tok(tokens[count-1].type)) {
            if (is_digit(*p) || *p == '.' || is_alpha(*p) || *p == '(') {
                tokens[count].type = TOK_STAR; tokens[count].value = 0; count++;
            }
        }

        if (is_digit(*p) || (*p == '.' && is_digit(p[1]))) {
            double val = 0;
            while (is_digit(*p)) { val = val * 10 + (*p - '0'); p++; }
            if (*p == '.') {
                p++; double frac = 0.1;
                while (is_digit(*p)) { val += (*p - '0') * frac; frac *= 0.1; p++; }
            }
            tokens[count].type = TOK_NUM; tokens[count].value = val; count++;
        } else if (is_alpha(*p)) {
            char buf[32]; int len = 0;
            while (is_alpha(*p) && len < 31) buf[len++] = *p++;
            buf[len] = 0;

            if (len == 1 && (buf[0] == 'x' || buf[0] == 'X')) tokens[count].type = TOK_VAR_X;
            else if (len == 1 && (buf[0] == 'y' || buf[0] == 'Y')) tokens[count].type = TOK_VAR_Y;
            else if (len == 1 && (buf[0] == 'z' || buf[0] == 'Z')) tokens[count].type = TOK_VAR_Z;
            else if (strcmp(buf, "sin") == 0) tokens[count].type = TOK_FN_SIN;
            else if (strcmp(buf, "cos") == 0) tokens[count].type = TOK_FN_COS;
            else if (strcmp(buf, "tan") == 0) tokens[count].type = TOK_FN_TAN;
            else if (strcmp(buf, "sqrt") == 0) tokens[count].type = TOK_FN_SQRT;
            else if (strcmp(buf, "abs") == 0) tokens[count].type = TOK_FN_ABS;
            else if (strcmp(buf, "log") == 0) tokens[count].type = TOK_FN_LOG;
            else if (strcmp(buf, "pi") == 0 || strcmp(buf, "PI") == 0) {
                tokens[count].type = TOK_NUM; tokens[count].value = M_PI;
            } else { tokens[count].type = TOK_NUM; tokens[count].value = 0; }
            count++;
        } else {
            switch (*p) {
                case '+': tokens[count].type = TOK_PLUS; break;
                case '-': tokens[count].type = TOK_MINUS; break;
                case '*': tokens[count].type = TOK_STAR; break;
                case '/': tokens[count].type = TOK_SLASH; break;
                case '^': tokens[count].type = TOK_CARET; break;
                case '(': {
                    if (count > 0 && is_value_tok(tokens[count-1].type)) {
                        tokens[count].type = TOK_STAR; count++;
                    }
                    tokens[count].type = TOK_LPAREN; break;
                }
                case ')': tokens[count].type = TOK_RPAREN; break;
                case '=': tokens[count].type = TOK_EQUALS; break;
                default: p++; continue;
            }
            p++; count++;
        }
    }
    tokens[count].type = TOK_END;
    return count;
}

static int parse_expr(Token *t, int *p, ASTNode *n, int *nc);

static int parse_atom(Token *t, int *p, ASTNode *n, int *nc) {
    if (t[*p].type == TOK_NUM) {
        int i = (*nc)++;
        n[i].type = NODE_NUM; n[i].value = t[*p].value; n[i].left = n[i].right = -1;
        (*p)++; return i;
    }
    if (t[*p].type >= TOK_VAR_X && t[*p].type <= TOK_VAR_Z) {
        int i = (*nc)++;
        n[i].type = NODE_VAR; n[i].var_idx = t[*p].type - TOK_VAR_X;
        n[i].left = n[i].right = -1; (*p)++; return i;
    }
    if (t[*p].type == TOK_LPAREN) {
        (*p)++;
        int inner = parse_expr(t, p, n, nc);
        if (t[*p].type == TOK_RPAREN) (*p)++;
        return inner;
    }
    int i = (*nc)++;
    n[i].type = NODE_NUM; n[i].value = 0; n[i].left = n[i].right = -1;
    return i;
}

static int parse_unary(Token *t, int *p, ASTNode *n, int *nc) {
    if (t[*p].type == TOK_MINUS) {
        (*p)++;
        int op = parse_unary(t, p, n, nc);
        int i = (*nc)++; n[i].type = NODE_NEG; n[i].left = op; n[i].right = -1; return i;
    }
    if (t[*p].type >= TOK_FN_SIN && t[*p].type <= TOK_FN_LOG) {
        int fn_type = t[*p].type; (*p)++;
        int arg;
        if (t[*p].type == TOK_LPAREN) {
            (*p)++;
            arg = parse_expr(t, p, n, nc);
            if (t[*p].type == TOK_RPAREN) (*p)++;
        } else {
            arg = parse_unary(t, p, n, nc);
        }
        int node_type = NODE_SIN;
        if (fn_type == TOK_FN_COS) node_type = NODE_COS;
        else if (fn_type == TOK_FN_TAN) node_type = NODE_TAN;
        else if (fn_type == TOK_FN_SQRT) node_type = NODE_SQRT;
        else if (fn_type == TOK_FN_ABS) node_type = NODE_ABS;
        else if (fn_type == TOK_FN_LOG) node_type = NODE_LOG;
        int i = (*nc)++; n[i].type = node_type; n[i].left = arg; n[i].right = -1; return i;
    }
    return parse_atom(t, p, n, nc);
}

static int parse_power(Token *t, int *p, ASTNode *n, int *nc) {
    int left = parse_unary(t, p, n, nc);
    if (t[*p].type == TOK_CARET) {
        (*p)++;
        int right = parse_power(t, p, n, nc); 
        int i = (*nc)++; n[i].type = NODE_POW; n[i].left = left; n[i].right = right; return i;
    }
    return left;
}

static int parse_term(Token *t, int *p, ASTNode *n, int *nc) {
    int left = parse_power(t, p, n, nc);
    while (t[*p].type == TOK_STAR || t[*p].type == TOK_SLASH) {
        int op = (t[*p].type == TOK_STAR) ? NODE_MUL : NODE_DIV; (*p)++;
        int right = parse_power(t, p, n, nc);
        int i = (*nc)++; n[i].type = op; n[i].left = left; n[i].right = right; left = i;
    }
    return left;
}

static int parse_expr(Token *t, int *p, ASTNode *n, int *nc) {
    int left = parse_term(t, p, n, nc);
    while (t[*p].type == TOK_PLUS || t[*p].type == TOK_MINUS) {
        int op = (t[*p].type == TOK_PLUS) ? NODE_ADD : NODE_SUB; (*p)++;
        int right = parse_term(t, p, n, nc);
        int i = (*nc)++; n[i].type = op; n[i].left = left; n[i].right = right; left = i;
    }
    return left;
}

static double eval_ast(ASTNode *n, int idx, double x, double y, double z) {
    if (idx < 0) return 0;
    ASTNode *nd = &n[idx];
    switch (nd->type) {
        case NODE_NUM: return nd->value;
        case NODE_VAR: return nd->var_idx == 0 ? x : nd->var_idx == 1 ? y : z;
        case NODE_ADD: return eval_ast(n, nd->left, x, y, z) + eval_ast(n, nd->right, x, y, z);
        case NODE_SUB: return eval_ast(n, nd->left, x, y, z) - eval_ast(n, nd->right, x, y, z);
        case NODE_MUL: return eval_ast(n, nd->left, x, y, z) * eval_ast(n, nd->right, x, y, z);
        case NODE_DIV: {
            double d = eval_ast(n, nd->right, x, y, z);
            return fabs(d) < 1e-15 ? 1e15 : eval_ast(n, nd->left, x, y, z) / d;
        }
        case NODE_POW: {
            double b = eval_ast(n, nd->left, x, y, z);
            double e = eval_ast(n, nd->right, x, y, z);
            if (e == 2.0) return b * b;
            if (e == 3.0) return b * b * b;
            return pow(b, e);
        }
        case NODE_NEG: return -eval_ast(n, nd->left, x, y, z);
        case NODE_SIN: return sin(eval_ast(n, nd->left, x, y, z));
        case NODE_COS: return cos(eval_ast(n, nd->left, x, y, z));
        case NODE_TAN: return tan(eval_ast(n, nd->left, x, y, z));
        case NODE_SQRT: return sqrt(eval_ast(n, nd->left, x, y, z));
        case NODE_ABS: return fabs(eval_ast(n, nd->left, x, y, z));
        case NODE_LOG: return log(eval_ast(n, nd->left, x, y, z));
    }
    return 0;
}

// =================
// Bytecode Compiler
// =================
static int compile_ast(ASTNode *nodes, int idx, Instruction *bc, int *len) {
    if (idx < 0 || *len >= MAX_BC_SIZE) return 0;
    ASTNode *n = &nodes[idx];
    
    compile_ast(nodes, n->left, bc, len);
    compile_ast(nodes, n->right, bc, len);
    
    Instruction *inst = &bc[(*len)++];
    switch (n->type) {
        case NODE_NUM: inst->op = OP_PUSH_NUM; inst->val = n->value; break;
        case NODE_VAR: inst->op = OP_PUSH_VAR; inst->var_idx = n->var_idx; break;
        case NODE_ADD: inst->op = OP_ADD; break;
        case NODE_SUB: inst->op = OP_SUB; break;
        case NODE_MUL: inst->op = OP_MUL; break;
        case NODE_DIV: inst->op = OP_DIV; break;
        case NODE_POW: inst->op = OP_POW; break;
        case NODE_NEG: inst->op = OP_NEG; break;
        case NODE_SIN: inst->op = OP_SIN; break;
        case NODE_COS: inst->op = OP_COS; break;
        case NODE_TAN: inst->op = OP_TAN; break;
        case NODE_SQRT: inst->op = OP_SQRT; break;
        case NODE_ABS: inst->op = OP_ABS; break;
        case NODE_LOG: inst->op = OP_LOG; break;
    }
    return 1;
}

static double run_bc(Instruction *bc, int len, double x, double y, double z) {
    if (len == 0) return 0;
    double stack[32];
    int sp = 0;
    
    for (int i = 0; i < len; i++) {
        Instruction *inst = &bc[i];
        switch (inst->op) {
            case OP_PUSH_NUM: stack[sp++] = inst->val; break;
            case OP_PUSH_VAR: stack[sp++] = (inst->var_idx == 0 ? x : inst->var_idx == 1 ? y : z); break;
            case OP_ADD: { double b = stack[--sp]; double a = stack[--sp]; stack[sp++] = a + b; break; }
            case OP_SUB: { double b = stack[--sp]; double a = stack[--sp]; stack[sp++] = a - b; break; }
            case OP_MUL: { double b = stack[--sp]; double a = stack[--sp]; stack[sp++] = a * b; break; }
            case OP_DIV: { 
                double b = stack[--sp]; double a = stack[--sp]; 
                stack[sp++] = (fabs(b) < 1e-15) ? 1e15 : a / b; break; 
            }
            case OP_POW: {
                double b = stack[--sp]; double a = stack[--sp];
                if (b == 2.0) stack[sp++] = a * a;
                else if (b == 3.0) stack[sp++] = a * a * a;
                else stack[sp++] = pow(a, b);
                break;
            }
            case OP_NEG: stack[sp-1] = -stack[sp-1]; break;
            case OP_SIN: stack[sp-1] = sin(stack[sp-1]); break;
            case OP_COS: stack[sp-1] = cos(stack[sp-1]); break;
            case OP_TAN: stack[sp-1] = tan(stack[sp-1]); break;
            case OP_SQRT: stack[sp-1] = sqrt(stack[sp-1]); break;
            case OP_ABS:  stack[sp-1] = fabs(stack[sp-1]); break;
            case OP_LOG:  stack[sp-1] = log(stack[sp-1]); break;
        }
    }
    return sp > 0 ? stack[sp-1] : 0;
}

// Check which variables an AST subtree uses
static void ast_find_vars(ASTNode *n, int idx, bool *has_x, bool *has_y, bool *has_z) {
    if (idx < 0) return;
    if (n[idx].type == NODE_VAR) {
        if (n[idx].var_idx == 0) *has_x = true;
        if (n[idx].var_idx == 1) *has_y = true;
        if (n[idx].var_idx == 2) *has_z = true;
    }
    ast_find_vars(n, n[idx].left, has_x, has_y, has_z);
    ast_find_vars(n, n[idx].right, has_x, has_y, has_z);
}

// =================
// Application State
// =================
static ui_window_t win_graph;

static char eq_buffer[256];
static int eq_len = 0;

// Parsed equation
static ASTNode lhs_nodes[MAX_NODES], rhs_nodes[MAX_NODES];
static int lhs_root = -1, rhs_root = -1;
static int lhs_nc = 0, rhs_nc = 0;
static bool eq_valid = false;
static int graph_mode = MODE_2D;

// 2D view
static double view_x_min = -10, view_x_max = 10;
static double view_y_min = -6.4, view_y_max = 6.4;

// 3D view
static double rot_x = 0.5, rot_y = 0.6; 
static double range_3d = 5.0;
static double zoom_3d = 1.0; 
static bool filled_mode = false;
static bool is_explicit_3d = false;
static bool is_explicit_2d = false;

static bool right_dragging = false;
static int drag_last_x = 0, drag_last_y = 0;

#define MAX_Z_PER_POINT 4
typedef struct {
    double z[MAX_Z_PER_POINT];
    float nx[MAX_Z_PER_POINT], ny[MAX_Z_PER_POINT], nz[MAX_Z_PER_POINT];
    int sx[MAX_Z_PER_POINT], sy[MAX_Z_PER_POINT], dz[MAX_Z_PER_POINT];
    int count;  
} surf_point_t;

static double surf_x[GRID_3D][GRID_3D], surf_y_3d[GRID_3D][GRID_3D];
static surf_point_t surf[GRID_3D][GRID_3D];

static bool surface_needs_eval = true;
static double rot_cx, rot_sx, rot_cy, rot_sy;

static widget_textbox_t tb_equation;
static widget_button_t btn_plot;
static widget_button_t btn_mode;
static widget_button_t btn_range_minus;
static widget_button_t btn_range_plus;

static const char *preset_labels[] = {
    "y = sin(x)", "y = x^2", "y = cos(x)*x",
    "z = sin(x)*cos(y)", "z = x^2 - y^2", "x^2+y^2+z^2=25", "x^2+y^2=16"
};
#define NUM_PRESETS 7

static bool presets_open = false;

// Widget context
static void gfx_draw_rect(void *ud, int x, int y, int w, int h, uint32_t c) {
    ui_draw_rect((ui_window_t)ud, x, y, w, h, c);
}
static void gfx_draw_rr(void *ud, int x, int y, int w, int h, int r, uint32_t c) {
    ui_draw_rounded_rect_filled((ui_window_t)ud, x, y, w, h, r, c);
}
static void gfx_draw_str(void *ud, int x, int y, const char *s, uint32_t c) {
    ui_draw_string((ui_window_t)ud, x, y, s, c);
}
static widget_context_t wctx = { 0, gfx_draw_rect, gfx_draw_rr, gfx_draw_str, NULL, NULL, false };

// ================
// Graphics helpers
// ================
static void gfb_clear(uint32_t c) {
    int total = graph_w * graph_h;
    uint64_t clear_val = ((uint64_t)1000000LL << 32) | c;
    for (int i = 0; i < total; i++) {
        graph_fb[i] = c;
        graph_zb[i] = 1000000;
        if (graph_czb) graph_czb[i] = clear_val;
    }
}

static void gfb_pixel(int x, int y, uint32_t c) {
    if (x >= 0 && x < graph_w && y >= 0 && y < graph_h)
        graph_fb[y * graph_w + x] = c;
}

static void gfb_pixel_z(int x, int y, int z, uint32_t c) {
    if (x < 0 || x >= graph_w || y < 0 || y >= graph_h) return;
    int idx = y * graph_w + x;
    
    if (graph_czb) {
        uint64_t new_val = ((uint64_t)z << 32) | c;
        uint64_t old_val;
        while (z < (int32_t)((old_val = __atomic_load_n(&graph_czb[idx], __ATOMIC_RELAXED)) >> 32)) {
            if (__atomic_compare_exchange_n(&graph_czb[idx], &old_val, new_val, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED))
                break;
        }
    } else {
        int32_t old_z;
        while (z < (old_z = __atomic_load_n(&graph_zb[idx], __ATOMIC_RELAXED))) {
            if (__atomic_compare_exchange_n(&graph_zb[idx], &old_z, z, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
                graph_fb[idx] = c;
                break;
            }
        }
    }
}



static void gfb_line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = fabs((double)(x1 - x0)), dy = fabs((double)(y1 - y0));
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (int i = 0; i < 2000; i++) {
        gfb_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static void gfb_line_z(int x0, int y0, int z0, int x1, int y1, int z1, uint32_t c) {
    int dx = fabs((double)(x1 - x0)), dy = fabs((double)(y1 - y0));
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int steps = (dx > dy ? dx : dy);
    if (steps == 0) { gfb_pixel_z(x0, y0, z0, c); return; }
    
    for (int i = 0; i <= steps && i < 2000; i++) {
        int cz = z0 + (int)((long)(z1 - z0) * i / steps);
        gfb_pixel_z(x0, y0, cz, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static void gfb_triangle_z(int x0, int y0, int z0, int x1, int y1, int z1, int x2, int y2, int z2, uint32_t c) {
    if (y1 < y0) { int t; t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; t=z0; z0=z1; z1=t; }
    if (y2 < y0) { int t; t=x0; x0=x2; x2=t; t=y0; y0=y2; y2=t; t=z0; z0=z2; z2=t; }
    if (y2 < y1) { int t; t=x1; x1=x2; x2=t; t=y1; y1=y2; y2=t; t=z1; z1=z2; z2=t; }

    if (y0 == y2) return;

    for (int y = y0; y <= y2; y++) {
        if (y < 0 || y >= graph_h) continue;
        bool second_half = y > y1 || y1 == y0;
        int h1 = second_half ? (y2 - y1) : (y1 - y0);
        int h2 = y2 - y0;
        if (h1 == 0) h1 = 1;
        if (h2 == 0) h2 = 1;

        float alpha = (float)(y - y0) / h2;
        float beta  = (float)(y - (second_half ? y1 : y0)) / h1;

        int ax = x0 + (int)((x2 - x0) * alpha);
        int bx = second_half ? x1 + (int)((x2 - x1) * beta) : x0 + (int)((x1 - x0) * beta);
        int az = z0 + (int)((z2 - z0) * alpha);
        int bz = second_half ? z1 + (int)((z2 - z1) * beta) : z0 + (int)((z1 - z0) * beta);

        if (ax > bx) { int t; t=ax; ax=bx; bx=t; t=az; az=bz; bz=t; }
        int span = bx - ax;
        int x_start = ax < 0 ? 0 : ax;
        int x_end   = bx >= graph_w ? graph_w - 1 : bx;
        for (int x = x_start; x <= x_end; x++) {
            float phi = (span == 0) ? 0.5f : (float)(x - ax) / span;
            int cz = az + (int)((bz - az) * phi);
            gfb_pixel_z(x, y, cz, c);
        }
    }
}

static uint32_t color_by_height(double z, double zmin, double zmax) {
    if (zmax <= zmin) return COLOR_CURVE;
    double t = (z - zmin) / (zmax - zmin);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int r, g, b;
    if (t < 0.25) { double s = t/0.25; r=0; g=(int)(s*255); b=255; }
    else if (t < 0.5) { double s=(t-0.25)/0.25; r=0; g=255; b=(int)((1-s)*255); }
    else if (t < 0.75) { double s=(t-0.5)/0.25; r=(int)(s*255); g=255; b=0; }
    else { double s=(t-0.75)/0.25; r=255; g=(int)((1-s)*255); b=0; }
    return 0xFF000000 | (r<<16) | (g<<8) | b;
}

static uint32_t apply_shading(uint32_t color, double nx, double ny, double nz) {
    double len = sqrt(nx*nx + ny*ny + nz*nz);
    if (len > 1e-9) { nx /= len; ny /= len; nz /= len; }
    else { nx = 0; ny = 1; nz = 0; }

    double lx = 0.577, ly = 0.707, lz = 0.408;
    double dot = nx * lx + ny * ly + nz * lz;
    if (dot < 0) dot = -dot * 0.2; 
    
    double intensity = 0.3 + 0.7 * dot;
    if (intensity > 1.0) intensity = 1.0;
    
    int r = (color >> 16) & 0xFF;
    int g = (color >> 8) & 0xFF;
    int b = color & 0xFF;
    
    r = (int)(r * intensity);
    g = (int)(g * intensity);
    b = (int)(b * intensity);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

// ========================
// 2D coordinate transforms
// ========================
static int screen_x_2d(double wx) {
    return (int)((wx - view_x_min) / (view_x_max - view_x_min) * graph_w);
}
static int screen_y_2d(double wy) {
    return (int)((view_y_max - wy) / (view_y_max - view_y_min) * graph_h);
}
static double world_x_2d(int px) {
    return view_x_min + (px / (double)graph_w) * (view_x_max - view_x_min);
}

// =============
// 3D projection
// =============
static void project_3d(double px, double py, double pz, int *sx, int *sy, int *sz) {
    double nx = px * rot_cy + pz * rot_sy, nz = -px * rot_sy + pz * rot_cy;
    px = nx; pz = nz;
    double ny = py * rot_cx - pz * rot_sx;
    nz = py * rot_sx + pz * rot_cx;
    py = ny; pz = nz;
    
    double view_dim = graph_h < graph_w ? graph_h : graph_w;
    double base_scale = view_dim * 0.35 / range_3d * zoom_3d;
    
    double d = range_3d * 5;
    double zd = pz + d;
    if (zd < d * 0.1) zd = d * 0.1;
    double persp = d / zd;
    *sx = (int)(px * base_scale * persp) + graph_w / 2;
    *sy = (int)(-py * base_scale * persp) + graph_h / 2;
    *sz = (int)(pz * 10000); // Fixed-point depth
}
// istg math is scary
// ====================================================
// Evaluate the implicit function: f(x,y,z) = LHS - RHS
// ====================================================
static double eval_implicit(double x, double y, double z) {
    return run_bc(lhs_bc, lhs_bc_len, x, y, z) - run_bc(rhs_bc, rhs_bc_len, x, y, z);
}

static double eval_rhs_only(double x, double y, double z) {
    return run_bc(rhs_bc, rhs_bc_len, x, y, z);
}

// ===========================
// Parse and classify equation
// ===========================
static void parse_equation(void) {
    surface_needs_eval = true;
    eq_valid = false;
    lhs_nc = 0; rhs_nc = 0;
    lhs_root = -1; rhs_root = -1;

    // Find '='
    int eq_pos = -1;
    for (int i = 0; eq_buffer[i]; i++) {
        if (eq_buffer[i] == '=') { eq_pos = i; break; }
    }

    if (eq_pos >= 0) {
        // Split into LHS and RHS
        char lhs_str[256], rhs_str[256];
        memcpy(lhs_str, eq_buffer, eq_pos); lhs_str[eq_pos] = 0;
        strcpy(rhs_str, eq_buffer + eq_pos + 1);

        Token lt[MAX_TOKENS], rt[MAX_TOKENS];
        tokenize(lhs_str, lt); tokenize(rhs_str, rt);
        int lp = 0, rp = 0;
        lhs_root = parse_expr(lt, &lp, lhs_nodes, &lhs_nc);
        rhs_root = parse_expr(rt, &rp, rhs_nodes, &rhs_nc);
    } else {
        Token tt[MAX_TOKENS];
        tokenize(eq_buffer, tt);
        int tp = 0;
        rhs_root = parse_expr(tt, &tp, rhs_nodes, &rhs_nc);
        // LHS = y (for display as y=f(x) if no y/z in expr)
        bool hx=false, hy=false, hz=false;
        ast_find_vars(rhs_nodes, rhs_root, &hx, &hy, &hz);
        if (!hy && !hz) {
            lhs_nc = 1;
            lhs_nodes[0].type = NODE_VAR; lhs_nodes[0].var_idx = 1;
            lhs_nodes[0].left = lhs_nodes[0].right = -1;
            lhs_root = 0;
        } else {
            lhs_root = rhs_root;
            memcpy(lhs_nodes, rhs_nodes, sizeof(ASTNode) * rhs_nc);
            lhs_nc = rhs_nc;
            rhs_nc = 1;
            rhs_nodes[0].type = NODE_NUM; rhs_nodes[0].value = 0;
            rhs_nodes[0].left = rhs_nodes[0].right = -1;
            rhs_root = 0;
        }
    }

    // Determine mode
    bool lhx=false, lhy=false, lhz=false, rhx=false, rhy=false, rhz=false;
    ast_find_vars(lhs_nodes, lhs_root, &lhx, &lhy, &lhz);
    ast_find_vars(rhs_nodes, rhs_root, &rhx, &rhy, &rhz);

    bool has_z = lhz || rhz;
    is_explicit_2d = false;
    is_explicit_3d = false;

    if (has_z) {
        graph_mode = MODE_3D;
        if (lhs_nc >= 1 && lhs_nodes[lhs_root].type == NODE_VAR &&
            lhs_nodes[lhs_root].var_idx == 2 && !rhz) {
            is_explicit_3d = true;
        }
    } else {
        graph_mode = MODE_2D;
        if (lhs_nc >= 1 && lhs_nodes[lhs_root].type == NODE_VAR &&
            lhs_nodes[lhs_root].var_idx == 1 && !rhy) {
            is_explicit_2d = true;
        }
    }

    eq_valid = true;

    // Compile to bytecode
    lhs_bc_len = 0; rhs_bc_len = 0;
    if (lhs_root >= 0) compile_ast(lhs_nodes, lhs_root, lhs_bc, &lhs_bc_len);
    if (rhs_root >= 0) compile_ast(rhs_nodes, rhs_root, rhs_bc, &rhs_bc_len);
}

// =========
// Rendering
// =========
static double get_nice_step(double range, int target_divisions) {
    if (range <= 0) return 1.0;
    double approx = range / target_divisions;
    
    // Find magnitude (10^n)
    double mag = 1.0;
    if (approx >= 1.0) {
        while (approx >= 10.0) { approx /= 10.0; mag *= 10.0; }
    } else {
        while (approx < 1.0) { approx *= 10.0; mag /= 10.0; }
    }
    
    // Pick nice residual
    double res;
    if (approx < 1.5) res = 1.0;
    else if (approx < 3.0) res = 2.0;
    else if (approx < 7.0) res = 5.0;
    else res = 10.0;
    
    return res * mag;
}

static void apply_aspect_ratio(void) {
    if (graph_w <= 0 || graph_h <= 0) return;
    double cy = (view_y_min + view_y_max) / 2.0;
    double x_range = view_x_max - view_x_min;
    double target_y_range = x_range * (double)graph_h / (double)graph_w;
    view_y_min = cy - target_y_range / 2.0;
    view_y_max = cy + target_y_range / 2.0;
}

static void autofit_2d_view(void) {
    if (!is_explicit_2d) {
        apply_aspect_ratio();
        return;
    }
    double y_min_data = 1e30, y_max_data = -1e30;
    bool found = false;
    for (int px = 0; px < graph_w; px += 2) {
        double wx = world_x_2d(px);
        double wy = eval_rhs_only(wx, 0, 0);
        if (wy == wy && fabs(wy) < 1e10) {
            if (wy < y_min_data) y_min_data = wy;
            if (wy > y_max_data) y_max_data = wy;
            found = true;
        }
    }
    if (found) {
        if (y_min_data * y_max_data <= 0 || fabs(y_min_data) < (y_max_data - y_min_data) * 0.5) {
            double max_abs = fabs(y_min_data);
            if (fabs(y_max_data) > max_abs) max_abs = fabs(y_max_data);
            double pad = max_abs * 0.15;
            if (pad < 0.5) pad = 0.5;
            view_y_min = -(max_abs + pad);
            view_y_max = (max_abs + pad);
        } else {
            double pad = (y_max_data - y_min_data) * 0.15;
            if (pad < 0.5) pad = 0.5;
            view_y_min = y_min_data - pad;
            view_y_max = y_max_data + pad;
        }

        double x_range = view_x_max - view_x_min;
        double target_y_range = x_range * (double)graph_h / (double)graph_w;
        double current_y_range = view_y_max - view_y_min;

        if (current_y_range < target_y_range) {
            double cy = (view_y_min + view_y_max) / 2.0;
            if (fabs(cy) < current_y_range * 0.1) cy = 0;
            view_y_min = cy - target_y_range / 2.0;
            view_y_max = cy + target_y_range / 2.0;
        } else {
            double target_x_range = current_y_range * (double)graph_w / (double)graph_h;
            double cx = (view_x_min + view_x_max) / 2.0;
            if (fabs(cx) < x_range * 0.1) cx = 0;
            view_x_min = cx - target_x_range / 2.0;
            view_x_max = cx + target_x_range / 2.0;
        }
    } else {
        apply_aspect_ratio();
    }
}

static void render_2d_grid(void) {
    // Grid intervals
    double x_step = get_nice_step(view_x_max - view_x_min, 8);
    double y_step = get_nice_step(view_y_max - view_y_min, 6);

    // X grid lines
    double x_start = (int)(view_x_min / x_step) * x_step;
    int safety = 0;
    for (double wx = x_start - x_step; wx <= view_x_max + x_step && safety < 200; wx += x_step, safety++) {
        int sx = screen_x_2d(wx);
        if (sx >= 0 && sx < graph_w) {
            for (int y = 0; y < graph_h; y++) gfb_pixel(sx, y, COLOR_GRID);
        }
    }

    // Y grid lines
    double y_start = (int)(view_y_min / y_step) * y_step;
    safety = 0;
    for (double wy = y_start - y_step; wy <= view_y_max + y_step && safety < 200; wy += y_step, safety++) {
        int sy = screen_y_2d(wy);
        if (sy >= 0 && sy < graph_h) {
            for (int x = 0; x < graph_w; x++) gfb_pixel(x, sy, COLOR_GRID);
        }
    }

    // Axes
    int ax = screen_x_2d(0), ay = screen_y_2d(0);
    if (ax >= 0 && ax < graph_w)
        for (int y = 0; y < graph_h; y++) gfb_pixel(ax, y, COLOR_AXIS);
    if (ay >= 0 && ay < graph_h)
        for (int x = 0; x < graph_w; x++) gfb_pixel(x, ay, COLOR_AXIS);
}

static void render_2d_explicit(void) {
    int prev_sx = -1, prev_sy = -1;
    bool prev_valid = false;

    for (int px = 0; px < graph_w; px++) {
        double wx = world_x_2d(px);
        double wy = eval_rhs_only(wx, 0, 0);
        if (wy != wy || fabs(wy) > 1e10) { prev_valid = false; continue; }
        int sy = screen_y_2d(wy);
        if (prev_valid && fabs((double)(sy - prev_sy)) < graph_h) {
            gfb_line(prev_sx, prev_sy, px, sy, COLOR_CURVE);
        }
        prev_sx = px; prev_sy = sy; prev_valid = true;
    }
}

static void render_2d_implicit(void) {
    // Marching squares for f(x,y) = 0
    int grid_x = 200, grid_y = 130;
    double dx = (view_x_max - view_x_min) / grid_x;
    double dy = (view_y_max - view_y_min) / grid_y;

    for (int gy = 0; gy < grid_y; gy++) {
        for (int gx = 0; gx < grid_x; gx++) {
            double x0 = view_x_min + gx * dx;
            double y0 = view_y_max - gy * dy;
            double x1 = x0 + dx, y1 = y0 - dy;

            double f00 = eval_implicit(x0, y0, 0);
            double f10 = eval_implicit(x1, y0, 0);
            double f01 = eval_implicit(x0, y1, 0);
            double f11 = eval_implicit(x1, y1, 0);

            // Check edges for sign changes
            int sx0 = screen_x_2d(x0), sx1 = screen_x_2d(x1);
            int sy0 = screen_y_2d(y0), sy1 = screen_y_2d(y1);

            if ((f00 > 0) != (f10 > 0)) {
                double t = f00 / (f00 - f10);
                int mx = sx0 + (int)(t * (sx1 - sx0));
                gfb_pixel(mx, sy0, COLOR_CURVE);
                gfb_pixel(mx+1, sy0, COLOR_CURVE);
            }
            if ((f00 > 0) != (f01 > 0)) {
                double t = f00 / (f00 - f01);
                int my = sy0 + (int)(t * (sy1 - sy0));
                gfb_pixel(sx0, my, COLOR_CURVE);
                gfb_pixel(sx0, my+1, COLOR_CURVE);
            }
            if ((f10 > 0) != (f11 > 0)) {
                double t = f10 / (f10 - f11);
                int my = sy0 + (int)(t * (sy1 - sy0));
                gfb_pixel(sx1, my, COLOR_CURVE);
            }
            if ((f01 > 0) != (f11 > 0)) {
                double t = f01 / (f01 - f11);
                int mx = sx0 + (int)(t * (sx1 - sx0));
                gfb_pixel(mx, sy1, COLOR_CURVE);
            }
        }
    }
}

static void render_3d_axes(void) {
    int ox, oy, oz;
    project_3d(0, 0, 0, &ox, &oy, &oz);

    int ax, ay, az;
    project_3d(range_3d, 0, 0, &ax, &ay, &az); gfb_line_z(ox, oy, oz, ax, ay, az, 0xFFFF4444);
    project_3d(0, range_3d, 0, &ax, &ay, &az); gfb_line_z(ox, oy, oz, ax, ay, az, 0xFF44FF44);
    project_3d(0, 0, range_3d, &ax, &ay, &az); gfb_line_z(ox, oy, oz, ax, ay, az, 0xFF4444FF);
}

// =======================
// Parallel Evaluation Job
// =======================

typedef struct {
    float c[MAX_Z_PER_POINT];  
    float nx[MAX_Z_PER_POINT], ny[MAX_Z_PER_POINT], nz[MAX_Z_PER_POINT]; 
    int   count;
} eval_cache_entry_t;

static eval_cache_entry_t eval_cache[3][GRID_3D][GRID_3D];

typedef struct {
    int start_j, end_j;
    double range;
    double step;
    double z_scale;
    int march_axis;
} eval_job_t;

static void eval_3d_explicit_job(void *arg) {
    eval_job_t *job = (eval_job_t *)arg;
    for (int j = job->start_j; j < job->end_j; j++) {
        for (int i = 0; i < GRID_3D; i++) {
            double wx = -job->range + i * job->step;
            double wy = -job->range + j * job->step;
            double wz = eval_rhs_only(wx, wy, 0);
            surf_x[j][i] = wx;
            surf_y_3d[j][i] = wy;
            surf[j][i].count = 0;
            
            if (fabs(wz) > 1e10 || wz != wz) {
                continue;
            }
            
            surf[j][i].z[0] = wz;
            
            double eps = 0.001;
            double dfx = 0.5 * (eval_rhs_only(wx+eps, wy, 0) - eval_rhs_only(wx-eps, wy, 0)) / eps;
            double dfy = 0.5 * (eval_rhs_only(wx, wy+eps, 0) - eval_rhs_only(wx, wy-eps, 0)) / eps;
            
            double nx = -dfx;
            double ny = -dfy;
            double nz = 1.0;
            double len = sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-9) { nx /= len; ny /= len; nz /= len; }
            else { nx = 0; ny = 0; nz = 1; }
            
            surf[j][i].nx[0] = (float)nx;
            surf[j][i].ny[0] = (float)ny;
            surf[j][i].nz[0] = (float)nz;
            surf[j][i].count = 1;
        }
    }
}

static void eval_3d_implicit_job(void *arg) {
    eval_job_t *job = (eval_job_t *)arg;
    const int axis        = job->march_axis;
    const int march_steps = 170;  
    const double mstep    = job->range * 2.0 / march_steps;

    for (int j = job->start_j; j < job->end_j; j++) {
        for (int i = 0; i < GRID_3D; i++) {
            double a = -job->range + i * job->step; 
            double b = -job->range + j * job->step; 

            eval_cache_entry_t *ce = &eval_cache[axis][j][i];
            ce->count = 0;

            double prev_f;
            switch (axis) {
                case 1:  prev_f = eval_implicit(-job->range, a, b); break;
                case 2:  prev_f = eval_implicit(a, -job->range, b); break;
                default: prev_f = eval_implicit(a, b, -job->range); break;
            }

            double found[MAX_Z_PER_POINT]; int nf = 0;
            for (int k = 1; k <= march_steps && nf < MAX_Z_PER_POINT; k++) {
                double c = -job->range + k * mstep;
                double cur_f;
                switch (axis) {
                    case 1:  cur_f = eval_implicit(c, a, b); break;
                    case 2:  cur_f = eval_implicit(a, c, b); break;
                    default: cur_f = eval_implicit(a, b, c); break;
                }
                if ((prev_f > 0) != (cur_f > 0) && fabs(prev_f) < 1e10 && fabs(cur_f) < 1e10) {
                    double ca = c - mstep, cb = c, fa = prev_f, fb = cur_f; (void)fb;
                    for (int bi = 0; bi < 15; bi++) {
                        double cm = (ca+cb)*0.5, fm;
                        switch (axis) {
                            case 1:  fm = eval_implicit(cm, a, b); break;
                            case 2:  fm = eval_implicit(a, cm, b); break;
                            default: fm = eval_implicit(a, b, cm); break;
                        }
                        if ((fa>0)!=(fm>0)) { cb=cm; fb=fm; } else { ca=cm; fa=fm; }
                    }
                    found[nf++] = (ca+cb)*0.5;
                }
                prev_f = cur_f;
            }
            for (int r = 0; r < nf-1; r++)
                for (int s = r+1; s < nf; s++)
                    if (found[r] > found[s]) { double t=found[r]; found[r]=found[s]; found[s]=t; }

            double eps = 0.001;
            for (int r = 0; r < nf; r++) {
                double c = found[r];
                double nx, ny, nz;
                switch (axis) {
                    case 1:
                        nx = eval_implicit(c+eps,a,b)-eval_implicit(c-eps,a,b);
                        ny = eval_implicit(c,a+eps,b)-eval_implicit(c,a-eps,b);
                        nz = eval_implicit(c,a,b+eps)-eval_implicit(c,a,b-eps);
                        break;
                    case 2:
                        nx = eval_implicit(a+eps,c,b)-eval_implicit(a-eps,c,b);
                        ny = eval_implicit(a,c+eps,b)-eval_implicit(a,c-eps,b);
                        nz = eval_implicit(a,c,b+eps)-eval_implicit(a,c,b-eps);
                        break;
                    default:
                        nx = eval_implicit(a+eps,b,c)-eval_implicit(a-eps,b,c);
                        ny = eval_implicit(a,b+eps,c)-eval_implicit(a,b-eps,c);
                        nz = eval_implicit(a,b,c+eps)-eval_implicit(a,b,c-eps);
                        break;
                }
                double d = sqrt(nx*nx+ny*ny+nz*nz);
                if (d > 1e-12) { nx/=d; ny/=d; nz/=d; } else { nx=0; ny=1; nz=0; }
                ce->c[r]  = (float)c;
                ce->nx[r] = (float)nx;
                ce->ny[r] = (float)ny;
                ce->nz[r] = (float)nz;
            }
            ce->count = nf;
        }
    }
}

static void eval_3d_project_job(void *arg) {
    eval_job_t *job = (eval_job_t *)arg;
    const int axis = job->march_axis;
    for (int j = job->start_j; j < job->end_j; j++) {
        for (int i = 0; i < GRID_3D; i++) {
            eval_cache_entry_t *ce = &eval_cache[axis][j][i];
            double a = -job->range + i * job->step;
            double b = -job->range + j * job->step;

            surf_x[j][i]    = a;
            surf_y_3d[j][i] = b;
            surf[j][i].count = ce->count;
            for (int s = 0; s < ce->count; s++) {
                double c = ce->c[s];
                surf[j][i].z[s]  = (float)c;
                surf[j][i].nx[s] = ce->nx[s];
                surf[j][i].ny[s] = ce->ny[s];
                surf[j][i].nz[s] = ce->nz[s];
                switch (axis) {
                    case 1: project_3d(c, b, a, &surf[j][i].sx[s], &surf[j][i].sy[s], &surf[j][i].dz[s]); break;
                    case 2: project_3d(a, b, c, &surf[j][i].sx[s], &surf[j][i].sy[s], &surf[j][i].dz[s]); break;
                    default:project_3d(a, c, b, &surf[j][i].sx[s], &surf[j][i].sy[s], &surf[j][i].dz[s]); break;
                }
            }
        }
    }
}

static void eval_3d_explicit_project_job(void *arg) {
    eval_job_t *job = (eval_job_t *)arg;
    for (int j = job->start_j; j < job->end_j; j++) {
        for (int i = 0; i < GRID_3D; i++) {
            double a = -job->range + i * job->step;
            double b = -job->range + j * job->step;
            
            for (int s = 0; s < surf[j][i].count; s++) {
                double c = surf[j][i].z[s];
                project_3d(a, c * job->z_scale, b, &surf[j][i].sx[s], &surf[j][i].sy[s], &surf[j][i].dz[s]);
            }
        }
    }
}

typedef struct {
    int start_j, end_j;
    double zmin, zmax;
    int march_axis;   
    int normal_axis; 
} draw_job_t;

static void render_3d_draw_job(void *arg) {
    draw_job_t *job = (draw_job_t *)arg;
    for (int j = job->start_j; j < job->end_j; j++) {
        for (int i = 0; i < GRID_3D; i++) {
            if (surf[j][i].count == 0) continue;

            for (int s = 0; s < surf[j][i].count; s++) {
                int sx0 = surf[j][i].sx[s], sy0 = surf[j][i].sy[s], dz0 = surf[j][i].dz[s];

                double height_val = (job->march_axis == 0) ? surf[j][i].z[s] : surf_y_3d[j][i];
                uint32_t col = color_by_height(height_val, job->zmin, job->zmax);
                if (filled_mode && job->normal_axis >= 0) {
                    float anx = (float)fabs(surf[j][i].nx[s]);
                    float any = (float)fabs(surf[j][i].ny[s]);
                    float anz = (float)fabs(surf[j][i].nz[s]);
                    bool dominant;
                    switch (job->normal_axis) {
                        case 0: dominant = (anz >= anx - 0.05f) && (anz >= any - 0.05f); break;
                        case 1: dominant = (anx >= any - 0.05f) && (anx >= anz - 0.05f); break;
                        case 2: dominant = (any >= anx - 0.05f) && (any >= anz - 0.05f); break;
                        default: dominant = true; break;
                    }
                    if (!dominant) continue;
                    
                    // Depth bias to prevent Z-fighting between passes
                    int bias = (job->normal_axis == 0) ? 0 : (job->normal_axis == 1) ? 2 : 4;
                    dz0 += bias; 
                }

                // Refined neighbor selection: only connect if points are world-space neighbors
                float world_step = (float)(job->zmax - job->zmin) / (GRID_3D - 1); // rough scaling
                if (world_step < 0.1f) world_step = 0.5f;
                float thresh = world_step * 2.5f;

                int s_tr = -1;
                if (i+1 < GRID_3D) {
                    float mind = 1e30f;
                    for (int n=0; n < surf[j][i+1].count; n++) {
                        float d = (float)fabs(surf[j][i+1].z[n] - surf[j][i].z[s]);
                        if (d < mind) { mind = d; s_tr = n; }
                    }
                    if (mind > thresh) s_tr = -1;
                }
                int s_bl = -1;
                if (j+1 < GRID_3D) {
                    float mind = 1e30f;
                    for (int n=0; n < surf[j+1][i].count; n++) {
                        float d = (float)fabs(surf[j+1][i].z[n] - surf[j][i].z[s]);
                        if (d < mind) { mind = d; s_bl = n; }
                    }
                    if (mind > thresh) s_bl = -1;
                }
                int s_br = -1;
                if (i+1 < GRID_3D && j+1 < GRID_3D) {
                    float mind = 1e30f;
                    for (int n=0; n < surf[j+1][i+1].count; n++) {
                        float d = (float)fabs(surf[j+1][i+1].z[n] - surf[j][i].z[s]);
                        if (d < mind) { mind = d; s_br = n; }
                    }
                    if (mind > thresh) s_br = -1;
                }

                if (filled_mode) {
                    bool v_tr = (s_tr >= 0);
                    bool v_bl = (s_bl >= 0);
                    bool v_br = (s_br >= 0);

                    if (v_tr && v_bl && v_br) {
                        int bias = (job->normal_axis == 0) ? 0 : (job->normal_axis == 1) ? 2 : 4;
                        int sx_tr = surf[j][i+1].sx[s_tr], sy_tr = surf[j][i+1].sy[s_tr], dz_tr = surf[j][i+1].dz[s_tr] + bias;
                        int sx_bl = surf[j+1][i].sx[s_bl], sy_bl = surf[j+1][i].sy[s_bl], dz_bl = surf[j+1][i].dz[s_bl] + bias;
                        int sx_br = surf[j+1][i+1].sx[s_br], sy_br = surf[j+1][i+1].sy[s_br], dz_br = surf[j+1][i+1].dz[s_br] + bias;

                        float avg_nx = surf[j][i].nx[s] + surf[j][i+1].nx[s_tr] + surf[j+1][i].nx[s_bl] + surf[j+1][i+1].nx[s_br];
                        float avg_ny = surf[j][i].ny[s] + surf[j][i+1].ny[s_tr] + surf[j+1][i].ny[s_bl] + surf[j+1][i+1].ny[s_br];
                        float avg_nz = surf[j][i].nz[s] + surf[j][i+1].nz[s_tr] + surf[j+1][i].nz[s_bl] + surf[j+1][i+1].nz[s_br];
                        avg_nx *= 0.25f; avg_ny *= 0.25f; avg_nz *= 0.25f;

                        float nlen = (float)sqrt(avg_nx*avg_nx + avg_ny*avg_ny + avg_nz*avg_nz);
                        if (nlen > 1e-9) { avg_nx /= nlen; avg_ny /= nlen; avg_nz /= nlen; }
                        else { avg_nx = 0; avg_ny = 0; avg_nz = 1; }

                        uint32_t scol = apply_shading(col, avg_nx, avg_ny, avg_nz);
                        gfb_triangle_z(sx0, sy0, dz0, sx_tr, sy_tr, dz_tr, sx_bl, sy_bl, dz_bl, scol);
                        gfb_triangle_z(sx_tr, sy_tr, dz_tr, sx_br, sy_br, dz_br, sx_bl, sy_bl, dz_bl, scol);
                    } else if (v_tr && v_bl) {
                        uint32_t scol = apply_shading(col, surf[j][i].nx[s], surf[j][i].ny[s], surf[j][i].nz[s]);
                        gfb_triangle_z(sx0, sy0, dz0,
                                       surf[j][i+1].sx[s_tr], surf[j][i+1].sy[s_tr], surf[j][i+1].dz[s_tr],
                                       surf[j+1][i].sx[s_bl], surf[j+1][i].sy[s_bl], surf[j+1][i].dz[s_bl], scol);
                    } else if (v_tr && v_br) {
                        uint32_t scol = apply_shading(col, surf[j][i].nx[s], surf[j][i].ny[s], surf[j][i].nz[s]);
                        gfb_triangle_z(sx0, sy0, dz0,
                                       surf[j][i+1].sx[s_tr], surf[j][i+1].sy[s_tr], surf[j][i+1].dz[s_tr],
                                       surf[j+1][i+1].sx[s_br], surf[j+1][i+1].sy[s_br], surf[j+1][i+1].dz[s_br], scol);
                    } else if (v_bl && v_br) {
                        uint32_t scol = apply_shading(col, surf[j][i].nx[s], surf[j][i].ny[s], surf[j][i].nz[s]);
                        gfb_triangle_z(sx0, sy0, dz0,
                                       surf[j+1][i].sx[s_bl], surf[j+1][i].sy[s_bl], surf[j+1][i].dz[s_bl],
                                       surf[j+1][i+1].sx[s_br], surf[j+1][i+1].sy[s_br], surf[j+1][i+1].dz[s_br], scol);
                    }
                } else {
                    if (i + 1 < GRID_3D && s_tr >= 0) {
                        gfb_line_z(sx0, sy0, dz0, surf[j][i+1].sx[s_tr], surf[j][i+1].sy[s_tr], surf[j][i+1].dz[s_tr], col);
                    }
                    if (j + 1 < GRID_3D && s_bl >= 0) {
                        gfb_line_z(sx0, sy0, dz0, surf[j+1][i].sx[s_bl], surf[j+1][i].sy[s_bl], surf[j+1][i].dz[s_bl], col);
                    }
                }
            }


        }
    }
}

static void render_3d_explicit(void) {
    double step = range_3d * 2.0 * 1.05 / (GRID_3D - 1);
    double zmin = 1e30, zmax = -1e30;

    if (surface_needs_eval) {
        for (int j = 0; j < GRID_3D; j++) { 
            for (int i = 0; i < GRID_3D; i++) { 
                surf[j][i].count = 0;
            } 
        }

        int num_chunks = 4;
        eval_job_t jobs[4];
        void *job_args[4];
        int rows_per_chunk = GRID_3D / num_chunks;
        
        for (int c = 0; c < num_chunks; c++) {
            jobs[c].start_j = c * rows_per_chunk;
            jobs[c].end_j = (c == num_chunks - 1) ? GRID_3D : (c + 1) * rows_per_chunk;
            jobs[c].range = range_3d;
            jobs[c].step = step;
            jobs[c].march_axis = 0;
            job_args[c] = &jobs[c];
        }

        sys_parallel_run(eval_3d_explicit_job, job_args, num_chunks);
    }
    
    // Compute min/max for coloring
    for (int j = 0; j < GRID_3D; j++) {
        for (int i = 0; i < GRID_3D; i++) {
            for (int s = 0; s < surf[j][i].count; s++) {
                if (surf[j][i].z[s] < zmin) zmin = surf[j][i].z[s];
                if (surf[j][i].z[s] > zmax) zmax = surf[j][i].z[s];
            }
        }
    }

    double z_scale = 1.0;
    if (zmax > zmin && (zmax - zmin) > 0.001) {
        z_scale = (range_3d * 2.0) / (zmax - zmin);
    }

    {
        int num_chunks = 4;
        eval_job_t jobs[4];
        void *job_args[4];
        int rows_per_chunk = GRID_3D / num_chunks;
        for (int c = 0; c < num_chunks; c++) {
            jobs[c].start_j = c * rows_per_chunk;
            jobs[c].end_j = (c == num_chunks - 1) ? GRID_3D : (c + 1) * rows_per_chunk;
            jobs[c].range = range_3d;
            jobs[c].step = step;
            jobs[c].z_scale = z_scale;
            jobs[c].march_axis = 0;
            job_args[c] = &jobs[c];
        }
        sys_parallel_run(eval_3d_explicit_project_job, job_args, num_chunks);
    }
    
    {
        int num_chunks = 4;
        draw_job_t jobs[4];
        void *job_args[4];
        int rows_per_chunk = GRID_3D / num_chunks;
        for (int c = 0; c < num_chunks; c++) {
            jobs[c].start_j = c * rows_per_chunk;
            jobs[c].end_j = (c == num_chunks - 1) ? GRID_3D : (c + 1) * rows_per_chunk;
            jobs[c].zmin = zmin;
            jobs[c].zmax = zmax;
            jobs[c].march_axis = 0;
            jobs[c].normal_axis = -1;
            job_args[c] = &jobs[c];
        }
        sys_parallel_run(render_3d_draw_job, job_args, num_chunks);
    }
}

static void render_3d_implicit(void) {
    double step = range_3d * 2.0 * 1.05 / (GRID_3D - 1);

    if (surface_needs_eval) {
        int num_chunks = 4, rows_per_chunk = GRID_3D / num_chunks;
        eval_job_t jobs[4]; void *job_args[4];
        for (int axis = 0; axis < 3; axis++) {
            for (int c = 0; c < num_chunks; c++) {
                jobs[c].start_j    = c * rows_per_chunk;
                jobs[c].end_j      = (c == num_chunks-1) ? GRID_3D : (c+1)*rows_per_chunk;
                jobs[c].range      = range_3d;
                jobs[c].step       = step;
                jobs[c].z_scale    = 1.0;
                jobs[c].march_axis = axis;
                job_args[c] = &jobs[c];
            }
            sys_parallel_run(eval_3d_implicit_job, job_args, num_chunks);
        }
        surface_needs_eval = false;
    }

    double zmin = 1e30, zmax = -1e30;
    for (int j = 0; j < GRID_3D; j++)
        for (int i = 0; i < GRID_3D; i++) {
            int cnt = eval_cache[0][j][i].count;
            for (int s = 0; s < cnt; s++) {
                float z = eval_cache[0][j][i].c[s];
                if (z < zmin) zmin = z;
                if (z > zmax) zmax = z;
            }
        }
    if (zmin > zmax) { zmin = -(float)range_3d; zmax = (float)range_3d; }

    static const int normal_axes[3] = {0, 1, 2};
    int num_chunks = 4, rows_per_chunk = GRID_3D / num_chunks;

    for (int axis = 0; axis < 3; axis++) {
        {
            eval_job_t jobs[4]; void *job_args[4];
            for (int c = 0; c < num_chunks; c++) {
                jobs[c].start_j    = c * rows_per_chunk;
                jobs[c].end_j      = (c == num_chunks-1) ? GRID_3D : (c+1)*rows_per_chunk;
                jobs[c].range      = range_3d;
                jobs[c].step       = step;
                jobs[c].z_scale    = 1.0;
                jobs[c].march_axis = axis;
                job_args[c] = &jobs[c];
            }
            sys_parallel_run(eval_3d_project_job, job_args, num_chunks);
        }
        {
            draw_job_t jobs[4]; void *job_args[4];
            for (int c = 0; c < num_chunks; c++) {
                jobs[c].start_j    = c * rows_per_chunk;
                jobs[c].end_j      = (c == num_chunks-1) ? GRID_3D : (c+1)*rows_per_chunk;
                jobs[c].zmin       = zmin;
                jobs[c].zmax       = zmax;
                jobs[c].march_axis = axis;
                jobs[c].normal_axis = filled_mode ? normal_axes[axis] : -1;
                job_args[c] = &jobs[c];
            }
            sys_parallel_run(render_3d_draw_job, job_args, num_chunks);
        }
    }
}

static void double_to_str(double val, char *buf) {
    if (val != val) { strcpy(buf, "NaN"); return; }
    if (val > 1e15) { strcpy(buf, "inf"); return; }
    if (val < -1e15) { strcpy(buf, "-inf"); return; }
    
    if (val > 2e9) {
        strcpy(buf, ">2G");
        return;
    }
    if (val < -2e9) {
        strcpy(buf, "<-2G");
        return;
    }

    if (val < 0) { *buf++ = '-'; val = -val; }
    
    int ipart = (int)val;
    itoa(ipart, buf);
    while (*buf) buf++;
    double frac = val - (double)ipart;
    if (frac > 0.005) {
        *buf++ = '.';
        int d1 = (int)(frac * 10) % 10;
        int d2 = (int)(frac * 100) % 10;
        *buf++ = '0' + d1;
        if (d2) *buf++ = '0' + d2;
    }
    *buf = 0;
}

static void render_graph(void) {
    gfb_clear(COLOR_BG);

    if (!eq_valid) {
        if (graph_mode == MODE_2D) render_2d_grid();
        ui_draw_image(win_graph, 0, GRAPH_Y, graph_w, graph_h, graph_fb);
        return;
    }

    if (graph_mode == MODE_2D) {
        if (is_explicit_2d) render_2d_explicit(); 
        render_2d_grid();
        if (is_explicit_2d) {
            int prev_sx = -1, prev_sy = -1;
            bool prev_valid = false;
            for (int px = 0; px < graph_w; px++) {
                double wx = world_x_2d(px);
                double wy = eval_rhs_only(wx, 0, 0);
                if (wy != wy || fabs(wy) > 1e10) { prev_valid = false; continue; }
                int sy = screen_y_2d(wy);
                if (prev_valid && fabs((double)(sy - prev_sy)) < graph_h)
                    gfb_line(prev_sx, prev_sy, px, sy, COLOR_CURVE);
                prev_sx = px; prev_sy = sy; prev_valid = true;
            }
        } else render_2d_implicit();
    } else {
        render_3d_axes();
        if (is_explicit_3d) render_3d_explicit();
        else render_3d_implicit();
    }

    if (graph_mode == MODE_3D && graph_czb) {
        int total = graph_w * graph_h;
        for (int i = 0; i < total; i++) {
            graph_fb[i] = (uint32_t)(graph_czb[i] & 0xFFFFFFFF);
        }
    }

    ui_draw_image(win_graph, 0, GRAPH_Y, graph_w, graph_h, graph_fb);

    if (graph_mode == MODE_2D) {
        double x_step = get_nice_step(view_x_max - view_x_min, 8);
        double y_step = get_nice_step(view_y_max - view_y_min, 6);

        int axis_y = screen_y_2d(0);
        if (axis_y < 10) axis_y = 10;
        if (axis_y > graph_h - 20) axis_y = graph_h - 20;
        axis_y += GRAPH_Y;

        double x_start = (int)(view_x_min / x_step) * x_step;
        int safety = 0;
        for (double wx = x_start - x_step; wx <= view_x_max + x_step && safety < 100; wx += x_step, safety++) {
            if (fabs(wx) < x_step * 0.1) continue; 
            int sx = screen_x_2d(wx);
            if (sx > 20 && sx < graph_w - 40) {
                char buf[32]; double_to_str(wx, buf);
                ui_draw_string(win_graph, sx - 10, axis_y + 4, buf, COLOR_TEXT);
            }
        }

        int axis_x = screen_x_2d(0);
        if (axis_x < 5) axis_x = 5;
        if (axis_x > graph_w - 40) axis_x = graph_w - 40;

        double y_start = (int)(view_y_min / y_step) * y_step;
        safety = 0;
        for (double wy = y_start - y_step; wy <= view_y_max + y_step && safety < 100; wy += y_step, safety++) {
            if (fabs(wy) < y_step * 0.1) continue; 
            int sy = screen_y_2d(wy);
            if (sy > 20 && sy < graph_h - 20) {
                char buf[32]; double_to_str(wy, buf);
                ui_draw_string(win_graph, axis_x + 6, sy + GRAPH_Y - 5, buf, COLOR_TEXT);
            }
        }
        
        int zx = screen_x_2d(0), zy = screen_y_2d(0);
        if (zx >= 0 && zx < graph_w - 10 && zy >= 0 && zy < graph_h - 10) {
            ui_draw_string(win_graph, zx + 4, zy + GRAPH_Y + 4, "0", COLOR_TEXT);
        }
    }
}

// =====
// Paint
// =====
static void paint_all(void) {
    rot_cx = cos(rot_x); rot_sx = sin(rot_x);
    rot_cy = cos(rot_y); rot_sy = sin(rot_y);

    ui_draw_rect(win_graph, 0, 0, win_w, TOOLBAR_H, COLOR_TOOLBAR_BG);
    widget_textbox_draw(&wctx, &tb_equation);
    widget_button_draw(&wctx, &btn_plot);
    widget_button_draw(&wctx, &btn_mode);
    ui_draw_rounded_rect_filled(win_graph, win_w - 80, 4, 70, 22, 4, 0xFF3A3A5A);
    ui_draw_string(win_graph, win_w - 72, 8, "Presets", COLOR_DARK_TEXT);
    render_graph();

    // Status bar
    int sty = GRAPH_Y + graph_h;
    ui_draw_rect(win_graph, 0, sty, win_w, STATUSBAR_H, COLOR_STATUS_BG);
    ui_draw_string(win_graph, 10, sty + 8,
                   graph_mode == MODE_3D ? "3D | CPU INTENSIVE!!" :
                                           "2D | Scroll=Zoom", COLOR_TEXT);

    char range_buf[64];
    if (graph_mode == MODE_2D) {
        strcpy(range_buf, "x:[");
        char tmp[16]; double_to_str(view_x_min, tmp); strcat(range_buf, tmp);
        strcat(range_buf, ",");
        double_to_str(view_x_max, tmp); strcat(range_buf, tmp);
        strcat(range_buf, "]");
        ui_draw_string(win_graph, win_w - 150, sty + 8, range_buf, COLOR_TEXT);
    } else {
        strcpy(range_buf, "Range:");
        char tmp[16]; double_to_str(range_3d, tmp); strcat(range_buf, tmp);
        ui_draw_string(win_graph, win_w - 200, sty + 8, range_buf, COLOR_TEXT);
        widget_button_draw(&wctx, &btn_range_minus);
        widget_button_draw(&wctx, &btn_range_plus);
    }

    if (presets_open) {
        int px = win_w - 150, py = TOOLBAR_H;
        ui_draw_rounded_rect_filled(win_graph, px, py, 140, NUM_PRESETS * 20 + 4, 4, COLOR_DARK_PANEL);
        for (int i = 0; i < NUM_PRESETS; i++) {
            ui_draw_string(win_graph, px + 8, py + 4 + i * 20, preset_labels[i], COLOR_DARK_TEXT);
        }
    }

    ui_mark_dirty(win_graph, 0, 0, win_w, CLIENT_H);
    surface_needs_eval = false;
}

// ====
// Zoom
// ====
static void reset_view(void) {
    if (graph_mode == MODE_2D) {
        view_x_min = -10.0; view_x_max = 10.0;
        view_y_min = -6.4; view_y_max = 6.4;
        apply_aspect_ratio();
    } else {
        zoom_3d = 1.0;
        rot_x = -0.5;
        rot_y = 0.5;
        range_3d = 10.0;
    }
}

static void zoom_2d(double factor) {
    if (factor <= 0) return;
    
    double cx = (view_x_min + view_x_max) / 2.0;
    double cy = (view_y_min + view_y_max) / 2.0;
    
    double half_x = (view_x_max - view_x_min) / 2.0;
    double half_y = (view_y_max - view_y_min) / 2.0;
    
    half_x *= factor;
    half_y *= factor;
    
    // Safety caps for zoom range
    if (half_x < 1e-12) half_x = 1e-12;
    if (half_x > 1e12) half_x = 1e12;
    if (half_y < 1e-12) half_y = 1e-12;
    if (half_y > 1e12) half_y = 1e12;

    view_x_min = cx - half_x;
    view_x_max = cx + half_x;
    view_y_min = cy - half_y;
    view_y_max = cy + half_y;
    
    apply_aspect_ratio();
}

static void handle_scroll(int dz) {
    if (graph_mode == MODE_2D) {
        surface_needs_eval = true;
        if (dz > 0) zoom_2d(0.85);
        else zoom_2d(1.18);
    } else {
        if (dz > 0) zoom_3d *= 1.15;
        else zoom_3d *= 0.87;
    }
}

static void update_widget_layout(void) {
    tb_equation.w = win_w - 220;
    btn_plot.x = win_w - 200;
    widget_button_init(&btn_mode, win_w - 145, 4, 60, 22, filled_mode ? "Wire" : "Filled");
    
    int sty = win_h - STATUSBAR_H - 20;
    widget_button_init(&btn_range_minus, win_w - 95, sty + 4, 30, 22, "-");
    widget_button_init(&btn_range_plus, win_w - 55, sty + 4, 30, 22, "+");
}

// ====
// Main
// ====
int main(void) {
    win_graph = ui_window_create("Grapher", 80, 60, win_w, win_h);
    ui_window_set_resizable(win_graph, true);
    wctx.user_data = (void *)win_graph;

    fb_capacity = graph_w * graph_h;
    graph_fb = (uint32_t *)malloc(fb_capacity * sizeof(uint32_t));
    graph_zb = (int32_t *)malloc(fb_capacity * sizeof(int32_t));
    graph_czb = (uint64_t *)malloc(fb_capacity * sizeof(uint64_t));
    if (!graph_fb || !graph_zb || !graph_czb) return 1;

    memset(eq_buffer, 0, sizeof(eq_buffer));
    strcpy(eq_buffer, "y = sin(x)");
    eq_len = strlen(eq_buffer);

    widget_textbox_init(&tb_equation, 10, 4, 340, 22, eq_buffer, 255);
    tb_equation.cursor_pos = eq_len;
    widget_button_init(&btn_plot, 360, 4, 50, 22, "Plot");
    update_widget_layout();

    // Parse initial equation
    parse_equation();

    paint_all();

    gui_event_t ev;
    bool needs_repaint = false;
    while (1) {
        bool got_event = false;
        while (ui_get_event(win_graph, &ev)) {
            got_event = true;
            if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            } else if (ev.type == GUI_EVENT_PAINT) {
                needs_repaint = true;
            } else if (ev.type == GUI_EVENT_KEY) {
                char c = (char)ev.arg1;
                if (tb_equation.focused) {
                    if (c == '\n') {
                        eq_len = strlen(eq_buffer);
                        parse_equation();
                        if (graph_mode == MODE_2D) autofit_2d_view();
                        needs_repaint = true;
                    } else {
                        widget_textbox_handle_key(&tb_equation, c, NULL);
                        eq_len = strlen(eq_buffer);
                        needs_repaint = true;
                    }
                } else if ((c == 'r' || c == 'R') && ev.arg3 == 1) { 
                    reset_view();
                    surface_needs_eval = true;
                    needs_repaint = true;
                } else if ((c == 'f' || c == 'F')) {
                    filled_mode = !filled_mode;
                    update_widget_layout();
                    needs_repaint = true;
                }
            } else if (ev.type == GUI_EVENT_RESIZE) {
                win_w = ev.arg1;
                win_h = ev.arg2;
                graph_w = win_w;
                graph_h = (win_h - 20) - TOOLBAR_H - STATUSBAR_H;
                if (graph_h < 50) graph_h = 50;
                
                int req_cap = graph_w * graph_h;
                if (req_cap > fb_capacity) {
                    if (graph_fb) free(graph_fb);
                    if (graph_zb) free(graph_zb);
                    if (graph_czb) free(graph_czb);
                    fb_capacity = (int)(req_cap * 1.5);
                    graph_fb = (uint32_t *)malloc(fb_capacity * sizeof(uint32_t));
                    graph_zb = (int32_t *)malloc(fb_capacity * sizeof(int32_t));
                    graph_czb = (uint64_t *)malloc(fb_capacity * sizeof(uint64_t));
                }
                
                update_widget_layout();
                
                if (graph_mode == MODE_2D) {
                    apply_aspect_ratio();
                }
                surface_needs_eval = true;
                needs_repaint = true;
            } else if (ev.type == GUI_EVENT_CLICK) {
                int mx = ev.arg1, my = ev.arg2;

                if (presets_open) {
                    int px = win_w - 150, py = TOOLBAR_H;
                    if (mx >= px && mx < px + 140 && my >= py && my < py + NUM_PRESETS * 20 + 4) {
                        int idx = (my - py - 2) / 20;
                        if (idx >= 0 && idx < NUM_PRESETS) {
                            strcpy(eq_buffer, preset_labels[idx]);
                            eq_len = strlen(eq_buffer);
                            tb_equation.cursor_pos = eq_len;
                            parse_equation();
                            if (graph_mode == MODE_2D) autofit_2d_view();
                        }
                    }
                    presets_open = false;
                    needs_repaint = true;
                    continue;
                }

                if (mx >= win_w - 80 && mx < win_w - 10 && my >= 4 && my < 26) {
                    presets_open = !presets_open;
                    needs_repaint = true;
                    continue;
                }

                if (widget_button_handle_mouse(&btn_plot, mx, my, false, true, NULL)) {
                    parse_equation();
                    if (graph_mode == MODE_2D) autofit_2d_view();
                    needs_repaint = true;
                    continue;
                }
                
                if (widget_button_handle_mouse(&btn_mode, mx, my, false, true, NULL)) {
                    filled_mode = !filled_mode;
                    update_widget_layout();
                    needs_repaint = true;
                    continue;
                }
                
                if (graph_mode == MODE_3D) {
                    if (widget_button_handle_mouse(&btn_range_plus, mx, my, false, true, NULL)) {
                        range_3d *= 1.25;
                        surface_needs_eval = true;
                        needs_repaint = true;
                        continue;
                    }
                    if (widget_button_handle_mouse(&btn_range_minus, mx, my, false, true, NULL)) {
                        range_3d *= 0.8;
                        surface_needs_eval = true;
                        needs_repaint = true;
                        continue;
                    }
                }

                widget_textbox_handle_mouse(&tb_equation, mx, my, true, NULL);
                needs_repaint = true;

            } else if (ev.type == GUI_EVENT_MOUSE_DOWN) {
                int mx = ev.arg1, my = ev.arg2;
                widget_button_handle_mouse(&btn_plot, mx, my, true, false, NULL);
                widget_button_handle_mouse(&btn_mode, mx, my, true, false, NULL);
                if (graph_mode == MODE_3D) {
                    widget_button_handle_mouse(&btn_range_plus, mx, my, true, false, NULL);
                    widget_button_handle_mouse(&btn_range_minus, mx, my, true, false, NULL);
                }
                needs_repaint = true;

            } else if (ev.type == GUI_EVENT_MOUSE_UP) {
                int mx = ev.arg1, my = ev.arg2;
                widget_button_handle_mouse(&btn_plot, mx, my, false, false, NULL);
                widget_button_handle_mouse(&btn_mode, mx, my, false, false, NULL);
                if (graph_mode == MODE_3D) {
                    widget_button_handle_mouse(&btn_range_plus, mx, my, false, false, NULL);
                    widget_button_handle_mouse(&btn_range_minus, mx, my, false, false, NULL);
                }
                needs_repaint = true;

            } else if (ev.type == GUI_EVENT_RIGHT_CLICK) {
                if (graph_mode == MODE_3D) {
                    right_dragging = true;
                    drag_last_x = ev.arg1;
                    drag_last_y = ev.arg2;
                }
            } else if (ev.type == GUI_EVENT_MOUSE_MOVE) {
                int mx = ev.arg1, my = ev.arg2;
                int buttons = ev.arg3;

                if (graph_mode == MODE_3D && (buttons & 2)) {
                    double dx = mx - drag_last_x;
                    double dy = my - drag_last_y;
                    rot_y += dx * 0.01;
                    rot_x += dy * 0.01;
                    drag_last_x = mx;
                    drag_last_y = my;
                    needs_repaint = true;
                } else {
                    right_dragging = false;
                }
            } else if (ev.type == 9) { 
                handle_scroll(ev.arg1);
                if (eq_valid) needs_repaint = true;
            }
        }
        
        if (graph_mode == MODE_3D && ROTATE == 1) {
            rot_y += 0.01;
            needs_repaint = true;
        }

        if (needs_repaint) {
            paint_all();
            needs_repaint = false;
        }

        if (!got_event) {
            sleep(16);
        }
    }

    free(graph_fb);
    sys_exit(0);
    return 0;
}

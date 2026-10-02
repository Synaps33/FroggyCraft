/*
 * FrogUI Calculator
 * v75: 15 decimal places precision using string arithmetic
 */

#include "calculator.h"
#include "render.h"
#include "font.h"
#include "gfx_theme.h"
#include <string.h>
#include <stdio.h>

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Layout
#define CALC_WIN_X 30
#define CALC_WIN_Y 12
#define CALC_WIN_W 260
#define CALC_WIN_H 215
#define CALC_RADIUS 8

#define CALC_EXPR_X (CALC_WIN_X + 10)
#define CALC_EXPR_Y (CALC_WIN_Y + 6)

#define CALC_DISP_X (CALC_WIN_X + 10)
#define CALC_DISP_Y (CALC_WIN_Y + 24)
#define CALC_DISP_W (CALC_WIN_W - 20)
#define CALC_DISP_H 26

#define CALC_BTN_COLS 5
#define CALC_BTN_ROWS 4
#define CALC_BTN_W 46
#define CALC_BTN_H 30
#define CALC_BTN_GAP 4
#define CALC_BTN_START_X (CALC_WIN_X + 12)
#define CALC_BTN_START_Y (CALC_WIN_Y + 58)
#define CALC_BTN_RADIUS 4

// Colors
#define COL_WIN_BG      0x2104
#define COL_DISP_BG     0x18C3
#define COL_DISP_TEXT   0xFFFF
#define COL_EXPR_TEXT   0x8410
#define COL_BTN_NUM     0x3186
#define COL_BTN_OP      0x4228
#define COL_BTN_TEXT    0xFFFF
#define COL_BTN_SEL     0x04FF
#define COL_BTN_SEL_BG  0x5ACB

#define BTN_NUM     0
#define BTN_OP      1
#define BTN_SPECIAL 2
#define BTN_EQUALS  3

static const char* btn_labels[CALC_BTN_ROWS][CALC_BTN_COLS] = {
    {"7", "8", "9", "/", "C"},
    {"4", "5", "6", "*", "CE"},
    {"1", "2", "3", "-", "<"},
    {"0", ".", "=", "+", ""}
};

static const int btn_types[CALC_BTN_ROWS][CALC_BTN_COLS] = {
    {BTN_NUM, BTN_NUM, BTN_NUM, BTN_OP, BTN_SPECIAL},
    {BTN_NUM, BTN_NUM, BTN_NUM, BTN_OP, BTN_SPECIAL},
    {BTN_NUM, BTN_NUM, BTN_NUM, BTN_OP, BTN_SPECIAL},
    {BTN_NUM, BTN_NUM, BTN_EQUALS, BTN_OP, BTN_NUM}
};

// Calculator state
static int calc_active = 0;
static int sel_row = 0;
static int sel_col = 0;

// Display/calculation buffers - enough for 15 integer + 15 decimal digits
#define MAX_DIGITS 32
static char display[MAX_DIGITS + 1];
static int display_len = 0;

#define MAX_EXPR 50
static char expression[MAX_EXPR + 1];

// Accumulator stored as string
static char accumulator[MAX_DIGITS + 1];
static char pending_op = 0;
static int has_decimal = 0;
static int new_number = 1;
static int error_state = 0;
static int just_calculated = 0;

// Decimal precision for division
#define DECIMAL_PRECISION 15

// Forward declarations
static void calc_clear_all(void);
static void calc_clear_entry(void);
static void calc_backspace(void);
static void calc_digit(char digit);
static void calc_decimal(void);
static void calc_operator(char op);
static void calc_equals(void);

// String arithmetic functions
static void str_add(const char *a, const char *b, char *result);
static void str_sub(const char *a, const char *b, char *result);
static void str_mul(const char *a, const char *b, char *result);
static void str_div(const char *a, const char *b, char *result, int precision);
static int str_compare_abs(const char *a, const char *b);
static void str_normalize(char *s);

// Helper: Draw rounded rectangle
static void draw_rounded_rect(uint16_t *fb, int x, int y, int w, int h, int r, uint16_t color) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            int dx = 0, dy = 0;
            if (px < x + r && py < y + r) { dx = x + r - px; dy = y + r - py; }
            else if (px >= x + w - r && py < y + r) { dx = px - (x + w - r - 1); dy = y + r - py; }
            else if (px < x + r && py >= y + h - r) { dx = x + r - px; dy = py - (y + h - r - 1); }
            else if (px >= x + w - r && py >= y + h - r) { dx = px - (x + w - r - 1); dy = py - (y + h - r - 1); }
            if (dx * dx + dy * dy <= r * r || (dx == 0 && dy == 0)) {
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT)
                    fb[py * SCREEN_WIDTH + px] = color;
            }
        }
    }
}

void calc_init(void) { calc_active = 0; calc_clear_all(); }
void calc_open(void) { calc_active = 1; calc_clear_all(); sel_row = 0; sel_col = 0; }
void calc_close(void) { calc_active = 0; }
int calc_is_active(void) { return calc_active; }

static void calc_clear_all(void) {
    strcpy(display, "0");
    display_len = 1;
    expression[0] = '\0';
    accumulator[0] = '\0';
    pending_op = 0;
    has_decimal = 0;
    new_number = 1;
    error_state = 0;
    just_calculated = 0;
}

static void calc_clear_entry(void) {
    strcpy(display, "0");
    display_len = 1;
    has_decimal = 0;
    error_state = 0;
}

static void calc_backspace(void) {
    if (error_state || just_calculated) {
        calc_clear_entry();
        just_calculated = 0;
        return;
    }
    if (display_len > 1) {
        if (display[display_len - 1] == '.') has_decimal = 0;
        display[--display_len] = '\0';
    } else if (display[0] != '0') {
        strcpy(display, "0");
        display_len = 1;
    }
}

static void calc_digit(char digit) {
    if (error_state) calc_clear_all();
    if (new_number || just_calculated) {
        display[0] = digit;
        display[1] = '\0';
        display_len = 1;
        has_decimal = 0;
        new_number = 0;
        just_calculated = 0;
    } else if (display_len < MAX_DIGITS - 1) {
        if (display_len == 1 && display[0] == '0' && !has_decimal) {
            if (digit == '0') return;
            display[0] = digit;
        } else {
            display[display_len++] = digit;
            display[display_len] = '\0';
        }
    }
}

static void calc_decimal(void) {
    if (error_state) calc_clear_all();
    if (new_number || just_calculated) {
        strcpy(display, "0.");
        display_len = 2;
        has_decimal = 1;
        new_number = 0;
        just_calculated = 0;
    } else if (!has_decimal && display_len < MAX_DIGITS - 1) {
        display[display_len++] = '.';
        display[display_len] = '\0';
        has_decimal = 1;
    }
}

// Remove leading zeros and trailing zeros after decimal
static void str_normalize(char *s) {
    int neg = (s[0] == '-');
    char *start = s + neg;

    // Remove leading zeros
    while (*start == '0' && *(start+1) && *(start+1) != '.') start++;
    if (neg && start != s + 1) {
        memmove(s + 1, start, strlen(start) + 1);
    } else if (!neg && start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    // Remove trailing zeros after decimal
    char *dot = strchr(s, '.');
    if (dot) {
        int len = strlen(s);
        while (len > 1 && s[len-1] == '0') s[--len] = '\0';
        if (s[len-1] == '.') s[--len] = '\0';
    }

    // Handle "-0"
    if (strcmp(s, "-0") == 0) strcpy(s, "0");
}

// Compare absolute values: returns 1 if |a|>|b|, -1 if |a|<|b|, 0 if equal
static int str_compare_abs(const char *a, const char *b) {
    // Skip signs
    if (*a == '-') a++;
    if (*b == '-') b++;

    // Find decimal points
    const char *da = strchr(a, '.');
    const char *db = strchr(b, '.');
    int int_a = da ? (da - a) : strlen(a);
    int int_b = db ? (db - b) : strlen(b);

    // Compare integer part lengths
    if (int_a != int_b) return int_a > int_b ? 1 : -1;

    // Compare digit by digit
    while (*a && *b) {
        if (*a == '.') { a++; continue; }
        if (*b == '.') { b++; continue; }
        if (*a != *b) return *a > *b ? 1 : -1;
        a++; b++;
    }
    while (*a) { if (*a != '.' && *a != '0') return 1; a++; }
    while (*b) { if (*b != '.' && *b != '0') return -1; b++; }
    return 0;
}

// Add two positive decimal strings
static void add_positive(const char *a, const char *b, char *result) {
    char aa[MAX_DIGITS*2], bb[MAX_DIGITS*2];
    strcpy(aa, a); strcpy(bb, b);

    // Align decimal points
    char *da = strchr(aa, '.'), *db = strchr(bb, '.');
    int fa = da ? strlen(da+1) : 0;
    int fb = db ? strlen(db+1) : 0;
    int maxf = fa > fb ? fa : fb;

    // Pad with zeros
    if (!da) { strcat(aa, "."); da = aa + strlen(aa) - 1; }
    if (!db) { strcat(bb, "."); db = bb + strlen(bb) - 1; }
    while (fa < maxf) { strcat(aa, "0"); fa++; }
    while (fb < maxf) { strcat(bb, "0"); fb++; }

    // Remove decimal for calculation
    memmove(da, da+1, strlen(da));
    db = strchr(bb, '.');
    memmove(db, db+1, strlen(db));

    // Pad integer part
    int la = strlen(aa), lb = strlen(bb);
    int maxl = la > lb ? la : lb;
    char pa[MAX_DIGITS*2], pb[MAX_DIGITS*2];
    memset(pa, '0', maxl - la); strcpy(pa + maxl - la, aa);
    memset(pb, '0', maxl - lb); strcpy(pb + maxl - lb, bb);

    // Add
    char res[MAX_DIGITS*2];
    int carry = 0;
    for (int i = maxl - 1; i >= 0; i--) {
        int sum = (pa[i] - '0') + (pb[i] - '0') + carry;
        res[i+1] = '0' + (sum % 10);
        carry = sum / 10;
    }
    res[0] = '0' + carry;
    res[maxl + 1] = '\0';

    // Insert decimal point
    if (maxf > 0) {
        int len = strlen(res);
        memmove(res + len - maxf + 1, res + len - maxf, maxf + 1);
        res[len - maxf] = '.';
    }

    strcpy(result, res);
    str_normalize(result);
}

// Subtract b from a (assumes a >= b, both positive)
static void sub_positive(const char *a, const char *b, char *result) {
    char aa[MAX_DIGITS*2], bb[MAX_DIGITS*2];
    strcpy(aa, a); strcpy(bb, b);

    // Align decimals
    char *da = strchr(aa, '.'), *db = strchr(bb, '.');
    int fa = da ? strlen(da+1) : 0;
    int fb = db ? strlen(db+1) : 0;
    int maxf = fa > fb ? fa : fb;

    if (!da) { strcat(aa, "."); }
    if (!db) { strcat(bb, "."); }
    while (fa < maxf) { strcat(aa, "0"); fa++; }
    while (fb < maxf) { strcat(bb, "0"); fb++; }

    da = strchr(aa, '.'); db = strchr(bb, '.');
    memmove(da, da+1, strlen(da));
    memmove(db, db+1, strlen(db));

    int la = strlen(aa), lb = strlen(bb);
    int maxl = la > lb ? la : lb;
    char pa[MAX_DIGITS*2], pb[MAX_DIGITS*2];
    memset(pa, '0', maxl - la); strcpy(pa + maxl - la, aa);
    memset(pb, '0', maxl - lb); strcpy(pb + maxl - lb, bb);

    // Subtract
    char res[MAX_DIGITS*2];
    int borrow = 0;
    for (int i = maxl - 1; i >= 0; i--) {
        int diff = (pa[i] - '0') - (pb[i] - '0') - borrow;
        if (diff < 0) { diff += 10; borrow = 1; } else borrow = 0;
        res[i] = '0' + diff;
    }
    res[maxl] = '\0';

    if (maxf > 0) {
        int len = strlen(res);
        memmove(res + len - maxf + 1, res + len - maxf, maxf + 1);
        res[len - maxf] = '.';
    }

    strcpy(result, res);
    str_normalize(result);
}

static void str_add(const char *a, const char *b, char *result) {
    int na = (a[0] == '-'), nb = (b[0] == '-');
    const char *pa = na ? a+1 : a;
    const char *pb = nb ? b+1 : b;

    if (!na && !nb) { add_positive(pa, pb, result); }
    else if (na && nb) { add_positive(pa, pb, result); if (result[0] != '0') { memmove(result+1, result, strlen(result)+1); result[0] = '-'; } }
    else {
        int cmp = str_compare_abs(pa, pb);
        if (cmp == 0) { strcpy(result, "0"); }
        else if (cmp > 0) { sub_positive(pa, pb, result); if (na) { memmove(result+1, result, strlen(result)+1); result[0] = '-'; } }
        else { sub_positive(pb, pa, result); if (nb) { memmove(result+1, result, strlen(result)+1); result[0] = '-'; } }
    }
}

static void str_sub(const char *a, const char *b, char *result) {
    char nb[MAX_DIGITS*2];
    if (b[0] == '-') strcpy(nb, b+1);
    else { nb[0] = '-'; strcpy(nb+1, b); }
    if (strcmp(nb, "-0") == 0) strcpy(nb, "0");
    str_add(a, nb, result);
}

static void str_mul(const char *a, const char *b, char *result) {
    int na = (a[0] == '-'), nb = (b[0] == '-');
    const char *pa = na ? a+1 : a;
    const char *pb = nb ? b+1 : b;

    // Count and remove decimals
    char aa[MAX_DIGITS*2], bb[MAX_DIGITS*2];
    strcpy(aa, pa); strcpy(bb, pb);
    char *da = strchr(aa, '.'), *db = strchr(bb, '.');
    int decs = 0;
    if (da) { decs += strlen(da+1); memmove(da, da+1, strlen(da)); }
    if (db) { decs += strlen(db+1); memmove(db, db+1, strlen(db)); }

    int la = strlen(aa), lb = strlen(bb);
    int lr = la + lb;
    char res[MAX_DIGITS*4];
    memset(res, '0', lr);
    res[lr] = '\0';

    for (int i = la - 1; i >= 0; i--) {
        int carry = 0;
        for (int j = lb - 1; j >= 0; j--) {
            int prod = (aa[i] - '0') * (bb[j] - '0') + (res[i+j+1] - '0') + carry;
            res[i+j+1] = '0' + (prod % 10);
            carry = prod / 10;
        }
        res[i] += carry;
    }

    // Insert decimal
    if (decs > 0 && decs < lr) {
        memmove(res + lr - decs + 1, res + lr - decs, decs + 1);
        res[lr - decs] = '.';
    }

    strcpy(result, res);
    str_normalize(result);

    // Apply sign
    if ((na != nb) && strcmp(result, "0") != 0) {
        memmove(result+1, result, strlen(result)+1);
        result[0] = '-';
    }
}

static void str_div(const char *a, const char *b, char *result, int precision) {
    // Check division by zero
    const char *pb = (b[0] == '-') ? b+1 : b;
    int all_zero = 1;
    for (const char *p = pb; *p; p++) if (*p != '0' && *p != '.') { all_zero = 0; break; }
    if (all_zero) { strcpy(result, "Error"); return; }

    int na = (a[0] == '-'), nb = (b[0] == '-');
    const char *pa = na ? a+1 : a;
    pb = nb ? b+1 : b;

    // Convert to integers by removing decimal points
    char dividend[MAX_DIGITS*4], divisor[MAX_DIGITS*4];
    strcpy(dividend, pa); strcpy(divisor, pb);

    char *da = strchr(dividend, '.'), *db = strchr(divisor, '.');
    int fa = da ? strlen(da+1) : 0;
    int fb = db ? strlen(db+1) : 0;

    if (da) memmove(da, da+1, strlen(da));
    if (db) memmove(db, db+1, strlen(db));

    // Scale to same decimal places
    int scale_diff = fa - fb;
    if (scale_diff > 0) {
        int len = strlen(divisor);
        for (int i = 0; i < scale_diff; i++) divisor[len+i] = '0';
        divisor[len + scale_diff] = '\0';
    } else if (scale_diff < 0) {
        int len = strlen(dividend);
        for (int i = 0; i < -scale_diff; i++) dividend[len+i] = '0';
        dividend[len - scale_diff] = '\0';
    }

    // Remove leading zeros
    while (dividend[0] == '0' && dividend[1]) memmove(dividend, dividend+1, strlen(dividend));
    while (divisor[0] == '0' && divisor[1]) memmove(divisor, divisor+1, strlen(divisor));

    // Long division
    char quotient[MAX_DIGITS*4] = "";
    char remainder[MAX_DIGITS*4] = "0";
    int decimal_placed = 0;
    int digits_after_decimal = 0;
    int dividend_pos = 0;
    int dividend_len = strlen(dividend);

    while (digits_after_decimal <= precision) {
        // Bring down next digit
        if (dividend_pos < dividend_len) {
            int rlen = strlen(remainder);
            if (!(rlen == 1 && remainder[0] == '0')) {
                remainder[rlen] = dividend[dividend_pos];
                remainder[rlen+1] = '\0';
            } else {
                remainder[0] = dividend[dividend_pos];
                remainder[1] = '\0';
            }
            dividend_pos++;
        } else {
            if (!decimal_placed) {
                if (strlen(quotient) == 0) strcat(quotient, "0");
                strcat(quotient, ".");
                decimal_placed = 1;
            }
            int rlen = strlen(remainder);
            if (!(rlen == 1 && remainder[0] == '0')) {
                remainder[rlen] = '0';
                remainder[rlen+1] = '\0';
            }
            digits_after_decimal++;
        }

        // Find quotient digit
        int q = 0;
        while (str_compare_abs(remainder, divisor) >= 0) {
            char temp[MAX_DIGITS*4];
            sub_positive(remainder, divisor, temp);
            strcpy(remainder, temp);
            q++;
            if (q > 9) break;
        }

        char qc[2] = {'0' + q, '\0'};
        strcat(quotient, qc);

        // Check if we're done (remainder is 0)
        if (strcmp(remainder, "0") == 0 && dividend_pos >= dividend_len) break;
    }

    strcpy(result, quotient);
    str_normalize(result);

    if ((na != nb) && strcmp(result, "0") != 0) {
        memmove(result+1, result, strlen(result)+1);
        result[0] = '-';
    }
}

static void calc_operator(char op) {
    if (error_state) return;

    if (pending_op && !new_number) {
        calc_equals();
        if (error_state) return;
    }

    strcpy(accumulator, display);
    pending_op = op;
    new_number = 1;
    just_calculated = 0;

    snprintf(expression, MAX_EXPR, "%s %c", display, op);
}

static void calc_equals(void) {
    if (error_state) return;
    if (!pending_op) { just_calculated = 1; return; }

    char result[MAX_DIGITS*4];

    snprintf(expression, MAX_EXPR, "%s %c %s =", accumulator, pending_op, display);

    switch (pending_op) {
        case '+': str_add(accumulator, display, result); break;
        case '-': str_sub(accumulator, display, result); break;
        case '*': str_mul(accumulator, display, result); break;
        case '/': str_div(accumulator, display, result, DECIMAL_PRECISION); break;
        default: strcpy(result, display); break;
    }

    if (strcmp(result, "Error") == 0) {
        strcpy(display, "Div by 0");
        display_len = 8;
        error_state = 1;
    } else {
        // Truncate to display size
        if (strlen(result) > MAX_DIGITS - 1) {
            result[MAX_DIGITS - 1] = '\0';
            str_normalize(result);
        }
        strcpy(display, result);
        display_len = strlen(display);
        has_decimal = (strchr(display, '.') != NULL);
    }

    pending_op = 0;
    new_number = 1;
    just_calculated = 1;
}

static void calc_press_button(int row, int col) {
    if (row < 0 || row >= CALC_BTN_ROWS || col < 0 || col >= CALC_BTN_COLS) return;
    const char* label = btn_labels[row][col];
    if (!label || !label[0]) return;

    char c = label[0];
    if (c >= '0' && c <= '9') calc_digit(c);
    else if (c == '.') calc_decimal();
    else if (c == '+' || c == '-' || c == '*' || c == '/') calc_operator(c);
    else if (c == '=') calc_equals();
    else if (c == 'C' && !label[1]) calc_clear_all();
    else if (strcmp(label, "CE") == 0) calc_clear_entry();
    else if (c == '<') calc_backspace();
}

int calc_handle_input(int up, int down, int left, int right, int a, int b, int x, int y) {
    (void)y;
    if (b) return 1;
    if (x) calc_clear_all();
    if (a) calc_press_button(sel_row, sel_col);

    if (up) { sel_row--; if (sel_row < 0) sel_row = CALC_BTN_ROWS - 1; }
    if (down) { sel_row++; if (sel_row >= CALC_BTN_ROWS) sel_row = 0; }
    if (left) { sel_col--; if (sel_col < 0) sel_col = CALC_BTN_COLS - 1; }
    if (right) { sel_col++; if (sel_col >= CALC_BTN_COLS) sel_col = 0; if (sel_row == CALC_BTN_ROWS - 1 && sel_col >= 4) sel_col = 0; }

    while (btn_labels[sel_row][sel_col][0] == '\0') { sel_col--; if (sel_col < 0) sel_col = CALC_BTN_COLS - 2; }

    return 0;
}

void calc_render(uint16_t *framebuffer) {
    if (!calc_active) return;

    gfx_theme_advance_animation();
    render_clear_screen_gfx(framebuffer);

    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        uint16_t c = framebuffer[i];
        int r = ((c >> 11) & 0x1F) * 2 / 3;
        int g = ((c >> 5) & 0x3F) * 2 / 3;
        int bb = (c & 0x1F) * 2 / 3;
        framebuffer[i] = (r << 11) | (g << 5) | bb;
    }

    draw_rounded_rect(framebuffer, CALC_WIN_X, CALC_WIN_Y, CALC_WIN_W, CALC_WIN_H, CALC_RADIUS, COL_WIN_BG);

    if (expression[0]) {
        int expr_w = builtin_measure_text(expression);
        int expr_x = CALC_WIN_X + CALC_WIN_W - 14 - expr_w;
        if (expr_x < CALC_EXPR_X) expr_x = CALC_EXPR_X;
        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, expr_x, CALC_EXPR_Y, expression, COL_EXPR_TEXT);
    }

    draw_rounded_rect(framebuffer, CALC_DISP_X, CALC_DISP_Y, CALC_DISP_W, CALC_DISP_H, 4, COL_DISP_BG);

    int text_w = builtin_measure_text(display);
    int text_x = CALC_DISP_X + CALC_DISP_W - text_w - 8;
    int text_y = CALC_DISP_Y + (CALC_DISP_H - 8) / 2;
    builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, text_x, text_y, display, COL_DISP_TEXT);

    if (pending_op && new_number) {
        char op_str[2] = {pending_op, '\0'};
        builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, CALC_DISP_X + 4, text_y, op_str, COL_EXPR_TEXT);
    }

    for (int row = 0; row < CALC_BTN_ROWS; row++) {
        for (int col = 0; col < CALC_BTN_COLS; col++) {
            const char* label = btn_labels[row][col];
            if (!label || !label[0]) continue;

            int bx = CALC_BTN_START_X + col * (CALC_BTN_W + CALC_BTN_GAP);
            int by = CALC_BTN_START_Y + row * (CALC_BTN_H + CALC_BTN_GAP);

            uint16_t btn_color = COL_BTN_NUM, text_color = COL_BTN_TEXT;
            if (row == sel_row && col == sel_col) { btn_color = COL_BTN_SEL_BG; text_color = COL_BTN_SEL; }
            else if (btn_types[row][col] == BTN_OP || btn_types[row][col] == BTN_SPECIAL) btn_color = COL_BTN_OP;
            else if (btn_types[row][col] == BTN_EQUALS) btn_color = 0x03EF;

            draw_rounded_rect(framebuffer, bx, by, CALC_BTN_W, CALC_BTN_H, CALC_BTN_RADIUS, btn_color);

            int lw = builtin_measure_text(label);
            builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, bx + (CALC_BTN_W - lw) / 2, by + (CALC_BTN_H - 8) / 2, label, text_color);
        }
    }

    builtin_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, 10, SCREEN_HEIGHT - 12, "A:Select B:Back X:Clear", 0x8410);
}

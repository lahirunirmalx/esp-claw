/*
 * M5Stack Cardputer 56-key matrix driver.
 * See cardputer_kbd.h for the layout and hardware notes.
 */
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "cardputer_kbd.h"

static const char *TAG = "cardputer_kbd";

/* Row-select outputs feeding the 74HC138 (A0, A1, A2). */
#define KBD_OUT_A0   8
#define KBD_OUT_A1   9
#define KBD_OUT_A2   11

/* Column inputs (read with internal pull-ups, active-low on press). */
static const gpio_num_t s_col_pins[7] = {13, 15, 3, 4, 5, 6, 7};

/* Maps the 7-bit column read of a given physical row scan into a logical
 * column x. Each entry: { column_bit_mask, x_when_row_index<=3, x_when>3 }.
 * The folded layout produces two x values per physical column. */
typedef struct { uint8_t mask; uint8_t x_lo; uint8_t x_hi; } col_chart_t;
static const col_chart_t s_col_chart[7] = {
    { 1u << 0, 1,  0 },
    { 1u << 1, 3,  2 },
    { 1u << 2, 5,  4 },
    { 1u << 3, 7,  6 },
    { 1u << 4, 9,  8 },
    { 1u << 5, 11, 10 },
    { 1u << 6, 13, 12 },
};

/* Per-position key labels and primary ASCII (lower-case / unshifted). */
typedef struct { const char *first; const char *second; char ch_first; char ch_second; } key_def_t;

static const key_def_t s_key_map[4][14] = {
    /* y=0 */
    {
        { "`",   "~",   '`',  '~'  },
        { "1",   "!",   '1',  '!'  },
        { "2",   "@",   '2',  '@'  },
        { "3",   "#",   '3',  '#'  },
        { "4",   "$",   '4',  '$'  },
        { "5",   "%",   '5',  '%'  },
        { "6",   "^",   '6',  '^'  },
        { "7",   "&",   '7',  '&'  },
        { "8",   "*",   '8',  '*'  },
        { "9",   "(",   '9',  '('  },
        { "0",   ")",   '0',  ')'  },
        { "-",   "_",   '-',  '_'  },
        { "=",   "+",   '=',  '+'  },
        { "del", "del", '\b', '\b' },
    },
    /* y=1 */
    {
        { "tab", "tab", '\t', '\t' },
        { "q", "Q", 'q', 'Q' }, { "w", "W", 'w', 'W' }, { "e", "E", 'e', 'E' },
        { "r", "R", 'r', 'R' }, { "t", "T", 't', 'T' }, { "y", "Y", 'y', 'Y' },
        { "u", "U", 'u', 'U' }, { "i", "I", 'i', 'I' }, { "o", "O", 'o', 'O' },
        { "p", "P", 'p', 'P' },
        { "[",  "{",  '[', '{' }, { "]",  "}",  ']', '}' },
        { "\\", "|",  '\\', '|' },
    },
    /* y=2 */
    {
        { "fn",    "fn",    0,   0   },
        { "shift", "shift", 0,   0   },
        { "a", "A", 'a', 'A' }, { "s", "S", 's', 'S' }, { "d", "D", 'd', 'D' },
        { "f", "F", 'f', 'F' }, { "g", "G", 'g', 'G' }, { "h", "H", 'h', 'H' },
        { "j", "J", 'j', 'J' }, { "k", "K", 'k', 'K' }, { "l", "L", 'l', 'L' },
        { ";", ":", ';', ':' }, { "'", "\"", '\'', '"' },
        { "enter", "enter", '\n', '\n' },
    },
    /* y=3 */
    {
        { "ctrl", "ctrl", 0, 0 },
        { "opt",  "opt",  0, 0 },
        { "alt",  "alt",  0, 0 },
        { "z", "Z", 'z', 'Z' }, { "x", "X", 'x', 'X' }, { "c", "C", 'c', 'C' },
        { "v", "V", 'v', 'V' }, { "b", "B", 'b', 'B' }, { "n", "N", 'n', 'N' },
        { "m", "M", 'm', 'M' },
        { ",", "<", ',', '<' }, { ".", ">", '.', '>' },
        { "/", "?", '/', '?' },
        { "space", "space", ' ', ' ' },
    },
};

static bool s_caps_locked = false;
static bool s_initialised = false;

static void kbd_select_row(uint8_t row)
{
    /* HC138 truth-table: A0 = LSB. Drive each address pin directly. */
    gpio_set_level((gpio_num_t)KBD_OUT_A0, (row >> 0) & 0x1);
    gpio_set_level((gpio_num_t)KBD_OUT_A1, (row >> 1) & 0x1);
    gpio_set_level((gpio_num_t)KBD_OUT_A2, (row >> 2) & 0x1);
    /* Propagation + RC charge of the column pull-ups is well under 1 µs
     * but a few cycles keep us out of the metastable region on cold boot. */
    esp_rom_delay_us(2);
}

static uint8_t kbd_read_columns(void)
{
    uint8_t v = 0;
    for (int i = 0; i < 7; ++i) {
        /* Idle high (pull-up), pressed low → invert into the bitmask. */
        if (gpio_get_level(s_col_pins[i]) == 0) {
            v |= (1u << i);
        }
    }
    return v;
}

esp_err_t cardputer_kbd_init(void)
{
    if (s_initialised) {
        return ESP_OK;
    }

    const gpio_num_t out_pins[3] = {
        (gpio_num_t)KBD_OUT_A0, (gpio_num_t)KBD_OUT_A1, (gpio_num_t)KBD_OUT_A2
    };

    for (int i = 0; i < 3; ++i) {
        gpio_reset_pin(out_pins[i]);
        gpio_set_direction(out_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(out_pins[i], 0);
    }
    for (int i = 0; i < 7; ++i) {
        gpio_reset_pin(s_col_pins[i]);
        gpio_set_direction(s_col_pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(s_col_pins[i], GPIO_PULLUP_ONLY);
    }

    s_initialised = true;
    ESP_LOGI(TAG, "initialised (HC138 on G%d/G%d/G%d, cols G13,15,3,4,5,6,7)",
             KBD_OUT_A0, KBD_OUT_A1, KBD_OUT_A2);
    return ESP_OK;
}

esp_err_t cardputer_kbd_scan(cardputer_kbd_state_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_initialised) {
        esp_err_t err = cardputer_kbd_init();
        if (err != ESP_OK) return err;
    }

    memset(out, 0, sizeof(*out));

    /* Collect raw (x, y) coordinates first. */
    for (uint8_t phys_row = 0; phys_row < 8 && out->pressed_count < CARDPUTER_KBD_MAX_PRESSED; ++phys_row) {
        kbd_select_row(phys_row);
        uint8_t cols = kbd_read_columns();
        if (cols == 0) continue;

        for (int j = 0; j < 7 && out->pressed_count < CARDPUTER_KBD_MAX_PRESSED; ++j) {
            if (!(cols & s_col_chart[j].mask)) continue;
            uint8_t x = (phys_row > 3) ? s_col_chart[j].x_hi : s_col_chart[j].x_lo;
            /* Fold 8 physical rows into 4 logical y-rows, flipped to match
             * the printed layout (top row = y=0). */
            int8_t y = (phys_row > 3) ? (int8_t)(phys_row - 4) : (int8_t)phys_row;
            y = (int8_t)(3 - y);
            if (y < 0 || y > 3 || x > 13) continue;

            out->pressed[out->pressed_count].x = (int8_t)x;
            out->pressed[out->pressed_count].y = y;
            out->pressed_count++;
        }
    }

    /* Classify presses: modifiers vs character keys, then resolve chars. */
    bool char_keys_idx[CARDPUTER_KBD_MAX_PRESSED] = {0};
    for (size_t i = 0; i < out->pressed_count; ++i) {
        const key_def_t *k = &s_key_map[out->pressed[i].y][out->pressed[i].x];
        if      (strcmp(k->first, "tab")   == 0) out->tab   = true;
        else if (strcmp(k->first, "fn")    == 0) out->fn    = true;
        else if (strcmp(k->first, "shift") == 0) out->shift = true;
        else if (strcmp(k->first, "ctrl")  == 0) out->ctrl  = true;
        else if (strcmp(k->first, "opt")   == 0) out->opt   = true;
        else if (strcmp(k->first, "alt")   == 0) out->alt   = true;
        else if (strcmp(k->first, "del")   == 0) out->del   = true;
        else if (strcmp(k->first, "enter") == 0) out->enter = true;
        else if (strcmp(k->first, "space") == 0) out->space = true;
        else { char_keys_idx[i] = true; }
    }

    bool upper = out->shift || s_caps_locked;
    for (size_t i = 0; i < out->pressed_count; ++i) {
        if (!char_keys_idx[i]) continue;
        if (out->chars_len >= CARDPUTER_KBD_MAX_PRESSED) break;
        const key_def_t *k = &s_key_map[out->pressed[i].y][out->pressed[i].x];
        char c = upper ? k->ch_second : k->ch_first;
        if (c != 0) out->chars[out->chars_len++] = c;
    }
    out->chars[out->chars_len] = '\0';

    return ESP_OK;
}

void cardputer_kbd_set_caps_lock(bool locked) { s_caps_locked = locked; }
bool cardputer_kbd_caps_lock(void) { return s_caps_locked; }

bool cardputer_kbd_state_changed(const cardputer_kbd_state_t *a,
                                 const cardputer_kbd_state_t *b)
{
    if (!a || !b) return true;
    if (a->pressed_count != b->pressed_count) return true;
    if (a->chars_len != b->chars_len) return true;
    if (memcmp(a->chars, b->chars, a->chars_len) != 0) return true;
    for (size_t i = 0; i < a->pressed_count; ++i) {
        if (a->pressed[i].x != b->pressed[i].x ||
            a->pressed[i].y != b->pressed[i].y) return true;
    }
    return a->fn != b->fn || a->shift != b->shift || a->ctrl != b->ctrl ||
           a->opt != b->opt || a->alt != b->alt || a->tab != b->tab ||
           a->del != b->del || a->enter != b->enter || a->space != b->space;
}

/*
 * M5Stack Cardputer 56-key matrix driver.
 *
 * Hardware:
 *   3 output GPIOs (G8, G9, G11) drive a 74HC138 1-of-8 demux that pulls
 *   one of 8 row lines low at a time. 7 column GPIOs (G13, G15, G3, G4,
 *   G5, G6, G7) are configured as inputs with internal pull-up; a pressed
 *   key shorts a column to the active (low) row, reading as 0.
 *
 * Coordinate system:
 *   The physical 8x7 matrix is folded into a logical 4x14 grid that
 *   matches the printed key layout (see _KEY_VALUE_MAP below). A scan
 *   returns the list of currently-pressed keys as (x, y) where x in
 *   [0..13] and y in [0..3].
 *
 * Layout (y=0 top row):
 *   y=0:  `  1  2  3  4  5  6  7  8  9  0  -  =  del
 *   y=1:  tab q  w  e  r  t  y  u  i  o  p  [  ]  '\'
 *   y=2:  fn shf a  s  d  f  g  h  j  k  l  ;  '  enter
 *   y=3:  ctl opt alt z  x  c  v  b  n  m  ,  .  /  space
 *
 * Ported from Forairaaaaa's M5Cardputer-UserDemo (Apache-2.0).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CARDPUTER_KBD_MAX_PRESSED  16

typedef struct {
    int8_t x;   /* 0..13 — column in the logical layout, -1 if invalid */
    int8_t y;   /* 0..3  — row in the logical layout, -1 if invalid */
} cardputer_kbd_point_t;

typedef struct {
    /* Pressed special keys */
    bool fn;
    bool shift;
    bool ctrl;
    bool opt;
    bool alt;
    bool tab;
    bool del;
    bool enter;
    bool space;

    /* Pressed character keys (after shift/caps applied). NUL-terminated. */
    char chars[CARDPUTER_KBD_MAX_PRESSED + 1];
    size_t chars_len;

    /* Raw list of all pressed key coordinates this scan */
    cardputer_kbd_point_t pressed[CARDPUTER_KBD_MAX_PRESSED];
    size_t pressed_count;
} cardputer_kbd_state_t;

/* Configure GPIOs. Safe to call once at startup. */
esp_err_t cardputer_kbd_init(void);

/* Perform one full matrix scan and populate *out. Returns ESP_OK always
 * (the driver does no I/O that can fail post-init). */
esp_err_t cardputer_kbd_scan(cardputer_kbd_state_t *out);

/* Toggle the internal caps-lock latch (no LED feedback on Cardputer). */
void cardputer_kbd_set_caps_lock(bool locked);
bool cardputer_kbd_caps_lock(void);

/* Compare two states for inequality — useful to detect transitions. */
bool cardputer_kbd_state_changed(const cardputer_kbd_state_t *a,
                                 const cardputer_kbd_state_t *b);

#ifdef __cplusplus
}
#endif

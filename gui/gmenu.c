/*  Copyright (c) 2015 James Laird-Wah
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at http://mozilla.org/MPL/2.0/. */

#include <iapetus.h>
#include <string.h>
#include <stdio.h>
#include "satiator.h"
#include "gmenu.h"

static font_struct main_font;
static char *version;
int asprintf(char **strp, const char *fmt, ...);

// NBG1 cell-mode
#define NBG1_MAP 1
static volatile uint16_t *char_map = (void*)(VDP2_RAM + (NBG1_MAP * 0x2000));

static int update_accel(int *accel) {
    int v = (*accel)++;
    if (!v)
        return 1;

    v -= 30;

    if (v < 0)
        return 0;

    if (v > 0x20 && (v & 1) == 0)
        return 1;

    if ((v & 0x3) == 0)
        return 1;

    return 0;
}

struct {
    int acc_l, acc_r, acc_u, acc_d;
} pad_state;


static int update_button_bit(int bit, int *accel) {
    int pressed = 0;

    if (per[0].but_push & bit)
        pressed = update_accel(accel);
    else
        *accel = 0;

    if (pressed)
        return bit;
    return 0;
}

static int pad_poll_buttons(void) {
    int out = 0;

    out |= update_button_bit(PAD_UP, &pad_state.acc_u);
    out |= update_button_bit(PAD_DOWN, &pad_state.acc_d);
    out |= update_button_bit(PAD_L, &pad_state.acc_l);
    out |= update_button_bit(PAD_R, &pad_state.acc_r);

    out |= per[0].but_push_once & (PAD_A|PAD_B|PAD_C|PAD_X|PAD_Y|PAD_Z|PAD_START);

    return out;
}

static void unpack_font(int colour) {
    // Unpack 1-bit font to 4-bit cells
    uint16_t *cell = (void*)(VDP2_RAM);
    uint8_t *font = font_8x8;
    for (int i=0; i<128; i++) {
        for (int row=0; row<8; row++) {
            uint32_t row_data = 0;
            uint8_t font_row = *font++;
            for (int col=0; col<8; col++) {
                row_data <<= 4;
                if (font_row & 0x80)
                    row_data |= colour;
                font_row <<= 1;
            }
            *cell++ = row_data >> 16;
            *cell++ = row_data;
        }
    }
}

#define SCROLL_OFF 3

static void init_char_mode(void) {
    // Sets up for drawing characters on NBG1
    unpack_font(0xe);
    *((volatile u16 *)(VDP2_CRAM+2*0xe)) = 0;   // make colour 0xe black
    screen_settings_struct nbg1;
    memset(&nbg1, 0, sizeof(nbg1));
    nbg1.is_bitmap = 0;
    nbg1.bitmap_size = BG_BITMAP512x256;
    nbg1.transparent_bit = 0;
    nbg1.color = BG_16COLOR;
    nbg1.pattern_name_size = 1;
    nbg1.char_size = 0;
    nbg1.plane_size = 0;
    nbg1.map_offset = 0;
    nbg1.map[0] = NBG1_MAP;
    nbg1.map[1] = NBG1_MAP;
    nbg1.map[2] = NBG1_MAP;
    nbg1.map[3] = NBG1_MAP;
    vdp_nbg1_init(&nbg1);
    vdp_set_priority(SCREEN_NBG1, 5);
}

void init_bitmap_mode(void) {
    // Init for bitmap mode in NBG0
    screen_settings_struct settings;
    memset(&settings, 0, sizeof(settings));
    // Setup a screen for us draw on
    settings.is_bitmap = TRUE;
    settings.bitmap_size = BG_BITMAP512x256;
    settings.transparent_bit = 0;
    settings.color = BG_256COLOR;
    settings.special_priority = 0;
    settings.special_color_calc = 0;
    settings.extra_palette_num = 0;
    settings.map_offset = 1;
    vdp_nbg0_init(&settings);
    vdp_set_priority(SCREEN_NBG0, 6);

    // Setup the default 8x16 1BPP font
    main_font.data = font_8x8;
    main_font.width = 8;
    main_font.height = 8;
    main_font.bpp = 1;
    main_font.out = (u8 *)(VDP2_RAM + 0x20000 * settings.map_offset);
    vdp_set_font(SCREEN_NBG0, &main_font, 1);
}

void menu_init(void) {
    vdp_set_default_palette();
    init_bitmap_mode();
    init_char_mode();
    VDP2_REG_CYCA0L = 0x15ee;
    VDP2_REG_CYCA0U = 0x44ee;
    vdp_disp_on();

    char fw_version[32];
    s_get_fw_version(fw_version, sizeof(fw_version));
    char *space = strchr(fw_version, ' ');
    if (space)
        *space = '\0';

    asprintf(&version, "BETA FW%s MNU%s", fw_version, VERSION);
    space = strchr(version, ' ');
    if (space)
        space = strchr(space+1, ' ');
    if (space)
        space = strchr(space+1, ' ');
    if (space)
        *space = '\0';
}


#define FONT_HEIGHT 8
#define FONT_WIDTH 8

static void write_char(int row, int col, char ch) {
    row &= 63;
    col &= 63;

    char_map[row*64+col] = ch;
}

static void write_string(int row, int col, const char *string) {
    row &= 63;
    col &= 63;

    volatile u16 *p = &char_map[row*64 + col];

    while ((*p++ = *string++) && (col++ < 63) );
    while (col++ < 64)
        *p++ = ' ';
}

static void erase(void) {
    for (int i=0; i<64*64; i++)
        char_map[i] = 0;
}

int menu_picklist(file_ent *entries, int n_entries, char *caption, font_struct *font) {
    if (!font)
        font = &main_font;
    vdp_clear_screen(font);

    int margin_top = 24;
    int margin_bot = 16;
    int margin_left = 32;
    int margin_right = 32;

    int width, height;
    vdp_get_scr_width_height(&width, &height);

    int n_rows = (height - margin_top - margin_bot) / FONT_HEIGHT;
    int n_cols = (width - margin_left - margin_right) / FONT_WIDTH;

    int selected = 0;

    // row drawn at top of screen
    int scrollbase = 0;

    VDP2_REG_WPSX0 = margin_left;
    VDP2_REG_WPSY0 = margin_top;
    VDP2_REG_WPEX0 = (width - margin_right - 1) << 1;
    VDP2_REG_WPEY0 = margin_top + n_rows * FONT_HEIGHT - 1;

    VDP2_REG_WCTLA = (1<<9) | (1<<8);

    gui_window_init();
    gui_window_draw(8, 8, width-16, height-16, TRUE, 0, RGB16(26, 26, 25) | 0x8000);
    vdp_print_text(font, 8+6, 8+4, 0xf, caption);
    vdp_print_text(font, 8+8, height-8, 0xf, version);

    int x_text = 4;

    erase();
    for (int row = 0; row < n_rows; row++) {
        if (row < n_entries)
            write_string(row, x_text, entries[row].name);
    }


again:
    while (1) {
        VDP2_REG_SCYIN1 = scrollbase*8 - margin_top;

        write_char(selected, 2, '\x10');

        vdp_vsync();
        int buttons = pad_poll_buttons();
        if (buttons & (PAD_UP | PAD_DOWN | PAD_L | PAD_R))
            write_char(selected, 2, ' ');

        if (buttons & PAD_UP) {
            selected--;
            goto move;
        }
        if (buttons & PAD_DOWN) {
            selected++;
            goto move;
        }
        if (buttons & PAD_L) {
            selected -= n_rows;
            goto move;
        }
        if (buttons & PAD_R) {
            selected += n_rows;
            goto move;
        }

        if (buttons & PAD_A) {
            erase();
            return selected;
        }

        if (buttons & PAD_C) {
            erase();
            return -1;
        }
    }

move:

    if (selected >= n_entries)
        selected = n_entries - 1;
    if (selected < 0)
        selected = 0;
    while (selected - scrollbase < SCROLL_OFF && scrollbase > 0) {
        scrollbase--;
        write_string(scrollbase, x_text, entries[scrollbase].name);
    };

    while (scrollbase + n_rows - selected < SCROLL_OFF && scrollbase < n_entries - n_rows) {
        write_string(scrollbase+n_rows, x_text, entries[scrollbase+n_rows].name);
        scrollbase++;
    }

    goto again;
}

void menu_error(const char *title, const char *message) {
    int width, height;
    vdp_get_scr_width_height(&width, &height);
    vdp_clear_screen(&main_font);
    gui_window_init();
    gui_window_draw(8*3, 8*5, width-8*5, height-8*7, TRUE, 0, RGB16(26, 26, 25) | 0x8000);
    vdp_print_text(&main_font, 8*3+6, 8*5+4, 0xf, title);
    vdp_print_text(&main_font, 8*3+14, 8*5+20, 0x10, message);

    for (;;) {
        vdp_vsync();
        if (per[0].but_push_once)
            break;
    }
}

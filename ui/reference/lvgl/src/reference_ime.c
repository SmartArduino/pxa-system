#include "pxsys/reference_ime.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
#define PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT 3
#define PXSYS_REFERENCE_IME_ENGLISH_MAP_SIZE 24
#define PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_MAP_FIRST 19
#define PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_BUTTON_FIRST 16
#endif

typedef enum {
    PXSYS_REFERENCE_IME_SYMBOLS_CHINESE,
    PXSYS_REFERENCE_IME_SYMBOLS_ENGLISH,
} pxsys_reference_ime_symbols_t;

struct pxsys_reference_ime {
    lv_obj_t *root;
    lv_obj_t *text_area;
    lv_obj_t *keyboard;
    lv_obj_t *symbol_panel;
    lv_obj_t *symbol_toolbar;
    lv_obj_t *symbol_grid;
#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
    lv_obj_t *pinyin_ime;
    lv_obj_t *candidates;
    const char *chinese_keyboard_map[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 21];
    const char *english_keyboard_map[PXSYS_REFERENCE_IME_ENGLISH_MAP_SIZE];
    const char *english_candidate_letters;
    char english_candidates[PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT][2];
    uint8_t english_candidate_start;
    uint8_t english_candidate_count;
    bool english_uppercase;
#endif
    pxsys_reference_ime_mode_t mode;
    void (*close_callback)(void* context);
    void* close_context;
    lv_coord_t width;
    lv_coord_t height;
    lv_align_t align;
    lv_coord_t offset_x;
    lv_coord_t offset_y;
    pxsys_reference_ime_mode_t last_keyboard_mode;
    pxsys_reference_ime_symbols_t symbol_set;
    const lv_font_t *text_font;
    const lv_font_t *icon_font;
    lv_font_t text_font_with_icons;
    lv_font_t icon_font_with_text_fallback;
};

#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
static const char *const s_english_keyboard_map[] = {
    "#+", "123", "abc", "def", LV_SYMBOL_BACKSPACE, "\n",
    "CN", "ghi", "jkl", "mno", "aA", "\n",
    "?", "pqrs", "tuv", "wxyz", LV_SYMBOL_DOWN, "\n",
    LV_SYMBOL_LEFT, " ", " ", " ", LV_SYMBOL_RIGHT, "",
};
static const char *const s_english_uppercase_keyboard_map[] = {
    "#+", "123", "ABC", "DEF", LV_SYMBOL_BACKSPACE, "\n",
    "CN", "GHI", "JKL", "MNO", "Aa", "\n",
    "?", "PQRS", "TUV", "WXYZ", LV_SYMBOL_DOWN, "\n",
    LV_SYMBOL_LEFT, " ", " ", " ", LV_SYMBOL_RIGHT, "",
};
static const lv_buttonmatrix_ctrl_t s_english_keyboard_ctrl[20] = {
    [0 ... 19] = LV_BUTTONMATRIX_CTRL_NO_REPEAT |
                  LV_BUTTONMATRIX_CTRL_CLICK_TRIG | 1,
};

static const char *const s_number_keyboard_map[] = {
    "1", "2", "3", "#+", "\n",
    "4", "5", "6", "EN", "\n",
    "7", "8", "9", "CN", "\n",
    "0", ".", "-", "_", LV_SYMBOL_BACKSPACE, LV_SYMBOL_DOWN, "",
};
static const lv_buttonmatrix_ctrl_t s_number_keyboard_ctrl[18] = {
    [0 ... 17] = LV_BUTTONMATRIX_CTRL_NO_REPEAT |
                  LV_BUTTONMATRIX_CTRL_CLICK_TRIG | 1,
};
#endif

static const char *const s_chinese_symbols[] = {
    "，", "。", "！", "？", "、", "；", "：", "（", "）", "“", "”", "‘", "’",
    "【", "】", "《", "》", "…", "—", "～", "·", "￥", "％", "＋", "－", "＝",
    "／", "＠", "＃", "＆", "＊", "｜", "＼", "＿", "＾", "｀", "＜", "＞",
    "｛", "｝", "［", "］", "〈", "〉",
};

static const char *const s_english_symbols[] = {
    ".", ",", "!", "?", "@", "#", "_", "-", "/", "\\", "(", ")", "[", "]", "{", "}",
    "'", "\"", ":", ";", "+", "=", "*", "&", "$", "%", "^", "|", "~", "`", "<", ">",
    "±", "×", "÷", "°", "‰", "€", "£", "¥", "¢", "©", "®", "™", "§",
};


static void style_keypad(lv_obj_t *keypad) {
    lv_obj_set_style_bg_opa(keypad, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keypad, 1, 0);
    lv_obj_set_style_radius(keypad, 10, 0);
    lv_obj_set_style_pad_all(keypad, 5, 0);
}

static void style_keypad_items(lv_obj_t *keypad) {
    lv_obj_set_style_border_width(keypad, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(keypad, 5, LV_PART_ITEMS);
}

/* The Guest owns the text input: it can delete it (replacing the surface, for
 * example) while the keypad is still open. A delete notification clears the
 * pointer, so it is only ever non-NULL while the object is alive and no code
 * has to touch freed memory. */
static void ime_target_deleted(lv_event_t *event) {
    pxsys_reference_ime_t *input_method =
        (pxsys_reference_ime_t *)lv_event_get_user_data(event);
    if (input_method == NULL) return;
    input_method->text_area = NULL;
    if (input_method->close_callback != NULL)
        input_method->close_callback(input_method->close_context);
}

static lv_obj_t *ime_text_area(const pxsys_reference_ime_t *input_method) {
    return input_method != NULL ? input_method->text_area : NULL;
}

static void input_method_hide_pinyin_candidates(pxsys_reference_ime_t *input_method) {
#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
    if (input_method->candidates != NULL)
        lv_obj_add_flag(input_method->candidates, LV_OBJ_FLAG_HIDDEN);
#else
    (void)input_method;
#endif
}

static void apply_icon_font_recursively(pxsys_reference_ime_t *input_method,
                                        lv_obj_t *object) {
    if (object == NULL) return;
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_USER_1) &&
        input_method->icon_font != NULL) {
        lv_obj_set_style_text_font(object, input_method->icon_font, 0);
    }
    const uint32_t count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < count; ++index) {
        apply_icon_font_recursively(input_method, lv_obj_get_child(object, index));
    }
}

static void refresh_fonts(pxsys_reference_ime_t *input_method) {
    if (input_method == NULL || input_method->text_font == NULL) return;

    const lv_font_t *font = input_method->text_font;
    if (input_method->icon_font != NULL) {
        input_method->icon_font_with_text_fallback = *input_method->icon_font;
        input_method->icon_font_with_text_fallback.fallback = LV_FONT_DEFAULT;
        input_method->text_font_with_icons = *input_method->text_font;
        input_method->text_font_with_icons.fallback =
            &input_method->icon_font_with_text_fallback;
        font = &input_method->text_font_with_icons;
    }

    lv_obj_set_style_text_font(input_method->root, font, 0);
    if (input_method->keyboard != NULL)
        lv_obj_set_style_text_font(input_method->keyboard, font, LV_PART_ITEMS);
#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
    if (input_method->candidates != NULL)
        lv_obj_set_style_text_font(input_method->candidates, font,
                                   LV_PART_ITEMS);
#endif
    if (ime_text_area(input_method) != NULL)
        lv_obj_set_style_text_font(ime_text_area(input_method), font, 0);
    apply_icon_font_recursively(input_method, input_method->symbol_panel);
}

#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
static const char *english_candidate_chars(const pxsys_reference_ime_t *input_method,
                                           const char *key) {
    static const char *const lowercase_keys[] = {
        "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz",
    };
    static const char *const uppercase_keys[] = {
        "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ",
    };
    if (key == NULL) return NULL;
    const char *const *keys = input_method != NULL && input_method->english_uppercase
                                  ? uppercase_keys
                                  : lowercase_keys;
    for (size_t index = 0;
         index < sizeof(lowercase_keys) / sizeof(lowercase_keys[0]); ++index) {
        if (strcmp(key, keys[index]) == 0) return keys[index];
    }
    return NULL;
}

static void set_english_candidates(pxsys_reference_ime_t *input_method,
                                   const char *letters, uint8_t start) {
    if (input_method == NULL) return;

    input_method->english_candidate_letters = letters;
    input_method->english_candidate_start = 0;
    input_method->english_candidate_count = 0;
    for (size_t index = 0; index < PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT;
         ++index) {
        input_method->english_candidates[index][0] = ' ';
        input_method->english_candidates[index][1] = '\0';
    }

    if (letters != NULL) {
        const size_t length = strlen(letters);
        if (start < length) {
            input_method->english_candidate_start = start;
            input_method->english_candidate_count = (uint8_t)(length - start);
            if (input_method->english_candidate_count >
                PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT) {
                input_method->english_candidate_count =
                    PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT;
            }
            for (uint8_t index = 0; index < input_method->english_candidate_count;
                 ++index) {
                input_method->english_candidates[index][0] =
                    letters[start + index];
            }
        }
    }

    if (input_method->mode == PXSYS_REFERENCE_IME_MODE_ENGLISH &&
        input_method->keyboard != NULL) {
        lv_buttonmatrix_set_map(input_method->keyboard,
                                input_method->english_keyboard_map);
    }
}

static void prepare_english_keyboard(pxsys_reference_ime_t *input_method) {
    const char *const *base_map = input_method->english_uppercase
                                      ? s_english_uppercase_keyboard_map
                                      : s_english_keyboard_map;
    memcpy(input_method->english_keyboard_map, base_map,
           sizeof(input_method->english_keyboard_map));
    input_method->english_candidate_letters = NULL;
    input_method->english_candidate_start = 0;
    input_method->english_candidate_count = 0;
    for (size_t index = 0; index < PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT;
         ++index) {
        input_method->english_candidates[index][0] = ' ';
        input_method->english_candidates[index][1] = '\0';
        input_method->english_keyboard_map[
            PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_MAP_FIRST + index] =
            input_method->english_candidates[index];
    }
}

static void apply_english_keyboard(pxsys_reference_ime_t *input_method) {
    prepare_english_keyboard(input_method);
    lv_keyboard_set_map(input_method->keyboard, LV_KEYBOARD_MODE_USER_2,
                        input_method->english_keyboard_map,
                        s_english_keyboard_ctrl);
    lv_keyboard_set_mode(input_method->keyboard, LV_KEYBOARD_MODE_USER_2);
}

static void apply_chinese_keyboard_map(pxsys_reference_ime_t *input_method) {
    if (input_method == NULL || input_method->keyboard == NULL) return;

    const char *const *native_map =
        lv_buttonmatrix_get_map(input_method->keyboard);
    if (native_map == NULL) return;
    for (size_t index = 0;
         index < sizeof(input_method->chinese_keyboard_map) /
                     sizeof(input_method->chinese_keyboard_map[0]);
         ++index) {
        input_method->chinese_keyboard_map[index] = native_map[index];
    }

    /* Preserve LVGL's K9 candidates and replace the three layout controls. */
    input_method->chinese_keyboard_map[0] = "#+";
    input_method->chinese_keyboard_map[10] = "EN";
    input_method->chinese_keyboard_map[16] = LV_SYMBOL_DOWN;
    lv_buttonmatrix_set_map(input_method->keyboard,
                            input_method->chinese_keyboard_map);

    /* Switch layout only after the key is released. */
    lv_buttonmatrix_set_button_ctrl(input_method->keyboard, 0,
                                    LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_buttonmatrix_set_button_ctrl(input_method->keyboard, 9,
                                    LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_buttonmatrix_set_button_ctrl(input_method->keyboard, 14,
                                    LV_BUTTONMATRIX_CTRL_CLICK_TRIG);

}
#endif

static void symbol_clicked(lv_event_t *event) {
    pxsys_reference_ime_t *input_method = lv_event_get_user_data(event);
    if (input_method == NULL || ime_text_area(input_method) == NULL) return;

    lv_obj_t *button = lv_event_get_current_target(event);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label == NULL) return;
    lv_textarea_add_text(ime_text_area(input_method),
                         lv_label_get_text(label));
}

static void symbol_toolbar_clicked(lv_event_t *event) {
    pxsys_reference_ime_t *input_method = lv_event_get_user_data(event);
    if (input_method == NULL) return;

    lv_obj_t *button = lv_event_get_current_target(event);
    switch (lv_obj_get_index(button)) {
    case 0:
        input_method->symbol_set = PXSYS_REFERENCE_IME_SYMBOLS_CHINESE;
        break;
    case 1:
        input_method->symbol_set = PXSYS_REFERENCE_IME_SYMBOLS_ENGLISH;
        break;
    case 2:
        input_method->symbol_set = PXSYS_REFERENCE_IME_SYMBOLS_ENGLISH;
        break;
    case 3:
        pxsys_reference_ime_set_mode(input_method,
                                  input_method->last_keyboard_mode);
        return;
    default:
        pxsys_reference_ime_hide(input_method);
        return;
    }

    lv_obj_clean(input_method->symbol_grid);
    const char *const *symbols = s_chinese_symbols;
    size_t symbol_count = sizeof(s_chinese_symbols) / sizeof(s_chinese_symbols[0]);
    if (input_method->symbol_set == PXSYS_REFERENCE_IME_SYMBOLS_ENGLISH) {
        symbols = s_english_symbols;
        symbol_count = sizeof(s_english_symbols) / sizeof(s_english_symbols[0]);
    }
    for (size_t index = 0; index < symbol_count; ++index) {
        lv_obj_t *symbol_button = lv_button_create(input_method->symbol_grid);
        lv_obj_set_size(symbol_button, 35, 24);
        lv_obj_set_style_border_width(symbol_button, 0, 0);
        lv_obj_set_style_radius(symbol_button, 4, 0);
        lv_obj_set_style_pad_all(symbol_button, 0, 0);
        lv_obj_add_event_cb(symbol_button, symbol_clicked, LV_EVENT_CLICKED,
                            input_method);
        lv_obj_t *symbol_label = lv_label_create(symbol_button);
        lv_label_set_text(symbol_label, symbols[index]);
        lv_obj_center(symbol_label);
    }

    for (uint32_t index = 0;
         index < lv_obj_get_child_count(input_method->symbol_toolbar); ++index) {
    }
    lv_obj_scroll_to_y(input_method->symbol_grid, 0, LV_ANIM_OFF);
}

static lv_obj_t *add_symbol_button(lv_obj_t *parent, const char *text,
                                   lv_event_cb_t callback,
                                   pxsys_reference_ime_t *input_method,
                                   bool icon) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 35, 24);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, input_method);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    if (icon) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_USER_1);
        if (input_method->icon_font != NULL)
            lv_obj_set_style_text_font(label, input_method->icon_font, 0);
    }
    lv_obj_center(label);
    return button;
}

static void create_symbol_panel(pxsys_reference_ime_t *input_method) {
    input_method->symbol_panel = lv_obj_create(input_method->root);
    style_keypad(input_method->symbol_panel);
    lv_obj_clear_flag(input_method->symbol_panel, LV_OBJ_FLAG_SCROLLABLE);

    input_method->symbol_toolbar = lv_obj_create(input_method->symbol_panel);
    lv_obj_set_size(input_method->symbol_toolbar, LV_PCT(100), 26);
    lv_obj_align(input_method->symbol_toolbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(input_method->symbol_toolbar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_method->symbol_toolbar, 0, 0);
    lv_obj_set_style_pad_all(input_method->symbol_toolbar, 0, 0);
    lv_obj_set_style_pad_column(input_method->symbol_toolbar, 4, 0);
    lv_obj_set_flex_flow(input_method->symbol_toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(input_method->symbol_toolbar, LV_OBJ_FLAG_SCROLLABLE);

    add_symbol_button(input_method->symbol_toolbar, "中", symbol_toolbar_clicked,
                      input_method, false);
    add_symbol_button(input_method->symbol_toolbar, "EN", symbol_toolbar_clicked,
                      input_method, false);
    add_symbol_button(input_method->symbol_toolbar, "ABC", symbol_toolbar_clicked,
                      input_method, false);
    add_symbol_button(input_method->symbol_toolbar, LV_SYMBOL_DOWN,
                      symbol_toolbar_clicked, input_method, true);
    for (uint32_t index = 0;
         index < lv_obj_get_child_count(input_method->symbol_toolbar); ++index) {
        lv_obj_set_flex_grow(lv_obj_get_child(input_method->symbol_toolbar, index),
                             1);
    }

    input_method->symbol_grid = lv_obj_create(input_method->symbol_panel);
    lv_obj_set_size(input_method->symbol_grid, LV_PCT(100), LV_PCT(62));
    lv_obj_set_pos(input_method->symbol_grid, 0, 30);
    lv_obj_set_style_bg_opa(input_method->symbol_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_method->symbol_grid, 0, 0);
    lv_obj_set_style_pad_all(input_method->symbol_grid, 0, 0);
    lv_obj_set_style_pad_row(input_method->symbol_grid, 4, 0);
    lv_obj_set_style_pad_column(input_method->symbol_grid, 4, 0);
    lv_obj_set_flex_flow(input_method->symbol_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(input_method->symbol_grid, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(input_method->symbol_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(input_method->symbol_grid,
                              LV_SCROLLBAR_MODE_AUTO);
    input_method->symbol_set = PXSYS_REFERENCE_IME_SYMBOLS_CHINESE;
    lv_obj_send_event(lv_obj_get_child(input_method->symbol_toolbar, 0),
                      LV_EVENT_CLICKED, NULL);
}

/* The pad has no confirm key of its own: closing it submits the text, so a
 * Guest that waits for the submitted event applies what the user typed. */
static void submit_and_hide(pxsys_reference_ime_t *input_method) {
    if (ime_text_area(input_method) != NULL)
        lv_obj_send_event(ime_text_area(input_method), LV_EVENT_READY, NULL);
    pxsys_reference_ime_hide(input_method);
}

static void keyboard_preprocess(lv_event_t *event) {
    pxsys_reference_ime_t *input_method = lv_event_get_user_data(event);
    if (input_method == NULL) return;
    lv_obj_t *keyboard = lv_event_get_current_target(event);
    const uint32_t button = lv_buttonmatrix_get_selected_button(keyboard);
    const char *text = lv_buttonmatrix_get_button_text(keyboard, button);
    if (text == NULL) return;

#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
    if (input_method->mode == PXSYS_REFERENCE_IME_MODE_CHINESE) {
        if (strcmp(text, "#+") == 0) {
            pxsys_reference_ime_set_mode(input_method, PXSYS_REFERENCE_IME_MODE_SYMBOLS);
            lv_event_stop_processing(event);
        } else if (strcmp(text, "123") == 0) {
            pxsys_reference_ime_set_mode(input_method, PXSYS_REFERENCE_IME_MODE_NUMBER);
            lv_event_stop_processing(event);
        } else if (strcmp(text, "EN") == 0) {
            pxsys_reference_ime_set_mode(input_method, PXSYS_REFERENCE_IME_MODE_ENGLISH);
            lv_event_stop_processing(event);
        } else if (strcmp(text, LV_SYMBOL_DOWN) == 0) {
            submit_and_hide(input_method);
            lv_event_stop_processing(event);
        }
        return;
    }
#endif

    if (strcmp(text, "123") == 0) {
        pxsys_reference_ime_set_mode(input_method, PXSYS_REFERENCE_IME_MODE_NUMBER);
        lv_event_stop_processing(event);
    } else if (strcmp(text, "CN") == 0) {
        pxsys_reference_ime_set_mode(input_method, PXSYS_REFERENCE_IME_MODE_CHINESE);
        lv_event_stop_processing(event);
    } else if (strcmp(text, "EN") == 0) {
        pxsys_reference_ime_set_mode(input_method, PXSYS_REFERENCE_IME_MODE_ENGLISH);
        lv_event_stop_processing(event);
    } else if (strcmp(text, "#+") == 0) {
        pxsys_reference_ime_set_mode(input_method, PXSYS_REFERENCE_IME_MODE_SYMBOLS);
        lv_event_stop_processing(event);
    } else if (strcmp(text, LV_SYMBOL_DOWN) == 0) {
        submit_and_hide(input_method);
        lv_event_stop_processing(event);
    } else if (strcmp(text, LV_SYMBOL_BACKSPACE) == 0 &&
               ime_text_area(input_method) != NULL) {
#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
        set_english_candidates(input_method, NULL, 0);
#endif
        lv_textarea_delete_char(ime_text_area(input_method));
        lv_event_stop_processing(event);
#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
    } else if (input_method->mode == PXSYS_REFERENCE_IME_MODE_ENGLISH) {
        if (strcmp(text, "aA") == 0 || strcmp(text, "Aa") == 0) {
            input_method->english_uppercase = !input_method->english_uppercase;
            apply_english_keyboard(input_method);
            lv_event_stop_processing(event);
        } else if (button >= PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_BUTTON_FIRST &&
            button < PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_BUTTON_FIRST +
                         PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT) {
            const uint8_t index =
                (uint8_t)(button - PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_BUTTON_FIRST);
            if (index < input_method->english_candidate_count &&
                ime_text_area(input_method) != NULL) {
                lv_textarea_add_char(ime_text_area(input_method),
                                     input_method->english_candidates[index][0]);
                set_english_candidates(input_method, NULL, 0);
            }
            lv_event_stop_processing(event);
        } else if (strcmp(text, LV_SYMBOL_LEFT) == 0) {
            if (input_method->english_candidate_letters != NULL &&
                input_method->english_candidate_start > 0) {
                const uint8_t start = input_method->english_candidate_start >=
                                              PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT
                                          ? input_method->english_candidate_start -
                                                PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT
                                          : 0;
                set_english_candidates(input_method,
                                       input_method->english_candidate_letters,
                                       start);
            } else if (ime_text_area(input_method) != NULL) {
                lv_textarea_cursor_left(ime_text_area(input_method));
                set_english_candidates(input_method, NULL, 0);
            }
            lv_event_stop_processing(event);
        } else if (strcmp(text, LV_SYMBOL_RIGHT) == 0) {
            const size_t length = input_method->english_candidate_letters == NULL
                                      ? 0
                                      : strlen(input_method->english_candidate_letters);
            if (input_method->english_candidate_letters != NULL &&
                (size_t)(input_method->english_candidate_start +
                         PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT) <
                    length) {
                set_english_candidates(
                    input_method, input_method->english_candidate_letters,
                    input_method->english_candidate_start +
                        PXSYS_REFERENCE_IME_ENGLISH_CANDIDATE_COUNT);
            } else if (ime_text_area(input_method) != NULL) {
                lv_textarea_cursor_right(ime_text_area(input_method));
                set_english_candidates(input_method, NULL, 0);
            }
            lv_event_stop_processing(event);
        } else {
            const char *letters = english_candidate_chars(input_method, text);
            if (letters != NULL) {
                set_english_candidates(input_method, letters, 0);
                lv_event_stop_processing(event);
            } else {
                set_english_candidates(input_method, NULL, 0);
            }
        }
#endif
    }
}

#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
static void keyboard_mode_event(lv_event_t *event) {
    pxsys_reference_ime_t *input_method = lv_event_get_user_data(event);
    if (input_method != NULL &&
        input_method->mode == PXSYS_REFERENCE_IME_MODE_CHINESE) {
        /* Pinyin replaces its map after backspace and candidate completion. */
        apply_chinese_keyboard_map(input_method);
    }
}

static void keyboard_candidate_visibility_event(lv_event_t *event) {
    pxsys_reference_ime_t *input_method = lv_event_get_user_data(event);
    if (input_method == NULL ||
        input_method->mode != PXSYS_REFERENCE_IME_MODE_CHINESE ||
        input_method->candidates == NULL) {
        return;
    }

    bool has_candidate = false;
    for (uint32_t index = 1; index <= LV_IME_PINYIN_CAND_TEXT_NUM; ++index) {
        const char *text =
            lv_buttonmatrix_get_button_text(input_method->candidates, index);
        if (text != NULL && text[0] != '\0' && strcmp(text, " ") != 0) {
            has_candidate = true;
            break;
        }
    }
    if (!has_candidate) input_method_hide_pinyin_candidates(input_method);
}

static void pinyin_candidate_selected_event(lv_event_t *event) {
    pxsys_reference_ime_t *input_method = lv_event_get_user_data(event);
    if (input_method == NULL ||
        input_method->mode != PXSYS_REFERENCE_IME_MODE_CHINESE) {
        return;
    }

    lv_obj_t *candidates = lv_event_get_current_target(event);
    const uint32_t button = lv_buttonmatrix_get_selected_button(candidates);
    if (button == LV_BUTTONMATRIX_BUTTON_NONE || button == 0 ||
        button == LV_IME_PINYIN_CAND_TEXT_NUM + 1) {
        return;
    }

    /* Selecting from the external candidate panel resets LVGL's K9 map. */
    apply_chinese_keyboard_map(input_method);
    input_method_hide_pinyin_candidates(input_method);
}
#endif

static void text_area_clicked(lv_event_t *event) {
    pxsys_reference_ime_show(lv_event_get_user_data(event));
}

pxsys_reference_ime_t *pxsys_reference_ime_create(lv_obj_t *parent,
                                             lv_obj_t *text_area) {
    if (parent == NULL) return NULL;

    pxsys_reference_ime_t *input_method = calloc(1, sizeof(*input_method));
    if (input_method == NULL) return NULL;
    /* The caller installs the icon font with set_icon_font(). */
    input_method->last_keyboard_mode = PXSYS_REFERENCE_IME_MODE_CHINESE;

    input_method->root = lv_obj_create(parent);
    lv_obj_set_size(input_method->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(input_method->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_method->root, 0, 0);
    lv_obj_set_style_pad_all(input_method->root, 0, 0);
    lv_obj_remove_flag(input_method->root,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    input_method->keyboard = lv_keyboard_create(input_method->root);
    style_keypad(input_method->keyboard);
    style_keypad_items(input_method->keyboard);

#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
    input_method->pinyin_ime = lv_ime_pinyin_create(input_method->root);
    lv_ime_pinyin_set_keyboard(input_method->pinyin_ime,
                               input_method->keyboard);
    input_method->candidates =
        lv_ime_pinyin_get_cand_panel(input_method->pinyin_ime);
    style_keypad(input_method->candidates);
    lv_obj_set_style_radius(input_method->candidates, 8, 0);
    lv_obj_set_style_pad_all(input_method->candidates, 4, 0);
    style_keypad_items(input_method->candidates);
    lv_obj_set_style_radius(input_method->candidates, 4, LV_PART_ITEMS);
    lv_obj_add_event_cb(input_method->keyboard, keyboard_preprocess,
                        LV_EVENT_VALUE_CHANGED | LV_EVENT_PREPROCESS,
                        input_method);
    lv_obj_add_event_cb(input_method->keyboard, keyboard_mode_event,
                        LV_EVENT_VALUE_CHANGED, input_method);
    lv_obj_add_event_cb(input_method->keyboard,
                        keyboard_candidate_visibility_event,
                        LV_EVENT_VALUE_CHANGED, input_method);
    lv_obj_add_event_cb(input_method->candidates,
                        pinyin_candidate_selected_event,
                        LV_EVENT_VALUE_CHANGED, input_method);
#else
    lv_keyboard_set_mode(input_method->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(input_method->keyboard, keyboard_preprocess,
                        LV_EVENT_VALUE_CHANGED | LV_EVENT_PREPROCESS,
                        input_method);
#endif

    create_symbol_panel(input_method);
    pxsys_reference_ime_set_text_area(input_method, text_area);
    pxsys_reference_ime_set_layout(input_method, LV_PCT(94), LV_PCT(43),
                                LV_ALIGN_BOTTOM_MID, 0, -8);
    pxsys_reference_ime_set_mode(input_method, PXSYS_REFERENCE_IME_MODE_CHINESE);
    return input_method;
}

void pxsys_reference_ime_destroy(pxsys_reference_ime_t *input_method) {
    if (input_method == NULL) return;
    if (ime_text_area(input_method) != NULL) {
        lv_obj_remove_event_cb_with_user_data(ime_text_area(input_method),
                                              text_area_clicked, input_method);
        lv_obj_remove_event_cb_with_user_data(ime_text_area(input_method),
                                              ime_target_deleted, input_method);
        if (input_method->text_font != NULL)
            lv_obj_set_style_text_font(ime_text_area(input_method),
                                       input_method->text_font, 0);
        input_method->text_area = NULL;
    }
    if (input_method->root != NULL) lv_obj_delete(input_method->root);
    free(input_method);
}

void pxsys_reference_ime_set_theme(pxsys_reference_ime_t *input_method,
                                    const pxsys_theme_snapshot_t *theme) {
    lv_obj_t *surfaces[3];
    size_t index;
    if (input_method == NULL || theme == NULL) return;
    surfaces[0] = input_method->keyboard;
#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
    surfaces[1] = input_method->candidates;
#else
    surfaces[1] = NULL;
#endif
    surfaces[2] = input_method->symbol_panel;
    for (index = 0; index < 3; ++index) {
        lv_obj_t *surface = surfaces[index];
        if (surface == NULL) continue;
        lv_obj_set_style_bg_color(surface,
            lv_color_hex(theme->colors[PXSYS_COLOR_SURFACE_CONTAINER]), 0);
        lv_obj_set_style_border_color(surface,
            lv_color_hex(theme->colors[PXSYS_COLOR_OUTLINE_VARIANT]), 0);
        lv_obj_set_style_text_color(surface,
            lv_color_hex(theme->colors[PXSYS_COLOR_ON_SURFACE]), 0);
        lv_obj_set_style_bg_color(surface,
            lv_color_hex(theme->colors[PXSYS_COLOR_SURFACE_CONTAINER_HIGHEST]),
            LV_PART_ITEMS);
        lv_obj_set_style_text_color(surface,
            lv_color_hex(theme->colors[PXSYS_COLOR_ON_SURFACE]), LV_PART_ITEMS);
        lv_obj_set_style_bg_color(surface,
            lv_color_hex(theme->colors[PXSYS_COLOR_PRIMARY_CONTAINER]),
            LV_PART_ITEMS | LV_STATE_PRESSED);
    }
    if (input_method->symbol_grid != NULL) {
        for (index = 0; index < lv_obj_get_child_count(input_method->symbol_grid);
             ++index) {
            lv_obj_t *button = lv_obj_get_child(input_method->symbol_grid, index);
            lv_obj_set_style_bg_color(button,
                lv_color_hex(theme->colors[PXSYS_COLOR_SURFACE_CONTAINER_HIGHEST]), 0);
            lv_obj_set_style_text_color(button,
                lv_color_hex(theme->colors[PXSYS_COLOR_ON_SURFACE]), 0);
        }
    }
}

void pxsys_reference_ime_set_text_area(pxsys_reference_ime_t *input_method,
                                    lv_obj_t *text_area) {
    /* A deleted input cleared the pointer already, so anything still here is
     * alive and its callback can be removed safely. */
    if (input_method == NULL) return;
    if (ime_text_area(input_method) != NULL) {
        lv_obj_remove_event_cb_with_user_data(ime_text_area(input_method),
                                              text_area_clicked, input_method);
        lv_obj_remove_event_cb_with_user_data(ime_text_area(input_method),
                                              ime_target_deleted, input_method);
        if (input_method->text_font != NULL)
            lv_obj_set_style_text_font(ime_text_area(input_method),
                                       input_method->text_font, 0);
    }
    input_method->text_area = text_area;
    if (text_area != NULL)
        lv_obj_add_event_cb(text_area, ime_target_deleted, LV_EVENT_DELETE,
                            input_method);
    if (input_method->keyboard != NULL)
        lv_keyboard_set_textarea(input_method->keyboard, text_area);
    if (text_area != NULL)
        lv_obj_add_event_cb(text_area, text_area_clicked, LV_EVENT_CLICKED,
                            input_method);
    refresh_fonts(input_method);
}

void pxsys_reference_ime_set_font(pxsys_reference_ime_t *input_method,
                               const lv_font_t *font) {
    if (input_method == NULL) return;
    input_method->text_font = font;
    refresh_fonts(input_method);
}

void pxsys_reference_ime_set_icon_font(pxsys_reference_ime_t *input_method,
                                    const lv_font_t *font) {
    if (input_method == NULL) return;
    input_method->icon_font = font;
    refresh_fonts(input_method);
}

void pxsys_reference_ime_set_layout(pxsys_reference_ime_t *input_method,
                                 lv_coord_t width, lv_coord_t height,
                                 lv_align_t align, lv_coord_t offset_x,
                                 lv_coord_t offset_y) {
    if (input_method == NULL) return;
    input_method->width = width;
    input_method->height = height;
    input_method->align = align;
    input_method->offset_x = offset_x;
    input_method->offset_y = offset_y;

    lv_obj_set_size(input_method->keyboard, width, height);
    lv_obj_align(input_method->keyboard, align, offset_x, offset_y);
    lv_obj_set_size(input_method->symbol_panel, width, height);
    lv_obj_align(input_method->symbol_panel, align, offset_x, offset_y);
#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
    lv_obj_set_size(input_method->candidates, width, 36);
    lv_obj_align_to(input_method->candidates, input_method->keyboard,
                    LV_ALIGN_OUT_TOP_MID, 0, -6);
#endif
}

lv_coord_t pxsys_reference_ime_keyboard_top(
    const pxsys_reference_ime_t *input_method) {
    lv_area_t area;
    if (input_method == NULL || input_method->keyboard == NULL) return -1;
    lv_obj_update_layout(input_method->root);
    lv_obj_get_coords(input_method->keyboard, &area);
    return area.y1;
}

void pxsys_reference_ime_set_mode(pxsys_reference_ime_t *input_method,
                               pxsys_reference_ime_mode_t mode) {
    if (input_method == NULL || input_method->keyboard == NULL) return;
    input_method->mode = mode;

    if (mode == PXSYS_REFERENCE_IME_MODE_SYMBOLS) {
        const uint32_t symbol_tab =
            input_method->last_keyboard_mode == PXSYS_REFERENCE_IME_MODE_CHINESE
                ? 0
                : 1;
        lv_obj_send_event(lv_obj_get_child(input_method->symbol_toolbar, symbol_tab),
                          LV_EVENT_CLICKED, NULL);
        input_method_hide_pinyin_candidates(input_method);
        lv_obj_add_flag(input_method->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(input_method->symbol_panel, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    input_method->last_keyboard_mode = mode;
    lv_obj_add_flag(input_method->symbol_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(input_method->keyboard, LV_OBJ_FLAG_HIDDEN);

#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
    if (mode == PXSYS_REFERENCE_IME_MODE_CHINESE) {
        lv_ime_pinyin_set_mode(input_method->pinyin_ime, LV_IME_PINYIN_MODE_K9);
        input_method_hide_pinyin_candidates(input_method);
        apply_chinese_keyboard_map(input_method);
        return;
    }

    lv_ime_pinyin_set_mode(input_method->pinyin_ime, LV_IME_PINYIN_MODE_K9_NUMBER);
    input_method_hide_pinyin_candidates(input_method);
    if (mode == PXSYS_REFERENCE_IME_MODE_ENGLISH) {
        apply_english_keyboard(input_method);
    } else {
        lv_keyboard_set_map(input_method->keyboard, LV_KEYBOARD_MODE_USER_3,
                            s_number_keyboard_map, s_number_keyboard_ctrl);
        lv_keyboard_set_mode(input_method->keyboard, LV_KEYBOARD_MODE_USER_3);
    }
#else
    if (mode == PXSYS_REFERENCE_IME_MODE_CHINESE ||
        mode == PXSYS_REFERENCE_IME_MODE_ENGLISH)
        lv_keyboard_set_mode(input_method->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    else
        lv_keyboard_set_mode(input_method->keyboard, LV_KEYBOARD_MODE_NUMBER);
#endif
}

pxsys_reference_ime_mode_t pxsys_reference_ime_get_mode(
    const pxsys_reference_ime_t *input_method) {
    return input_method == NULL ? PXSYS_REFERENCE_IME_MODE_CHINESE
                                : input_method->mode;
}

void pxsys_reference_ime_show(pxsys_reference_ime_t *input_method) {
    if (input_method == NULL || input_method->root == NULL) return;
    lv_obj_remove_flag(input_method->root, LV_OBJ_FLAG_HIDDEN);
    if (input_method->mode == PXSYS_REFERENCE_IME_MODE_SYMBOLS) {
        lv_obj_remove_flag(input_method->symbol_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(input_method->keyboard, LV_OBJ_FLAG_HIDDEN);
        input_method_hide_pinyin_candidates(input_method);
    } else {
        lv_obj_remove_flag(input_method->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(input_method->symbol_panel, LV_OBJ_FLAG_HIDDEN);
        if (input_method->mode != PXSYS_REFERENCE_IME_MODE_CHINESE)
            input_method_hide_pinyin_candidates(input_method);
    }
}

void pxsys_reference_ime_hide(pxsys_reference_ime_t *input_method) {
    const bool was_visible =
        input_method != NULL && input_method->root != NULL &&
        !lv_obj_has_flag(input_method->root, LV_OBJ_FLAG_HIDDEN);
    if (input_method == NULL || input_method->root == NULL) return;
    input_method_hide_pinyin_candidates(input_method);
    lv_obj_add_flag(input_method->root, LV_OBJ_FLAG_HIDDEN);
    if (was_visible && input_method->close_callback != NULL)
        input_method->close_callback(input_method->close_context);
}

void pxsys_reference_ime_set_close_callback(pxsys_reference_ime_t *input_method,
                                            void (*callback)(void* context),
                                            void* context) {
    if (input_method == NULL) return;
    input_method->close_callback = callback;
    input_method->close_context = context;
}

bool pxsys_reference_ime_is_visible(const pxsys_reference_ime_t *input_method) {
    return input_method != NULL && input_method->root != NULL &&
           !lv_obj_has_flag(input_method->root, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *pxsys_reference_ime_get_root(const pxsys_reference_ime_t *input_method) {
    return input_method == NULL ? NULL : input_method->root;
}

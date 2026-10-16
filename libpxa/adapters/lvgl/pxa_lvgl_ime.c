#include "pxa/lvgl/pxa_lvgl_ime.h"
#include "pxa_lvgl_ime_builtin.h"
#include "lvgl.h"
#include "fonts/ime_label_12.h"
#include "fonts/ime_latin_12.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct pxa_lvgl_ime {
    pxa_lvgl_ime_config_t config;
    lv_obj_t *target, *root, *preedit, *candidates, *keys, *toolbar;
    lv_obj_t *key_rows[3];
    lv_style_t matrix_style, item_style, pressed_style;
    pxa_lvgl_ime_builtin_t *builtin;
    lv_obj_t *scroll_parent;
    lv_obj_t *editor_view, *editor_text, *editor_caret, *editor_navigation;
    int32_t previous_scroll_y;
    uint32_t editor_start, editor_end, editor_cursor;
    char *editor_buffer;
    lv_area_t observed_editor;
    lv_font_t *sized_font;
    unsigned sized_font_px, sized_font_limit;
    uint64_t generation, fingerprint;
    uint32_t page, visible, *previous_page;
    unsigned page_depth, page_capacity, spelling_start, spelling_visible;
    uint16_t candidate_count, spelling_count;
    uint8_t layout, language, panel, chinese_layout, pending_switch;
    uint8_t symbol_page, multi_key, multi_index;
    uint32_t shift_since, multi_since;
    char *items;
    const char **candidate_map;
    lv_font_t font, cjk_font, label_font;
    lv_timer_t *retry;
    struct { uint32_t value; uint16_t action; uint8_t layout, language; char text[8]; } queue[16];
    uint8_t queue_head, queue_count;
    lv_point_t gesture_start;
    uint32_t awaiting_since, started_since;
    uint32_t opened_since, first_draw_ms, ready_ms;
    uint8_t footer_weights;
    /* UI-thread-owned flags: no atomic or concurrent bitfield updates. Keep
     * state compact without a separate allocation or cached keyboard maps. */
    bool contour_keys:1, editor_prefix:1, view_visible:1, refreshing:1;
    bool editing:1, sensitive:1, uppercase:1, caps_lock:1, more:1, held:1;
    bool ready:1, awaiting:1, expanded:1, gesture_swiped:1, drawn:1, provider_ready:1;
    bool waiting_release:1, arc_toolbar:1, local:1, dispatching:1, traditional:1;

};
/* One set of immutable maps serves both rectangular and contoured displays.
 * Row views avoid per-instance pointer tables; no Latin alphabet paging. */
enum { PANEL_LETTERS, PANEL_NUMBERS, PANEL_SYMBOLS, PANEL_TOOLS };
enum { SWITCH_NONE, SWITCH_LANGUAGE, SWITCH_LAYOUT, SWITCH_CLOSE, SWITCH_SCRIPT, MOVE_LEFT, MOVE_RIGHT };
typedef struct {
    const char *const *map;
    const char *const *row[4];
    unsigned rows;
} ime_keyset_t;
static const char *const nine_cn_done_0[] = {"标点","2 ABC","3 DEF",LV_SYMBOL_BACKSPACE,""};
static const char *const nine_cn_done_1[] = {"4 GHI","5 JKL","6 MNO","分词",""};
static const char *const nine_cn_done_2[] = {"7 PQRS","8 TUV","9 WXYZ","完成",""};
static const char *const nine_cn_done_map[] = {"标点","2 ABC","3 DEF",LV_SYMBOL_BACKSPACE,"\n","4 GHI","5 JKL","6 MNO","分词","\n","7 PQRS","8 TUV","9 WXYZ","完成",""};
static const ime_keyset_t nine_cn_done = {nine_cn_done_map,{nine_cn_done_0,nine_cn_done_1,nine_cn_done_2},3};
static const char *const nine_en_lower_done_0[] = {"标点","2 abc","3 def",LV_SYMBOL_BACKSPACE,""};
static const char *const nine_en_lower_done_1[] = {"4 ghi","5 jkl","6 mno",LV_SYMBOL_UP,""};
static const char *const nine_en_lower_done_2[] = {"7 pqrs","8 tuv","9 wxyz","完成",""};
static const char *const nine_en_lower_done_map[] = {"标点","2 abc","3 def",LV_SYMBOL_BACKSPACE,"\n","4 ghi","5 jkl","6 mno",LV_SYMBOL_UP,"\n","7 pqrs","8 tuv","9 wxyz","完成",""};
static const ime_keyset_t nine_en_lower_done = {nine_en_lower_done_map,{nine_en_lower_done_0,nine_en_lower_done_1,nine_en_lower_done_2},3};
static const char *const full_en_lower_done_0[] = {"q","w","e","r","t","y","u","i","o","p",""};
static const char *const full_en_lower_done_1[] = {"a","s","d","f","g","h","j","k","l",LV_SYMBOL_BACKSPACE,""};
static const char *const full_en_lower_done_2[] = {LV_SYMBOL_UP,"z","x","c","v","b","n","m","完成",""};
static const char *const full_en_lower_done_map[] = {"q","w","e","r","t","y","u","i","o","p","\n","a","s","d","f","g","h","j","k","l",LV_SYMBOL_BACKSPACE,"\n",LV_SYMBOL_UP,"z","x","c","v","b","n","m","完成",""};
static const ime_keyset_t full_en_lower_done = {full_en_lower_done_map,{full_en_lower_done_0,full_en_lower_done_1,full_en_lower_done_2},3};
static const char *const nine_en_upper_done_0[] = {"标点","2 ABC","3 DEF",LV_SYMBOL_BACKSPACE,""};
static const char *const nine_en_upper_done_1[] = {"4 GHI","5 JKL","6 MNO",LV_SYMBOL_UP,""};
static const char *const nine_en_upper_done_2[] = {"7 PQRS","8 TUV","9 WXYZ","完成",""};
static const char *const nine_en_upper_done_map[] = {"标点","2 ABC","3 DEF",LV_SYMBOL_BACKSPACE,"\n","4 GHI","5 JKL","6 MNO",LV_SYMBOL_UP,"\n","7 PQRS","8 TUV","9 WXYZ","完成",""};
static const ime_keyset_t nine_en_upper_done = {nine_en_upper_done_map,{nine_en_upper_done_0,nine_en_upper_done_1,nine_en_upper_done_2},3};
static const char *const full_en_upper_done_0[] = {"Q","W","E","R","T","Y","U","I","O","P",""};
static const char *const full_en_upper_done_1[] = {"A","S","D","F","G","H","J","K","L",LV_SYMBOL_BACKSPACE,""};
static const char *const full_en_upper_done_2[] = {LV_SYMBOL_UP,"Z","X","C","V","B","N","M","完成",""};
static const char *const full_en_upper_done_map[] = {"Q","W","E","R","T","Y","U","I","O","P","\n","A","S","D","F","G","H","J","K","L",LV_SYMBOL_BACKSPACE,"\n",LV_SYMBOL_UP,"Z","X","C","V","B","N","M","完成",""};
static const ime_keyset_t full_en_upper_done = {full_en_upper_done_map,{full_en_upper_done_0,full_en_upper_done_1,full_en_upper_done_2},3};
static const char *const full_cn_done_0[] = {"q","w","e","r","t","y","u","i","o","p",""};
static const char *const full_cn_done_1[] = {"a","s","d","f","g","h","j","k","l",LV_SYMBOL_BACKSPACE,""};
static const char *const full_cn_done_2[] = {"分词","z","x","c","v","b","n","m","完成",""};
static const char *const full_cn_done_map[] = {"q","w","e","r","t","y","u","i","o","p","\n","a","s","d","f","g","h","j","k","l",LV_SYMBOL_BACKSPACE,"\n","分词","z","x","c","v","b","n","m","完成",""};
static const ime_keyset_t full_cn_done = {full_cn_done_map,{full_cn_done_0,full_cn_done_1,full_cn_done_2},3};
static const char *const number_done_0[] = {"1","2","3",LV_SYMBOL_BACKSPACE,""};
static const char *const number_done_1[] = {"4","5","6","00",""};
static const char *const number_done_2[] = {"7","8","9","/",""};
static const char *const number_done_3[] = {"-","0",".","完成",""};
static const char *const number_done_map[] = {"1","2","3",LV_SYMBOL_BACKSPACE,"\n","4","5","6","00","\n","7","8","9","/","\n","-","0",".","完成",""};
static const ime_keyset_t number_done = {number_done_map,{number_done_0,number_done_1,number_done_2,number_done_3},4};
static const char *const cn_symbols_0_done_0[] = {"，","。","！","？","、","；",LV_SYMBOL_BACKSPACE,""};
static const char *const cn_symbols_0_done_1[] = {"：","（","）","“","”","…","下一页",""};
static const char *const cn_symbols_0_done_2[] = {"《","》","【","】","￥","％","完成",""};
static const char *const cn_symbols_0_done_map[] = {"，","。","！","？","、","；",LV_SYMBOL_BACKSPACE,"\n","：","（","）","“","”","…","下一页","\n","《","》","【","】","￥","％","完成",""};
static const ime_keyset_t cn_symbols_0_done = {cn_symbols_0_done_map,{cn_symbols_0_done_0,cn_symbols_0_done_1,cn_symbols_0_done_2},3};
static const char *const cn_symbols_1_done_0[] = {"＋","－","＝","@","#","&",LV_SYMBOL_BACKSPACE,""};
static const char *const cn_symbols_1_done_1[] = {"/","\\","_","|","~","^","上一页",""};
static const char *const cn_symbols_1_done_2[] = {"<",">","$","*","{","}","完成",""};
static const char *const cn_symbols_1_done_map[] = {"＋","－","＝","@","#","&",LV_SYMBOL_BACKSPACE,"\n","/","\\","_","|","~","^","上一页","\n","<",">","$","*","{","}","完成",""};
static const ime_keyset_t cn_symbols_1_done = {cn_symbols_1_done_map,{cn_symbols_1_done_0,cn_symbols_1_done_1,cn_symbols_1_done_2},3};
static const char *const en_symbols_0_done_0[] = {",",".","!","?","'","\"",LV_SYMBOL_BACKSPACE,""};
static const char *const en_symbols_0_done_1[] = {";",":","(",")","[","]","下一页",""};
static const char *const en_symbols_0_done_2[] = {"{","}","@","#","$","%","完成",""};
static const char *const en_symbols_0_done_map[] = {",",".","!","?","'","\"",LV_SYMBOL_BACKSPACE,"\n",";",":","(",")","[","]","下一页","\n","{","}","@","#","$","%","完成",""};
static const ime_keyset_t en_symbols_0_done = {en_symbols_0_done_map,{en_symbols_0_done_0,en_symbols_0_done_1,en_symbols_0_done_2},3};
static const char *const en_symbols_1_done_0[] = {"+","-","=","*","/","&",LV_SYMBOL_BACKSPACE,""};
static const char *const en_symbols_1_done_1[] = {"\\","_","|","~","^","`","上一页",""};
static const char *const en_symbols_1_done_2[] = {"<",">","!","?",";",":","完成",""};
static const char *const en_symbols_1_done_map[] = {"+","-","=","*","/","&",LV_SYMBOL_BACKSPACE,"\n","\\","_","|","~","^","`","上一页","\n","<",">","!","?",";",":","完成",""};
static const ime_keyset_t en_symbols_1_done = {en_symbols_1_done_map,{en_symbols_1_done_0,en_symbols_1_done_1,en_symbols_1_done_2},3};
static const char *const nine_cn_line_0[] = {"标点","2 ABC","3 DEF",LV_SYMBOL_BACKSPACE,""};
static const char *const nine_cn_line_1[] = {"4 GHI","5 JKL","6 MNO","分词",""};
static const char *const nine_cn_line_2[] = {"7 PQRS","8 TUV","9 WXYZ",LV_SYMBOL_NEW_LINE,""};
static const char *const nine_cn_line_map[] = {"标点","2 ABC","3 DEF",LV_SYMBOL_BACKSPACE,"\n","4 GHI","5 JKL","6 MNO","分词","\n","7 PQRS","8 TUV","9 WXYZ",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t nine_cn_line = {nine_cn_line_map,{nine_cn_line_0,nine_cn_line_1,nine_cn_line_2},3};
static const char *const nine_en_lower_line_0[] = {"标点","2 abc","3 def",LV_SYMBOL_BACKSPACE,""};
static const char *const nine_en_lower_line_1[] = {"4 ghi","5 jkl","6 mno",LV_SYMBOL_UP,""};
static const char *const nine_en_lower_line_2[] = {"7 pqrs","8 tuv","9 wxyz",LV_SYMBOL_NEW_LINE,""};
static const char *const nine_en_lower_line_map[] = {"标点","2 abc","3 def",LV_SYMBOL_BACKSPACE,"\n","4 ghi","5 jkl","6 mno",LV_SYMBOL_UP,"\n","7 pqrs","8 tuv","9 wxyz",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t nine_en_lower_line = {nine_en_lower_line_map,{nine_en_lower_line_0,nine_en_lower_line_1,nine_en_lower_line_2},3};
static const char *const full_en_lower_line_0[] = {"q","w","e","r","t","y","u","i","o","p",""};
static const char *const full_en_lower_line_1[] = {"a","s","d","f","g","h","j","k","l",LV_SYMBOL_BACKSPACE,""};
static const char *const full_en_lower_line_2[] = {LV_SYMBOL_UP,"z","x","c","v","b","n","m",LV_SYMBOL_NEW_LINE,""};
static const char *const full_en_lower_line_map[] = {"q","w","e","r","t","y","u","i","o","p","\n","a","s","d","f","g","h","j","k","l",LV_SYMBOL_BACKSPACE,"\n",LV_SYMBOL_UP,"z","x","c","v","b","n","m",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t full_en_lower_line = {full_en_lower_line_map,{full_en_lower_line_0,full_en_lower_line_1,full_en_lower_line_2},3};
static const char *const nine_en_upper_line_0[] = {"标点","2 ABC","3 DEF",LV_SYMBOL_BACKSPACE,""};
static const char *const nine_en_upper_line_1[] = {"4 GHI","5 JKL","6 MNO",LV_SYMBOL_UP,""};
static const char *const nine_en_upper_line_2[] = {"7 PQRS","8 TUV","9 WXYZ",LV_SYMBOL_NEW_LINE,""};
static const char *const nine_en_upper_line_map[] = {"标点","2 ABC","3 DEF",LV_SYMBOL_BACKSPACE,"\n","4 GHI","5 JKL","6 MNO",LV_SYMBOL_UP,"\n","7 PQRS","8 TUV","9 WXYZ",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t nine_en_upper_line = {nine_en_upper_line_map,{nine_en_upper_line_0,nine_en_upper_line_1,nine_en_upper_line_2},3};
static const char *const full_en_upper_line_0[] = {"Q","W","E","R","T","Y","U","I","O","P",""};
static const char *const full_en_upper_line_1[] = {"A","S","D","F","G","H","J","K","L",LV_SYMBOL_BACKSPACE,""};
static const char *const full_en_upper_line_2[] = {LV_SYMBOL_UP,"Z","X","C","V","B","N","M",LV_SYMBOL_NEW_LINE,""};
static const char *const full_en_upper_line_map[] = {"Q","W","E","R","T","Y","U","I","O","P","\n","A","S","D","F","G","H","J","K","L",LV_SYMBOL_BACKSPACE,"\n",LV_SYMBOL_UP,"Z","X","C","V","B","N","M",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t full_en_upper_line = {full_en_upper_line_map,{full_en_upper_line_0,full_en_upper_line_1,full_en_upper_line_2},3};
static const char *const full_cn_line_0[] = {"q","w","e","r","t","y","u","i","o","p",""};
static const char *const full_cn_line_1[] = {"a","s","d","f","g","h","j","k","l",LV_SYMBOL_BACKSPACE,""};
static const char *const full_cn_line_2[] = {"分词","z","x","c","v","b","n","m",LV_SYMBOL_NEW_LINE,""};
static const char *const full_cn_line_map[] = {"q","w","e","r","t","y","u","i","o","p","\n","a","s","d","f","g","h","j","k","l",LV_SYMBOL_BACKSPACE,"\n","分词","z","x","c","v","b","n","m",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t full_cn_line = {full_cn_line_map,{full_cn_line_0,full_cn_line_1,full_cn_line_2},3};
static const char *const number_line_0[] = {"1","2","3",LV_SYMBOL_BACKSPACE,""};
static const char *const number_line_1[] = {"4","5","6","00",""};
static const char *const number_line_2[] = {"7","8","9","/",""};
static const char *const number_line_3[] = {"-","0",".",LV_SYMBOL_NEW_LINE,""};
static const char *const number_line_map[] = {"1","2","3",LV_SYMBOL_BACKSPACE,"\n","4","5","6","00","\n","7","8","9","/","\n","-","0",".",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t number_line = {number_line_map,{number_line_0,number_line_1,number_line_2,number_line_3},4};
static const char *const cn_symbols_0_line_0[] = {"，","。","！","？","、","；",LV_SYMBOL_BACKSPACE,""};
static const char *const cn_symbols_0_line_1[] = {"：","（","）","“","”","…","下一页",""};
static const char *const cn_symbols_0_line_2[] = {"《","》","【","】","￥","％",LV_SYMBOL_NEW_LINE,""};
static const char *const cn_symbols_0_line_map[] = {"，","。","！","？","、","；",LV_SYMBOL_BACKSPACE,"\n","：","（","）","“","”","…","下一页","\n","《","》","【","】","￥","％",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t cn_symbols_0_line = {cn_symbols_0_line_map,{cn_symbols_0_line_0,cn_symbols_0_line_1,cn_symbols_0_line_2},3};
static const char *const cn_symbols_1_line_0[] = {"＋","－","＝","@","#","&",LV_SYMBOL_BACKSPACE,""};
static const char *const cn_symbols_1_line_1[] = {"/","\\","_","|","~","^","上一页",""};
static const char *const cn_symbols_1_line_2[] = {"<",">","$","*","{","}",LV_SYMBOL_NEW_LINE,""};
static const char *const cn_symbols_1_line_map[] = {"＋","－","＝","@","#","&",LV_SYMBOL_BACKSPACE,"\n","/","\\","_","|","~","^","上一页","\n","<",">","$","*","{","}",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t cn_symbols_1_line = {cn_symbols_1_line_map,{cn_symbols_1_line_0,cn_symbols_1_line_1,cn_symbols_1_line_2},3};
static const char *const en_symbols_0_line_0[] = {",",".","!","?","'","\"",LV_SYMBOL_BACKSPACE,""};
static const char *const en_symbols_0_line_1[] = {";",":","(",")","[","]","下一页",""};
static const char *const en_symbols_0_line_2[] = {"{","}","@","#","$","%",LV_SYMBOL_NEW_LINE,""};
static const char *const en_symbols_0_line_map[] = {",",".","!","?","'","\"",LV_SYMBOL_BACKSPACE,"\n",";",":","(",")","[","]","下一页","\n","{","}","@","#","$","%",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t en_symbols_0_line = {en_symbols_0_line_map,{en_symbols_0_line_0,en_symbols_0_line_1,en_symbols_0_line_2},3};
static const char *const en_symbols_1_line_0[] = {"+","-","=","*","/","&",LV_SYMBOL_BACKSPACE,""};
static const char *const en_symbols_1_line_1[] = {"\\","_","|","~","^","`","上一页",""};
static const char *const en_symbols_1_line_2[] = {"<",">","!","?",";",":",LV_SYMBOL_NEW_LINE,""};
static const char *const en_symbols_1_line_map[] = {"+","-","=","*","/","&",LV_SYMBOL_BACKSPACE,"\n","\\","_","|","~","^","`","上一页","\n","<",">","!","?",";",":",LV_SYMBOL_NEW_LINE,""};
static const ime_keyset_t en_symbols_1_line = {en_symbols_1_line_map,{en_symbols_1_line_0,en_symbols_1_line_1,en_symbols_1_line_2},3};
static const char *const tools_0[] = {"九键","全键",""};
static const char *const tools_1[] = {"中文","English",""};
static const char *const tools_2[] = {LV_SYMBOL_LEFT,"返回",LV_SYMBOL_RIGHT,""};
static const char *const tools_map[] = {"九键","全键","\n","中文","English","\n",LV_SYMBOL_LEFT,"返回",LV_SYMBOL_RIGHT,""};
static const ime_keyset_t tools = {tools_map,{tools_0,tools_1,tools_2},3};
static const char *const tools_script_2[] = {"简体","繁體",""};
static const char *const tools_script_map[] = {"九键","全键","\n","中文","English","\n","简体","繁體","\n",LV_SYMBOL_LEFT,"返回",LV_SYMBOL_RIGHT,""};
static const ime_keyset_t tools_script = {tools_script_map,{tools_0,tools_1,tools_script_2,tools_2},4};
static const char *const toolbar_letters_cn[] = {"符","123","中","空格",LV_SYMBOL_DOWN,""};
static const char *const toolbar_numbers_cn[] = {"符","返回","中","空格",LV_SYMBOL_DOWN,""};
static const char *const toolbar_symbols_cn[] = {"返回","123","中","空格",LV_SYMBOL_DOWN,""};
static const char *const toolbar_letters_en[] = {"符","123","EN","空格",LV_SYMBOL_DOWN,""};
static const char *const toolbar_numbers_en[] = {"符","返回","EN","空格",LV_SYMBOL_DOWN,""};
static const char *const toolbar_symbols_en[] = {"返回","123","EN","空格",LV_SYMBOL_DOWN,""};
/* One matrix, with invisible side cells fitting the lower circular chord.
 * No additional widget or per-instance map is needed for the second row. */
static const char *const arc_letters_cn[] = {"符","123","中","\n"," ","空格",LV_SYMBOL_DOWN," ",""};
static const char *const arc_numbers_cn[] = {"符","返回","中","\n"," ","空格",LV_SYMBOL_DOWN," ",""};
static const char *const arc_symbols_cn[] = {"返回","123","中","\n"," ","空格",LV_SYMBOL_DOWN," ",""};
static const char *const arc_letters_en[] = {"符","123","EN","\n"," ","空格",LV_SYMBOL_DOWN," ",""};
static const char *const arc_numbers_en[] = {"符","返回","EN","\n"," ","空格",LV_SYMBOL_DOWN," ",""};
static const char *const arc_symbols_en[] = {"返回","123","EN","\n"," ","空格",LV_SYMBOL_DOWN," ",""};
static const char *const idle_nine[] = {LV_SYMBOL_SETTINGS,"中文 · 九键",""};
static const char *const idle_full[] = {LV_SYMBOL_SETTINGS,"中文 · 全键",""};
static const char *const idle_english[] = {LV_SYMBOL_SETTINGS,"English",""};
static const char *const idle_password[] = {LV_SYMBOL_SETTINGS,"English · 密码",""};
static const char *const idle_loading[] = {LV_SYMBOL_SETTINGS,"正在准备…",""};
static const char *const empty_map[] = {" ",""};
static const char *const loading_map[] = {"正在准备…",""};
static const char *const navigation_map[] = {LV_SYMBOL_LEFT,LV_SYMBOL_RIGHT,""};
/* Fixed rail geometry belongs in ROM, not separate local style allocations
 * for every editor. The same style also suppresses disabled highlights. */
static const lv_style_const_prop_t rail_properties[] = {
    {.prop=LV_STYLE_BG_OPA,.value={.num=LV_OPA_TRANSP}},
    {.prop=LV_STYLE_BORDER_WIDTH,.value={.num=0}},
    {.prop=LV_STYLE_OUTLINE_WIDTH,.value={.num=0}},
    {.prop=LV_STYLE_SHADOW_WIDTH,.value={.num=0}},
    {.prop=LV_STYLE_RADIUS,.value={.num=2}},
    LV_STYLE_CONST_PROPS_END,
};
static LV_STYLE_CONST_INIT(rail_style,rail_properties);
static const lv_style_const_prop_t disabled_properties[] = {
    {.prop=LV_STYLE_TEXT_OPA,.value={.num=LV_OPA_50}},
    LV_STYLE_CONST_PROPS_END,
};
static LV_STYLE_CONST_INIT(disabled_style,disabled_properties);
static void target_event(lv_event_t *event);
static void rebuild_keys(pxa_lvgl_ime_t *ime);
static const char *composition(const pxa_lvgl_ime_t *ime);
static void layout(pxa_lvgl_ime_t *ime, bool choose_layout);
static void pack_spellings(pxa_lvgl_ime_t *ime);
static lv_obj_t *matrix(lv_obj_t *root,lv_event_cb_t callback,pxa_lvgl_ime_t *ime);
static int text_width(const pxa_lvgl_ime_t *ime, const char *text);
static int px(const pxa_lvgl_ime_t *ime, int dp);
static void refresh_editor(pxa_lvgl_ime_t *ime, bool reveal);
static void scroll_parent_deleted(lv_event_t *event) {
    pxa_lvgl_ime_t *ime=lv_event_get_user_data(event); ime->scroll_parent=NULL;
}
static void flush_input(pxa_lvgl_ime_t *ime);
static pxa_status_t local_present(void *context,const pxa_ime_frame_t *frame);
static pxa_status_t send_action(pxa_lvgl_ime_t *ime,uint16_t action,uint32_t value,const char *text);
static void restart_session(pxa_lvgl_ime_t *ime);
static bool sync_sensitive_mode(pxa_lvgl_ime_t *ime);
static pxa_status_t fallback(pxa_lvgl_ime_t *ime);
static bool needs_provider(const pxa_lvgl_ime_t *ime) {
    return ime->target && !ime->sensitive && ime->language==PXA_IME_CHINESE && !ime->local;
}
static bool busy(const pxa_lvgl_ime_t *ime) { return !ime->ready || ime->awaiting || ime->queue_count || ime->waiting_release; }
static lv_obj_t *key_row(pxa_lvgl_ime_t *ime,unsigned row) {
    return row ? ime->key_rows[row-1] : ime->keys;
}
static unsigned space_button(const pxa_lvgl_ime_t *ime) { return ime->arc_toolbar?4:3; }
static void update_interactivity(pxa_lvgl_ime_t *ime) {
    lv_obj_set_state(ime->candidates, LV_STATE_DISABLED, busy(ime));
    for (unsigned row=0;row<4;++row)
        if (key_row(ime,row)) lv_obj_set_state(key_row(ime,row),LV_STATE_DISABLED,ime->queue_count==16 || ime->pending_switch || ime->waiting_release);
    if (ime->retry) {
        if (busy(ime)) {
            lv_timer_set_period(ime->retry,!ime->ready || ime->awaiting ? 100 : 8); lv_timer_resume(ime->retry);
        } else if (ime->target) {
            /* LVGL has no public caret-changed event for programmatic moves.
             * O(1) caret/geometry polling reuses this timer; no Guest polling. */
            lv_timer_set_period(ime->retry,80); lv_timer_resume(ime->retry);
        } else lv_timer_pause(ime->retry);
    }
}
static void flush_input(pxa_lvgl_ime_t *ime) {
    if (!ime->target || ime->dispatching || !ime->ready || ime->awaiting || !ime->queue_count) return;
    if (ime->local) {
        ime->dispatching=true;
        /* Local replies can request another confirm during language/close
         * switching. Drain iteratively, never recurse through PRESENT. */
        for (unsigned budget=16;budget && ime->target && ime->queue_count;--budget) {
            unsigned i=ime->queue_head;
            uint16_t action=ime->queue[i].action; uint32_t value=ime->queue[i].value;
            uint8_t input_layout=ime->queue[i].layout;
            char text[sizeof(ime->queue[i].text)]; memcpy(text,ime->queue[i].text,sizeof(text));
            ime->queue_head=(ime->queue_head+1)%16; --ime->queue_count;
            ime->awaiting=true;
            pxa_status_t status=pxa_lvgl_ime_builtin_input(ime->builtin,action,input_layout,value,
                (pxa_bytes_t){(const uint8_t *)text,strlen(text)},local_present,ime);
            if (status!=PXA_STATUS_OK) { ime->awaiting=false; pxa_lvgl_ime_close(ime); break; }
        }
        ime->dispatching=false; update_interactivity(ime); return;
    }
    unsigned i = ime->queue_head;
    pxa_status_t status = ime->config.input(ime->config.context, ime->generation,
        ime->queue[i].action, ime->queue[i].layout, ime->queue[i].language,
        ime->queue[i].value, (pxa_bytes_t){(uint8_t *)ime->queue[i].text, strlen(ime->queue[i].text)});
    if (status == PXA_STATUS_OK) {
        ime->queue_head = (ime->queue_head + 1) % 16; --ime->queue_count;
        ime->awaiting = true; ime->awaiting_since = lv_tick_get();
    } else if (status != PXA_STATUS_WOULD_BLOCK) (void)fallback(ime);
    update_interactivity(ime);
}
static void retry_input(lv_timer_t *timer) {
    pxa_lvgl_ime_t *ime = lv_timer_get_user_data(timer);
    (void)sync_sensitive_mode(ime);
    if (ime->multi_key && lv_tick_elaps(ime->multi_since)>=650) {
        ime->multi_key=0;
        if (ime->uppercase && !ime->caps_lock) { ime->uppercase=false; rebuild_keys(ime); }
    }
    // Full inventory verification precedes RESET and can take seconds on flash.
    // Queue early taps until RESET is acknowledged; do not lose the first key.
    if (!ime->ready && lv_tick_elaps(ime->started_since) > 10000) (void)fallback(ime);
    if (ime->awaiting && lv_tick_elaps(ime->awaiting_since) > 3000) (void)fallback(ime);
    flush_input(ime);
    if (ime->target) {
        lv_area_t area; lv_obj_get_coords(ime->target,&area);
        bool moved=ime->editor_cursor != lv_textarea_get_cursor_pos(ime->target);
        if (moved) { ime->multi_key=0; restart_session(ime); }
        if (moved || memcmp(&area,&ime->observed_editor,sizeof(area))) refresh_editor(ime,true);
    }
}
static void turn_word_page(pxa_lvgl_ime_t *ime, bool next) {
    if (!next) {
        if (ime->page_depth) (void)send_action(ime,PXA_IME_PAGE,ime->previous_page[--ime->page_depth],NULL);
        else if (ime->page) (void)send_action(ime,PXA_IME_PAGE,0,NULL);
        return;
    }
    if (!ime->visible || (!ime->more && ime->visible>=ime->candidate_count)) return;
    if (ime->page_depth==32) {
        memmove(ime->previous_page,ime->previous_page+1,31*sizeof(*ime->previous_page));
        ime->page_depth=31;
    }
    if (ime->page_depth==ime->page_capacity) {
        unsigned capacity=ime->page_capacity?ime->page_capacity*2:4;
        void *pages=realloc(ime->previous_page,capacity*sizeof(*ime->previous_page));
        if (!pages) return;
        ime->previous_page=pages; ime->page_capacity=capacity;
    }
    ime->previous_page[ime->page_depth++]=ime->page;
    (void)send_action(ime,PXA_IME_PAGE,ime->page+ime->visible,NULL);
}
static void header_event(lv_event_t *event) {
    pxa_lvgl_ime_t *ime=lv_event_get_user_data(event);
    lv_indev_t *pointer=lv_event_get_indev(event);
    if (!pointer || lv_indev_get_type(pointer)!=LV_INDEV_TYPE_POINTER) return;
    lv_event_code_t code=lv_event_get_code(event);
    if (code==LV_EVENT_PRESSED) {
        lv_indev_get_point(pointer,&ime->gesture_start); ime->gesture_swiped=false;
    } else if (code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) {
        ime->gesture_swiped=false;
    } else if (code==LV_EVENT_PRESSING && !ime->gesture_swiped && !busy(ime)) {
        lv_point_t point; lv_indev_get_point(pointer,&point);
        int dx=point.x-ime->gesture_start.x,dy=point.y-ime->gesture_start.y;
        int threshold=px(ime,12);
        int maximum=lv_obj_get_width(ime->candidates)/6;
        if (threshold>maximum) threshold=maximum;
        if (threshold<10) threshold=10;
        if (abs(dx)<threshold && abs(dy)<threshold) return;
        ime->gesture_swiped=true;
        lv_area_t box; lv_obj_get_content_coords(ime->candidates,&box);
        const char *const *map=lv_buttonmatrix_get_map(ime->candidates);
        unsigned rows=1;
        for (unsigned i=0;*map[i];++i) if (!strcmp(map[i],"\n")) ++rows;
        const int gap=lv_obj_get_style_pad_row(ime->candidates,0);
        const int first_height=(lv_area_get_height(&box)-gap*(int)(rows-1))/(int)rows;
        bool spelling=ime->layout==PXA_IME_K9 && ime->spelling_count &&
            ime->gesture_start.y<box.y1+first_height;
        if (abs(dx)>abs(dy)*3/2) {
            if (spelling) {
                if (dx<0 && ime->spelling_start+ime->spelling_visible<ime->spelling_count)
                    ime->spelling_start+=ime->spelling_visible;
                else if (dx>0) ime->spelling_start=ime->spelling_start>ime->spelling_visible?
                    ime->spelling_start-ime->spelling_visible:0;
                pack_spellings(ime);
            } else turn_word_page(ime,dx<0);
        } else if (abs(dy)>abs(dx)*3/2 && ime->candidate_count &&
                   ((dy<0 && !ime->expanded) || (dy>0 && ime->expanded))) {
            ime->expanded=dy<0; ime->panel=PANEL_LETTERS; layout(ime,false);
        }
        lv_buttonmatrix_set_selected_button(ime->candidates,LV_BUTTONMATRIX_BUTTON_NONE);
        /* Consume even an out-of-band drag: releasing must not choose a word
         * or press a key below the candidate strip. */
        lv_indev_wait_release(pointer);
    }
}

static uint64_t text_fingerprint(lv_obj_t *target) {
    const unsigned char *p = (const unsigned char *)lv_textarea_get_text(target);
    uint64_t hash = UINT64_C(14695981039346656037);
    while (*p) hash = (hash ^ *p++) * UINT64_C(1099511628211);
    return hash;
}
static int px(const pxa_lvgl_ime_t *ime, int dp) {
    uint32_t density = ime->config.environment.density_q16;
    return (int)(((uint64_t)(density ? density : 65536) * (unsigned)dp + 32768) / 65536);
}
static void clear_presentation(pxa_lvgl_ime_t *ime) {
    if (ime->preedit) lv_label_set_text(ime->preedit, ime->sensitive ? "英文 · 密码" : ime->language ? "English" : "拼音");
    lv_buttonmatrix_set_map(ime->candidates,ime->ready?empty_map:loading_map);
    free(ime->items); ime->items = NULL;
    ime->candidate_map=NULL;
    ime->candidate_count = ime->spelling_count = 0;
    ime->visible = 0; ime->page_depth = 0;
    pack_spellings(ime);
    update_interactivity(ime);
}
static void restart_session(pxa_lvgl_ime_t *ime) {
    if (!ime->target) return;
    if (ime->generation == UINT64_MAX) { pxa_lvgl_ime_close(ime); return; }
    ++ime->generation; ime->queue_count = 0; ime->awaiting = false; ime->provider_ready=false;
    ime->multi_key=0;
    ime->ready = !needs_provider(ime);
    bool local_fallback=false;
    if (ime->local) {
        unsigned scripts=pxa_lvgl_ime_builtin_scripts();
        if (!(scripts&1u)) ime->traditional=true;
        else if (!(scripts&2u)) ime->traditional=false;
        if (ime->sensitive || ime->language==PXA_IME_ENGLISH) {
            pxa_lvgl_ime_builtin_destroy(ime->builtin); ime->builtin=NULL;
        }
        if (!ime->sensitive && ime->language==PXA_IME_CHINESE && !ime->builtin)
            ime->builtin=pxa_lvgl_ime_builtin_create(ime->root);
        if (!ime->builtin && !ime->sensitive && ime->language==PXA_IME_CHINESE) {
            ime->language=PXA_IME_ENGLISH; ime->layout=PXA_IME_K26;
            local_fallback=true;
        }
        if (ime->builtin) (void)pxa_lvgl_ime_builtin_set_traditional(ime->builtin,ime->traditional);
        ime->provider_ready=ime->builtin!=NULL;
        ime->ready_ms=0;
    }
    ime->started_since = lv_tick_get(); clear_presentation(ime);
    if (local_fallback) layout(ime,false);
    update_interactivity(ime);
    ime->config.focus(ime->config.context, ime->generation, true, needs_provider(ime));
}
static pxa_status_t send_action(pxa_lvgl_ime_t *ime, uint16_t action, uint32_t value, const char *text) {
    if (!ime->target) return PXA_STATUS_BAD_STATE;
    size_t length = text ? strlen(text) : 0;
    if (length >= sizeof(ime->queue[0].text)) return PXA_STATUS_INVALID_ARGUMENT;
    if (ime->queue_count == 16) return PXA_STATUS_WOULD_BLOCK;
    unsigned i = (ime->queue_head + ime->queue_count++) % 16;
    ime->queue[i].action = action; ime->queue[i].value = value;
    ime->queue[i].layout = ime->layout; ime->queue[i].language = ime->language;
    if (length) memcpy(ime->queue[i].text, text, length);
    ime->queue[i].text[length] = 0;
    flush_input(ime); update_interactivity(ime);
    return PXA_STATUS_OK;
}
static void directly(pxa_lvgl_ime_t *ime, const char *text, bool backspace) {
    if (!ime->target) return;
    ime->editing = true;
    if (backspace) lv_textarea_delete_char(ime->target);
    else lv_textarea_add_text(ime->target, text);
    if (ime->target) ime->fingerprint = text_fingerprint(ime->target);
    ime->editing = false;
    refresh_editor(ime,true);
}
static void map_controls(lv_obj_t *matrix) {
    lv_buttonmatrix_set_button_ctrl_all(matrix, LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_NO_REPEAT);
}
static size_t presentation_pointers(unsigned count,unsigned spellings) {
    /* Grid row separators and navigation share the existing reply allocation. */
    return count+spellings+12;
}
static const char *composition(const pxa_lvgl_ime_t *ime) {
    if (!ime->items) return "";
    const char *text=ime->items+(ime->candidate_map?
        presentation_pointers(ime->candidate_count,ime->spelling_count)*sizeof(char *):0);
    return text+strlen(text)+1;
}
static void complete_local(pxa_lvgl_ime_t *ime) {
    if (lv_textarea_get_one_line(ime->target)) {
        lv_obj_send_event(ime->target,LV_EVENT_READY,NULL);
        pxa_lvgl_ime_close(ime);
    } else directly(ime,"\n",false);
}
static void finish_switch(pxa_lvgl_ime_t *ime) {
    if (!ime->pending_switch || !ime->ready || ime->awaiting || ime->queue_count || ime->waiting_release) return;
    if (*composition(ime)) {
        /* Preserve the text, including tails left after choosing a partial word. */
        (void)send_action(ime,PXA_IME_CONFIRM,0,NULL); return;
    }
    uint8_t action=ime->pending_switch; ime->pending_switch=SWITCH_NONE;
    if (action==SWITCH_CLOSE) { pxa_lvgl_ime_close(ime); return; }
    if (action==MOVE_LEFT || action==MOVE_RIGHT) {
        uint32_t before=lv_textarea_get_cursor_pos(ime->target);
        ime->multi_key=0;
        if (action==MOVE_LEFT) lv_textarea_cursor_left(ime->target);
        else lv_textarea_cursor_right(ime->target);
        if (ime->target && before!=lv_textarea_get_cursor_pos(ime->target)) {
            restart_session(ime); refresh_editor(ime,true);
        }
        return;
    }
    bool release=action==SWITCH_LANGUAGE && needs_provider(ime) && ime->provider_ready;
    ime->multi_key=0; ime->expanded=false;
    if (action==SWITCH_LAYOUT || ime->panel==PANEL_TOOLS) ime->panel=PANEL_LETTERS;
    if (action==SWITCH_LANGUAGE) {
        if (!ime->language) { ime->chinese_layout=ime->layout; ime->layout=PXA_IME_K26; }
        else ime->layout=ime->chinese_layout;
        ime->language^=1;
    } else if (action==SWITCH_SCRIPT) ime->traditional=!ime->traditional;
    else { ime->layout^=1; if (!ime->language) ime->chinese_layout=ime->layout; }
    /* The runtime owner unloads outside LVGL's lock. Rebuilding a full keymap
     * before its acknowledgement would overlap it with the live dictionary. */
    ime->waiting_release=release;
    restart_session(ime);
    if (!release) layout(ime,false);
}
void pxa_lvgl_ime_provider_released(pxa_lvgl_ime_t *ime,uint64_t generation) {
    if (!ime || !ime->target || generation!=ime->generation || !ime->waiting_release) return;
    ime->waiting_release=false;
    finish_switch(ime);
    if (ime->target) { layout(ime,false); update_interactivity(ime); }
}
static bool sync_sensitive_mode(pxa_lvgl_ime_t *ime) {
    if (!ime->target || ime->sensitive==lv_textarea_get_password_mode(ime->target)) return false;
    bool release=(needs_provider(ime) && ime->provider_ready) || ime->waiting_release;
    ime->sensitive=lv_textarea_get_password_mode(ime->target);
    ime->language=ime->sensitive?PXA_IME_ENGLISH:ime->config.preferences.language;
    ime->panel=PANEL_LETTERS; ime->pending_switch=SWITCH_NONE; ime->expanded=false;
    ime->uppercase=ime->caps_lock=false;
    ime->waiting_release=release && ime->language==PXA_IME_ENGLISH;
    /* Cancel rather than commit old preedit when the editor becomes sensitive.
     * No extra state or allocation; reuse the generation/unload handshake. */
    restart_session(ime);
    if (!ime->waiting_release) layout(ime,true);
    else { ime->layout=PXA_IME_K26; refresh_editor(ime,true); }
    return true;
}
static void request_switch(pxa_lvgl_ime_t *ime,uint8_t action) {
    if (ime->pending_switch || (ime->waiting_release && action!=SWITCH_CLOSE) ||
        (ime->sensitive && action!=SWITCH_CLOSE && action!=MOVE_LEFT && action!=MOVE_RIGHT)) return;
    ime->pending_switch=action;
    /* With no early Chinese input, English can cancel an unfinished load now. */
    if (!ime->queue_count && !ime->awaiting && !*composition(ime)) ime->ready=true;
    finish_switch(ime); update_interactivity(ime);
}
static const ime_keyset_t *keyset(const pxa_lvgl_ime_t *ime) {
    bool done=ime->target && lv_textarea_get_one_line(ime->target);
#define PICK(name) (done?&name##_done:&name##_line)
    if (ime->panel==PANEL_TOOLS) return ime->local && !ime->sensitive &&
        pxa_lvgl_ime_builtin_scripts()==3 ? &tools_script : &tools;
    if (ime->panel==PANEL_NUMBERS) return PICK(number);
    if (ime->panel==PANEL_SYMBOLS) {
        if (ime->language || ime->sensitive) return ime->symbol_page?PICK(en_symbols_1):PICK(en_symbols_0);
        return ime->symbol_page?PICK(cn_symbols_1):PICK(cn_symbols_0);
    }
    if (ime->layout==PXA_IME_K9) {
        if (!ime->language) return PICK(nine_cn);
        return ime->uppercase?PICK(nine_en_upper):PICK(nine_en_lower);
    }
    if (!ime->language && !ime->sensitive) return PICK(full_cn);
    return ime->uppercase?PICK(full_en_upper):PICK(full_en_lower);
#undef PICK
}
static void edit_key(pxa_lvgl_ime_t *ime,const char *key,bool literal) {
    bool backspace=!strcmp(key,LV_SYMBOL_BACKSPACE);
    bool enter=!strcmp(key,LV_SYMBOL_NEW_LINE) || !strcmp(key,"完成");
    bool space=!strcmp(key,"空格");
    ime->multi_key=0;
    if (ime->sensitive || ime->language==PXA_IME_ENGLISH ||
        (!ime->ready && literal && !ime->queue_count && !ime->awaiting)) {
        if (enter) complete_local(ime);
        else directly(ime,space?" ":key,backspace);
        return;
    }
    pxa_status_t status;
    if (backspace) status=send_action(ime,PXA_IME_BACKSPACE,0,NULL);
    else if (space || (enter && (*composition(ime) || ime->queue_count || ime->awaiting)))
        status=send_action(ime,PXA_IME_CONFIRM,0,NULL);
    else if (enter) status=lv_textarea_get_one_line(ime->target)?
        send_action(ime,PXA_IME_CONFIRM,1,NULL):send_action(ime,PXA_IME_TEXT,1,"\n");
    else status=send_action(ime,PXA_IME_TEXT,literal,key);
    if (status!=PXA_STATUS_OK && ime->preedit) lv_label_set_text(ime->preedit,"请稍候…");
}
static void latin_multi(pxa_lvgl_ime_t *ime,const char *key) {
    uint8_t digit=(uint8_t)(key[0]-'0'); const char *letters=key+2;
    bool replacing=ime->multi_key==digit && lv_tick_elaps(ime->multi_since)<650;
    if (replacing) {
        ime->multi_index=(ime->multi_index+1)%strlen(letters);
    } else {
        if (ime->multi_key && ime->uppercase && !ime->caps_lock) {
            ime->uppercase=false; rebuild_keys(ime);
        }
        ime->multi_index=0;
    }
    char letter=letters[ime->multi_index];
    if (!ime->uppercase && letter>='A' && letter<='Z') letter+='a'-'A';
    /* Reject a filtered replacement before deleting the accepted character.
     * A full editor can reject an initial tap too: only actual insertion may
     * arm multi-tap, otherwise the next tap could erase existing user text. */
    const char *accepted=lv_textarea_get_accepted_chars(ime->target);
    if (accepted && !strchr(accepted,letter)) return;
    if (replacing) directly(ime,"",true);
    if (!ime->target) return;
    uint32_t position=lv_textarea_get_cursor_pos(ime->target);
    char text[2]={letter,0}; directly(ime,text,false);
    ime->multi_key=ime->target && lv_textarea_get_cursor_pos(ime->target)==position+1?digit:0;
    ime->multi_since=lv_tick_get();
}
static bool space_gesture(pxa_lvgl_ime_t *ime,lv_event_t *event,const char *key) {
    if (strcmp(key,"空格")) return false;
    lv_event_code_t code=lv_event_get_code(event); lv_indev_t *pointer=lv_event_get_indev(event);
    if (!pointer || lv_indev_get_type(pointer)!=LV_INDEV_TYPE_POINTER) return false;
    if (code==LV_EVENT_PRESSED) { ime->held=false; lv_indev_get_point(pointer,&ime->gesture_start); }
    else if (code==LV_EVENT_LONG_PRESSED && !*composition(ime) && !ime->queue_count && !ime->awaiting) {
        ime->held=true;
        /* Capture the toolbar, even outside its bounds, until release. */
        lv_obj_set_press_lock(ime->toolbar,true);
        lv_buttonmatrix_set_button_ctrl(ime->toolbar,space_button(ime),LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    else if (code==LV_EVENT_PRESSING && ime->held) {
        lv_point_t point; lv_indev_get_point(pointer,&point);
        int step=px(ime,12); if (step<8) step=8;
        if (step>ime->font.line_height) step=ime->font.line_height;
        int delta=(point.x-ime->gesture_start.x)/step;
        if (delta) {
            int position=(int)lv_textarea_get_cursor_pos(ime->target)+delta;
            if (position<0) position=0;
            lv_textarea_set_cursor_pos(ime->target,position);
            ime->gesture_start.x+=delta*step; ime->multi_key=0;
            refresh_editor(ime,true);
        }
        /* Keep the space key captured while crossing adjacent key cells. */
        lv_event_stop_processing(event);
        return true;
    }
    return false;
}
static void keys_event(lv_event_t *event) {
    pxa_lvgl_ime_t *ime=lv_event_get_user_data(event);
    if (sync_sensitive_mode(ime)) return;
    lv_obj_t *object=lv_event_get_current_target(event);
    const char *key=lv_buttonmatrix_get_button_text(object,lv_buttonmatrix_get_selected_button(object));
    if (!key || !ime->target || ime->pending_switch || ime->waiting_release) return;
    lv_event_code_t code=lv_event_get_code(event);
    if (code==LV_EVENT_PRESSED) ime->held=false;
    bool group=ime->panel==PANEL_LETTERS && ime->layout==PXA_IME_K9 && key[0]>='2' && key[0]<='9' && key[1]==' ';
    if (code==LV_EVENT_LONG_PRESSED && group) {
        ime->held=true; char digit[2]={key[0],0}; edit_key(ime,digit,true); return;
    }
    if (code!=LV_EVENT_VALUE_CHANGED) return;
    if (ime->held) { ime->held=false; return; }
    if (!strcmp(key,"标点")) { ime->panel=PANEL_SYMBOLS; ime->symbol_page=0; ime->expanded=false; layout(ime,false); return; }
    if (!strcmp(key,"分词")) {
        if (*composition(ime) || ime->queue_count || ime->awaiting) (void)send_action(ime,PXA_IME_TEXT,0,"'");
        return;
    }
    if (!strcmp(key,LV_SYMBOL_UP)) {
        bool double_tap=ime->uppercase && ime->shift_since && lv_tick_elaps(ime->shift_since)<400;
        if (ime->caps_lock) { ime->caps_lock=false; ime->uppercase=false; }
        else if (double_tap) ime->caps_lock=true;
        else { ime->uppercase=!ime->uppercase; ime->caps_lock=false; }
        ime->shift_since=lv_tick_get(); ime->multi_key=0; rebuild_keys(ime); return;
    }
    if (!strcmp(key,"下一页") || !strcmp(key,"上一页")) { ime->symbol_page^=1; rebuild_keys(ime); return; }
    if (ime->panel==PANEL_TOOLS) {
        if (!strcmp(key,LV_SYMBOL_LEFT)) request_switch(ime,MOVE_LEFT);
        else if (!strcmp(key,LV_SYMBOL_RIGHT)) request_switch(ime,MOVE_RIGHT);
        else if (!strcmp(key,"返回")) { ime->panel=PANEL_LETTERS; layout(ime,false); }
        else if ((!strcmp(key,"九键") && ime->layout!=PXA_IME_K9) ||
                 (!strcmp(key,"全键") && ime->layout!=PXA_IME_K26)) request_switch(ime,SWITCH_LAYOUT);
        else if (ime->local && pxa_lvgl_ime_builtin_scripts()==3 &&
                 ((!strcmp(key,"简体") && ime->traditional) ||
                  (!strcmp(key,"繁體") && !ime->traditional))) request_switch(ime,SWITCH_SCRIPT);
        else if ((!strcmp(key,"中文") && ime->language) ||
                 (!strcmp(key,"English") && !ime->language)) request_switch(ime,SWITCH_LANGUAGE);
        else { ime->panel=PANEL_LETTERS; layout(ime,false); }
        return;
    }
    if (group) {
        if (ime->language) latin_multi(ime,key);
        else { char digit[2]={key[0],0}; edit_key(ime,digit,false); }
    } else {
        edit_key(ime,key,ime->panel!=PANEL_LETTERS);
        if (ime->language && ime->uppercase && !ime->caps_lock && strlen(key)==1 && key[0]>='A' && key[0]<='Z') {
            ime->uppercase=false; rebuild_keys(ime);
        }
    }
}
static void rebuild_keys(pxa_lvgl_ime_t *ime) {
    const ime_keyset_t *set=keyset(ime);
    unsigned narrow=ime->config.environment.width<ime->config.environment.height?
        ime->config.environment.width:ime->config.environment.height;
    unsigned used=ime->contour_keys?set->rows:1;
    for (unsigned row=1;row<used;++row)
        if (!key_row(ime,row)) ime->key_rows[row-1]=matrix(ime->root,keys_event,ime);
    for (unsigned row=used;row<4;++row) {
        if (!key_row(ime,row)) continue;
        lv_obj_delete(key_row(ime,row)); ime->key_rows[row-1]=NULL;
    }
    for (unsigned row=0;row<used;++row) {
        lv_obj_t *object=key_row(ime,row); if (!object) continue;
        const char *const *map=ime->contour_keys?set->row[row]:set->map;
        /* Only the candidate map changes for an ordinary decoder reply.
         * Reinstalling immutable key maps resets controls, recomputes every
         * cell and can disturb a held key. No extra per-instance cache. */
        if (lv_buttonmatrix_get_map(object)==map) continue;
        lv_buttonmatrix_set_map(object,map); map_controls(object);
        for (uint32_t i=0;lv_buttonmatrix_get_button_text(object,i);++i) {
            const char *label=lv_buttonmatrix_get_button_text(object,i);
            /* Two CJK characters need more width than a Latin letter. This
             * also gives Shift/Enter the familiar wider QWERTY hit targets. */
            if (ime->panel==PANEL_LETTERS && ime->layout==PXA_IME_K26 &&
                (!strcmp(label,"分词") || !strcmp(label,LV_SYMBOL_UP) ||
                 !strcmp(label,"完成") || !strcmp(label,LV_SYMBOL_NEW_LINE)))
                lv_buttonmatrix_set_button_width(object,i,narrow<=180?4:2);
            if (ime->panel==PANEL_SYMBOLS) {
                if (!strcmp(label,"下一页") || !strcmp(label,"上一页"))
                    lv_buttonmatrix_set_button_width(object,i,3);
                else if (!strcmp(label,"完成") || !strcmp(label,LV_SYMBOL_NEW_LINE))
                    lv_buttonmatrix_set_button_width(object,i,2);
            }
            if (!strcmp(label,LV_SYMBOL_BACKSPACE) || (ime->panel==PANEL_TOOLS &&
                (!strcmp(label,LV_SYMBOL_LEFT) || !strcmp(label,LV_SYMBOL_RIGHT)))) {
                /* Editing controls respond on press and repeat while held,
                 * never once more on release. Letters stay release-only. */
                lv_buttonmatrix_clear_button_ctrl(object,i,
                    LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
                if (ime->panel==PANEL_LETTERS && ime->layout==PXA_IME_K26)
                    lv_buttonmatrix_set_button_width(object,i,2);
            }
            if (!strcmp(label,LV_SYMBOL_UP)) {
                if (ime->uppercase) lv_buttonmatrix_set_button_ctrl(object,i,LV_BUTTONMATRIX_CTRL_CHECKED);
            }
            if (ime->panel==PANEL_TOOLS) {
                bool active=(!strcmp(label,"九键") && ime->layout==PXA_IME_K9) ||
                    (!strcmp(label,"全键") && ime->layout==PXA_IME_K26) ||
                    (!strcmp(label,"中文") && !ime->language) ||
                    (!strcmp(label,"English") && ime->language) ||
                    (!strcmp(label,"简体") && !ime->traditional) ||
                    (!strcmp(label,"繁體") && ime->traditional);
                if (active) lv_buttonmatrix_set_button_ctrl(object,i,LV_BUTTONMATRIX_CTRL_CHECKED);
                if (ime->sensitive && (!strcmp(label,"中文") || !strcmp(label,"九键")))
                    lv_buttonmatrix_set_button_ctrl(object,i,LV_BUTTONMATRIX_CTRL_DISABLED);
            }
        }
    }
    const char *const *toolbar=ime->language?
        (ime->panel==PANEL_NUMBERS?toolbar_numbers_en:ime->panel==PANEL_SYMBOLS?toolbar_symbols_en:toolbar_letters_en):
        (ime->panel==PANEL_NUMBERS?toolbar_numbers_cn:ime->panel==PANEL_SYMBOLS?toolbar_symbols_cn:toolbar_letters_cn);
    if (ime->arc_toolbar) toolbar=ime->language?
        (ime->panel==PANEL_NUMBERS?arc_numbers_en:ime->panel==PANEL_SYMBOLS?arc_symbols_en:arc_letters_en):
        (ime->panel==PANEL_NUMBERS?arc_numbers_cn:ime->panel==PANEL_SYMBOLS?arc_symbols_cn:arc_letters_cn);
    if (lv_buttonmatrix_get_map(ime->toolbar)!=toolbar) {
        lv_buttonmatrix_set_map(ime->toolbar,toolbar); map_controls(ime->toolbar);
        ime->footer_weights=0;
        lv_buttonmatrix_set_button_width(ime->toolbar,space_button(ime),ime->arc_toolbar?6:3);
        if (ime->arc_toolbar) {
            lv_buttonmatrix_set_button_ctrl(ime->toolbar,3,LV_BUTTONMATRIX_CTRL_HIDDEN);
            lv_buttonmatrix_set_button_ctrl(ime->toolbar,6,LV_BUTTONMATRIX_CTRL_HIDDEN);
            lv_buttonmatrix_set_button_width(ime->toolbar,5,2);
        }
    }
    if (lv_buttonmatrix_has_button_ctrl(ime->toolbar,2,LV_BUTTONMATRIX_CTRL_DISABLED)!=ime->sensitive) {
        if (ime->sensitive) lv_buttonmatrix_set_button_ctrl(ime->toolbar,2,LV_BUTTONMATRIX_CTRL_DISABLED);
        else lv_buttonmatrix_clear_button_ctrl(ime->toolbar,2,LV_BUTTONMATRIX_CTRL_DISABLED);
    }
}
static void toolbar_event(lv_event_t *event) {
    pxa_lvgl_ime_t *ime=lv_event_get_user_data(event);
    if (sync_sensitive_mode(ime)) return;
    lv_event_code_t code=lv_event_get_code(event);
    if ((code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) && ime->held) {
        ime->held=false;
        lv_obj_set_press_lock(ime->toolbar,false);
        lv_buttonmatrix_clear_button_ctrl(ime->toolbar,space_button(ime),LV_BUTTONMATRIX_CTRL_CHECKED);
        lv_buttonmatrix_set_selected_button(ime->toolbar,LV_BUTTONMATRIX_BUTTON_NONE);
        lv_event_stop_processing(event);
        return;
    }
    if (code==LV_EVENT_PRESSED) {
        ime->held=false;
        lv_indev_t *pointer=lv_event_get_indev(event);
        if (pointer) lv_indev_get_point(pointer,&ime->gesture_start);
    }
    lv_obj_t *object=lv_event_get_current_target(event);
    const char *key=lv_buttonmatrix_get_button_text(object,lv_buttonmatrix_get_selected_button(object));
    if (!key || !ime->target) return;
    if (space_gesture(ime,event,key)) return;
    if (lv_event_get_code(event)!=LV_EVENT_VALUE_CHANGED) return;
    if (!strcmp(key,LV_SYMBOL_DOWN)) { request_switch(ime,SWITCH_CLOSE); return; }
    if (ime->pending_switch || ime->waiting_release) return;
    if (ime->held) { ime->held=false; return; }
    ime->multi_key=0; ime->expanded=false;
    if (!strcmp(key,"空格")) { edit_key(ime,key,false); return; }
    if (!strcmp(key,"中") || !strcmp(key,"EN")) { request_switch(ime,SWITCH_LANGUAGE); return; }
    if (!strcmp(key,"返回")) ime->panel=PANEL_LETTERS;
    else if (!strcmp(key,"123")) ime->panel=PANEL_NUMBERS;
    else if (!strcmp(key,"符")) { ime->panel=PANEL_SYMBOLS; ime->symbol_page=0; }
    layout(ime,false);
}
static void candidate_event(lv_event_t *event) {
    pxa_lvgl_ime_t *ime=lv_event_get_user_data(event);
    if (sync_sensitive_mode(ime)) return;
    if (lv_event_get_code(event)!=LV_EVENT_VALUE_CHANGED) { header_event(event); return; }
    if (ime->gesture_swiped) return;
    const char *key=lv_buttonmatrix_get_button_text(ime->candidates,lv_buttonmatrix_get_selected_button(ime->candidates));
    if (!key) return;
    if (!strcmp(key,LV_SYMBOL_SETTINGS)) {
        ime->expanded=false; ime->panel=ime->panel==PANEL_TOOLS?PANEL_LETTERS:PANEL_TOOLS; layout(ime,false); return;
    }
    if (busy(ime)) return;
    const char *text=composition(ime)+strlen(composition(ime))+1;
    for (unsigned i=0;i<ime->candidate_count;++i) {
        if (key==text) { (void)send_action(ime,PXA_IME_CHOOSE,ime->page+i,NULL); return; }
        text+=strlen(text)+1;
    }
    if (!strcmp(key,LV_SYMBOL_UP) || !strcmp(key,"键盘")) {
        ime->expanded=!ime->expanded; ime->panel=PANEL_LETTERS; layout(ime,false); return;
    }
    if (!strcmp(key,"<") || !strcmp(key,">") || !strcmp(key,"首页")) { turn_word_page(ime,!strcmp(key,">")); return; }
    for (unsigned i=0;i<ime->spelling_count;++i) {
        if (!strcmp(key,text)) { (void)send_action(ime,PXA_IME_CHOOSE_PINYIN,i,NULL); return; }
        text+=strlen(text)+1;
    }
}
static void target_event(lv_event_t *event) {
    pxa_lvgl_ime_t *ime = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) { ime->target = NULL; pxa_lvgl_ime_close(ime); return; }
    if (!ime->target) return;
    if (sync_sensitive_mode(ime)) return;
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_VALUE_CHANGED ||
        code == LV_EVENT_SCROLL || code == LV_EVENT_SIZE_CHANGED || code == LV_EVENT_STYLE_CHANGED)
        refresh_editor(ime,true);
    if (ime->editing) return;
    if (code == LV_EVENT_CLICKED) restart_session(ime);
    else if (code == LV_EVENT_VALUE_CHANGED) {
        uint64_t now = text_fingerprint(ime->target);
        // Declarative model echoes of an accepted edit are not new sessions.
        if (now != ime->fingerprint) { ime->fingerprint = now; restart_session(ime); }
    }
}
bool pxa_lvgl_ime_owns_textarea(const void *textarea) {
    if (!textarea) return false;
    lv_obj_t *object = (lv_obj_t *)textarea;
    for (uint32_t i = 0; i < lv_obj_get_event_count(object); ++i) {
        lv_event_dsc_t *d = lv_obj_get_event_dsc(object, i);
        if (lv_event_dsc_get_cb(d) == target_event) return true;
    }
    return false;
}
void pxa_lvgl_ime_close(pxa_lvgl_ime_t *ime) {
    if (!ime) return;
    ime->queue_count = 0; ime->awaiting = false; ime->ready = true;
    ime->pending_switch=0; ime->multi_key=0; ime->expanded=false; ime->waiting_release=false;
    ime->held=false;
    lv_obj_set_press_lock(ime->toolbar,false);
    lv_buttonmatrix_clear_button_ctrl(ime->toolbar,space_button(ime),LV_BUTTONMATRIX_CTRL_CHECKED);
    if (ime->target) {
        lv_obj_remove_event_cb_with_user_data(ime->target, target_event, ime);
        lv_obj_remove_state(ime->target, LV_STATE_FOCUSED);
        ime->target = NULL;
    }
    if (ime->scroll_parent) {
        lv_obj_t *parent=ime->scroll_parent; ime->scroll_parent=NULL;
        lv_obj_remove_event_cb_with_user_data(parent,scroll_parent_deleted,ime);
        lv_obj_scroll_to_y(parent,ime->previous_scroll_y,LV_ANIM_OFF);
    }
    pxa_lvgl_ime_builtin_destroy(ime->builtin); ime->builtin=NULL;
    ime->local=!ime->config.input;
    if (ime->root) lv_obj_set_hidden(ime->root, true);
    ime->view_visible=false; if (ime->editor_buffer) memset(ime->editor_buffer,0,257);
    free(ime->previous_page); ime->previous_page=NULL; ime->page_capacity=0;
    if (ime->editor_view) lv_obj_set_hidden(ime->editor_view,true);
    clear_presentation(ime);
    ime->config.focus(ime->config.context, ime->generation, false, false);
}
static lv_obj_t *matrix(lv_obj_t *root, lv_event_cb_t callback, pxa_lvgl_ime_t *ime) {
    lv_obj_t *object = lv_buttonmatrix_create(root);
    if (!object) return NULL;
    /* These private matrices use system tokens below. Do not retain the
     * default theme's button shadows, recoloring or state transitions. */
    lv_obj_remove_style_all(object);
    lv_obj_set_scrollable(object, false); lv_obj_set_click_focusable(object, false);
    lv_obj_add_style(object,&ime->matrix_style,0);
    lv_obj_add_style(object,&ime->item_style,LV_PART_ITEMS);
    lv_obj_add_style(object,&ime->pressed_style,LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_obj_add_style(object,&disabled_style,LV_PART_ITEMS|LV_STATE_DISABLED);
    if (callback) lv_obj_add_event_cb(object, callback, LV_EVENT_ALL, ime);
    lv_buttonmatrix_set_map(object, empty_map);
    return object;
}
static void first_draw(lv_event_t *event) {
    pxa_lvgl_ime_t *ime=lv_event_get_user_data(event);
    if (!ime->target || ime->drawn) return;
    ime->drawn=true; ime->first_draw_ms=lv_tick_elaps(ime->opened_since);
    /* Enqueue only: a draw callback must never enter or load Guest code. */
    ime->config.focus(ime->config.context,ime->generation,true,needs_provider(ime));
}
static const lv_font_t *bitmap_latin_font(unsigned height) {
    const lv_font_t *const fonts[]={
#if LV_FONT_MONTSERRAT_12
        &lv_font_montserrat_12,
#endif
#if LV_FONT_MONTSERRAT_14
        &lv_font_montserrat_14,
#endif
#if LV_FONT_MONTSERRAT_16
        &lv_font_montserrat_16,
#endif
#if LV_FONT_MONTSERRAT_20
        &lv_font_montserrat_20,
#endif
#if LV_FONT_MONTSERRAT_24
        &lv_font_montserrat_24,
#endif
#if LV_FONT_MONTSERRAT_28
        &lv_font_montserrat_28,
#endif
        LV_FONT_DEFAULT
    };
    const lv_font_t *best=NULL; unsigned distance=UINT32_MAX;
    for (unsigned i=0;i<sizeof(fonts)/sizeof(fonts[0]);++i) {
        unsigned line=fonts[i]->line_height;
        if (line*4<height*3 || line>height+2) continue;
        unsigned delta=line>height?line-height:height-line;
        if (delta<distance) { best=fonts[i]; distance=delta; }
    }
    return best;
}
void pxa_lvgl_ime_set_theme(pxa_lvgl_ime_t *ime, const pxa_lvgl_ui_theme_t *theme) {
    if (!ime || !theme) return;
    /* Font and environment changes can alter size-dependent key weights.
     * Ordinary decoder replies reuse the maps; a theme refresh rebuilds them. */
    for (unsigned row=0;row<4;++row)
        if (key_row(ime,row)) lv_buttonmatrix_set_map(key_row(ime,row),empty_map);
    ime->config.theme = *theme;
    const lv_font_t *font = theme->body_font ? theme->body_font : LV_FONT_DEFAULT;
    const lv_font_t *alternatives[] = {theme->body_font, theme->label_font, theme->caption_font};
    unsigned extent=ime->config.environment.height;
    if (ime->config.environment.display_shape==2 && extent>ime->config.environment.width) extent=ime->config.environment.width;
    unsigned available=extent*3/5;
    unsigned limit=available>30?(available-30)/6:10;
    for (unsigned i = 0; i < 3; ++i) {
        if (!alternatives[i]) continue;
        font = alternatives[i];
        if ((unsigned)font->line_height <= limit) break;
    }
#if LV_USE_FREETYPE
    if ((unsigned)font->line_height>limit && ime->config.sized_text_font_path) {
        /* A physical space conflict must not turn Chinese labels into tiny
         * 8px glyphs. Prefer a taller compact keyboard before going below 12px. */
        unsigned size=limit*4/5; if (size<12) size=12;
        if (ime->sized_font && ime->sized_font_limit!=limit) {
            lv_freetype_font_delete(ime->sized_font); ime->sized_font=NULL;
        }
        if (!ime->sized_font) {
            ime->sized_font=lv_freetype_font_create(ime->config.sized_text_font_path,
                LV_FREETYPE_FONT_RENDER_MODE_BITMAP,size,LV_FREETYPE_FONT_STYLE_NORMAL);
            ime->sized_font_px=size; ime->sized_font_limit=limit;
        }
        /* A reused face may already have reached the minimum size. Start
         * from its actual size so theme updates do not recreate it. */
        size=ime->sized_font_px;
        while (ime->sized_font && (unsigned)ime->sized_font->line_height>limit && size>12) {
            lv_freetype_font_delete(ime->sized_font); --size;
            ime->sized_font=lv_freetype_font_create(ime->config.sized_text_font_path,
                LV_FREETYPE_FONT_RENDER_MODE_BITMAP,size,LV_FREETYPE_FONT_STYLE_NORMAL);
            ime->sized_font_px=size;
        }
        if (ime->sized_font) font=ime->sized_font;
    }
#endif
    ime->font = *font; ime->font.fallback = LV_FONT_DEFAULT;
    /* CJK faces include generous ascent/descent leading. A private compact
     * face keeps its glyphs and trims only that leading, not the glyph bitmap. */
    if (font==ime->sized_font && ime->font.line_height>(int)ime->sized_font_px+2) {
        int leading=ime->font.line_height-(int)ime->sized_font_px-2;
        ime->font.line_height=ime->sized_font_px+2;
        ime->font.base_line=ime->font.base_line>leading/2?ime->font.base_line-leading/2:0;
    }
    ime->cjk_font=ime->font;
    /* Number+letter groups must fit four columns. Preserve CJK size and
     * reuse compiled Latin faces; very narrow screens use a ROM-only face. */
    unsigned latin_height=ime->font.line_height;
    unsigned latin_limit=ime->config.environment.width>32?(ime->config.environment.width-32)/16:0;
    if (latin_limit<12) latin_limit=12;
    if (latin_height>latin_limit) latin_height=latin_limit;
    const lv_font_t *latin=ime->config.environment.width<200?&ime_latin_12:bitmap_latin_font(latin_height);
    if (!latin && ime->config.environment.width<360) latin=&ime_latin_12;
    if (latin) {
        /* Reuse compiled antialiased Latin glyphs, with the exact CJK face as
         * fallback. No extra face, atlas or FreeType cache for typing letters. */
        ime->font=*latin;
        ime->font.line_height=ime->cjk_font.line_height;
        ime->font.base_line=ime->cjk_font.base_line;
        ime->font.fallback=&ime->cjk_font;
    }
    /* Product caption faces can already be 12 px: requiring a newly created
     * face would miss that common path. Match the actual CJK advance instead. */
    lv_font_glyph_dsc_t label_metrics;
    bool small_labels=ime->font.line_height<=20 &&
        lv_font_get_glyph_dsc(&ime->cjk_font,&label_metrics,0x4e2d,0);
    if (small_labels) {
        small_labels=!label_metrics.is_placeholder && label_metrics.adv_w==12;
        lv_font_glyph_release_draw_data(&label_metrics);
    }
    if (small_labels) {
        ime->label_font=ime_label_12;
        ime->label_font.line_height=ime->font.line_height;
        ime->label_font.base_line=ime->font.base_line;
        ime->label_font.fallback=&ime->cjk_font;
        if (latin) ime->font.fallback=&ime->label_font;
        else ime->font=ime->label_font;
    }
    lv_obj_set_style_text_font(ime->root, &ime->font, 0);
    lv_obj_set_style_text_color(ime->root, lv_color_hex(theme->rgba[PXA_UI_THEME_TEXT] >> 8), 0);
    lv_obj_set_style_bg_color(ime->root, lv_color_hex(theme->rgba[PXA_UI_THEME_SURFACE_CONTAINER] >> 8), 0);
    lv_obj_set_style_border_color(ime->root,lv_color_hex(theme->rgba[PXA_UI_THEME_OUTLINE_VARIANT]>>8),0);
    /* Only theme/environment updates change these panel properties. Setting
     * them during each candidate layout invalidates the whole keyboard. */
    lv_obj_set_style_pad_all(ime->root,ime->config.environment.display_shape==2?2:4,0);
    lv_obj_set_style_border_width(ime->root,ime->config.environment.display_shape?0:1,0);
    lv_obj_set_style_bg_opa(ime->root,LV_OPA_COVER,0);
    if (ime->preedit) {
        lv_obj_set_style_bg_color(ime->preedit,lv_color_hex(theme->rgba[PXA_UI_THEME_SURFACE_CONTAINER]>>8),0);
        lv_obj_set_style_bg_opa(ime->preedit,LV_OPA_COVER,0);
        lv_obj_set_style_radius(ime->preedit,2,0);
    }
    if (ime->editor_view) {
        lv_obj_set_style_bg_color(ime->editor_view,lv_color_hex(theme->rgba[PXA_UI_THEME_SURFACE_CONTAINER_HIGHEST]>>8),0);
        lv_obj_set_style_border_color(ime->editor_view,lv_color_hex(theme->rgba[PXA_UI_THEME_OUTLINE_VARIANT]>>8),0);
        lv_obj_set_style_text_color(ime->editor_text,lv_color_hex(theme->rgba[PXA_UI_THEME_TEXT]>>8),0);
        lv_obj_set_style_bg_color(ime->editor_caret,lv_color_hex(theme->rgba[PXA_UI_THEME_TEXT]>>8),0);
    }
    lv_style_set_bg_color(&ime->item_style,lv_color_hex(theme->rgba[PXA_UI_THEME_SURFACE_CONTAINER_HIGHEST]>>8));
    lv_style_set_text_color(&ime->item_style,lv_color_hex(theme->rgba[PXA_UI_THEME_TEXT]>>8));
    lv_style_set_border_color(&ime->item_style,lv_color_hex(theme->rgba[PXA_UI_THEME_OUTLINE_VARIANT]>>8));
    lv_style_set_bg_color(&ime->pressed_style,lv_color_hex(theme->rgba[PXA_UI_THEME_PRIMARY_CONTAINER]>>8));
    lv_style_set_text_color(&ime->pressed_style,lv_color_hex(theme->rgba[PXA_UI_THEME_ON_PRIMARY_CONTAINER]>>8));
    lv_obj_report_style_change(&ime->item_style); lv_obj_report_style_change(&ime->pressed_style);
    if (ime->target) layout(ime,false);
}
void pxa_lvgl_ime_set_preferences(pxa_lvgl_ime_t *ime,pxa_ime_preferences_t preferences) {
    if (ime && pxa_ime_preferences_valid(preferences)) ime->config.preferences=preferences;
}
void pxa_lvgl_ime_set_environment(pxa_lvgl_ime_t *ime,const pxa_ui_environment_t *environment) {
    if (!ime || !environment) return;
    ime->config.environment=*environment;
    pxa_lvgl_ime_set_theme(ime,&ime->config.theme);
}
pxa_lvgl_ime_t *pxa_lvgl_ime_create(const pxa_lvgl_ime_config_t *config) {
    if (!config || config->struct_size < sizeof(*config) || !config->focus || !pxa_ime_preferences_valid(config->preferences)) return NULL;
    pxa_lvgl_ime_t *ime = calloc(1, sizeof(*ime)); if (!ime) return NULL;
    ime->config = *config;
    ime->local=!config->input;
    lv_style_init(&ime->matrix_style); lv_style_init(&ime->item_style); lv_style_init(&ime->pressed_style);
    lv_style_set_border_width(&ime->matrix_style,0);
    lv_style_set_pad_all(&ime->matrix_style,2);
    lv_style_set_pad_row(&ime->matrix_style,3); lv_style_set_pad_column(&ime->matrix_style,3);
    lv_style_set_bg_opa(&ime->matrix_style,LV_OPA_TRANSP);
    lv_style_set_radius(&ime->item_style,4);
    lv_style_set_bg_opa(&ime->item_style,LV_OPA_COVER);
    lv_style_set_bg_opa(&ime->pressed_style,LV_OPA_COVER);
    ime->root = lv_obj_create(config->parent ? config->parent : lv_layer_top());
    if (!ime->root) goto failed;
    lv_obj_set_floating(ime->root, true); lv_obj_set_hidden(ime->root, true);
    lv_obj_set_scrollable(ime->root, false); lv_obj_set_click_focusable(ime->root, false);
    lv_obj_set_style_pad_all(ime->root, 4, 0); lv_obj_set_style_border_width(ime->root, 1, 0);
    lv_obj_add_event_cb(ime->root,first_draw,LV_EVENT_DRAW_POST_END,ime);
    ime->candidates = matrix(ime->root, NULL, ime);
    if (ime->candidates) lv_obj_add_event_cb(ime->candidates,candidate_event,LV_EVENT_ALL,ime);
    /* Candidate rows are text rails, distinct from the boxed typing keys.
     * Selected and pressed items share the same accessible highlight. */
    if (ime->candidates) {
        lv_obj_add_style(ime->candidates,&rail_style,LV_PART_ITEMS);
        lv_obj_add_style(ime->candidates,&rail_style,LV_PART_ITEMS|LV_STATE_DISABLED);
        lv_obj_add_style(ime->candidates,&ime->pressed_style,LV_PART_ITEMS|LV_STATE_CHECKED);
    }
    ime->keys = matrix(ime->root, keys_event, ime);
    ime->toolbar = matrix(ime->root, NULL, ime);
    if (ime->toolbar) {
        lv_obj_add_style(ime->toolbar,&ime->pressed_style,LV_PART_ITEMS|LV_STATE_CHECKED);
        lv_obj_add_event_cb(ime->toolbar,toolbar_event,LV_EVENT_ALL|LV_EVENT_PREPROCESS,ime);
    }
    if (!ime->candidates || !ime->keys || !ime->toolbar) goto failed;
    ime->retry = lv_timer_create(retry_input, 8, ime);
    if (!ime->retry) goto failed;
    pxa_lvgl_ime_set_theme(ime, &config->theme); clear_presentation(ime);
    return ime;
failed:
    if (ime->root) lv_obj_delete(ime->root);
    lv_style_reset(&ime->matrix_style); lv_style_reset(&ime->item_style); lv_style_reset(&ime->pressed_style);
    free(ime); return NULL;
}
void pxa_lvgl_ime_destroy(pxa_lvgl_ime_t *ime) {
    if (!ime) return;
    pxa_lvgl_ime_close(ime); lv_timer_delete(ime->retry); lv_obj_delete(ime->root);
    lv_style_reset(&ime->matrix_style); lv_style_reset(&ime->item_style); lv_style_reset(&ime->pressed_style);
    free(ime->editor_buffer);
#if LV_USE_FREETYPE
    if (ime->sized_font) lv_freetype_font_delete(ime->sized_font);
#endif
    free(ime);
}
/* A rectangular band must fit at both ends, including asymmetric corners.
 * Widths are computed from physical geometry rather than radius/3 heuristics. */
static int corner_inset(int radius, int distance) {
    if (radius <= 0 || distance >= radius) return 0;
    double d = radius - (distance < 0 ? 0 : distance);
    return (int)ceil(radius - sqrt(fmax(0.0, (double)radius*radius-d*d)));
}
static void band_bounds(const pxa_ui_environment_t *e, int top, int bottom,
    int margin, int *left, int *right) {
    int w=(int)e->width, h=(int)e->height;
    *left=(int)e->safe_insets[3]+margin; *right=w-(int)e->safe_insets[1]-margin;
    if (e->display_shape == 2) {
        double r=(w<h?w:h)*0.5-margin, cy=(h-1)*0.5, cx=(w-1)*0.5;
        double d=fmax(fabs(top-cy),fabs(bottom-cy));
        double half=sqrt(fmax(0.0,r*r-d*d));
        int l=(int)ceil(cx-half), rr=(int)floor(cx+half)+1;
        if (*left<l) *left=l;
        if (*right>rr) *right=rr;
    } else if (e->display_shape == 1) {
        int radii[4];
        for (unsigned i=0;i<4;++i) {
            radii[i]=(int)e->corner_radii[i];
            if (radii[i]>w/2) radii[i]=w/2;
            if (radii[i]>h/2) radii[i]=h/2;
        }
        int l=corner_inset(radii[0],top), b=corner_inset(radii[3],h-1-bottom);
        int rr=corner_inset(radii[1],top), br=corner_inset(radii[2],h-1-bottom);
        if (l<b) l=b;
        if (rr<br) rr=br;
        if (*left<l+margin) *left=l+margin;
        if (*right>w-rr-margin) *right=w-rr-margin;
    }
}
static void section_bounds(pxa_lvgl_ime_t *ime,int y,int height,int *x,int *width) {
    lv_area_t root; lv_obj_get_coords(ime->root,&root);
    int padding=lv_obj_get_style_pad_left(ime->root,0)+lv_obj_get_style_border_width(ime->root,0);
    int left,right;
    band_bounds(&ime->config.environment,root.y1+padding+y,root.y1+padding+y+height-1,
        ime->config.environment.display_shape==2?4:0,&left,&right);
    int origin=root.x1+padding, end=root.x2-padding+1;
    if (left<origin) left=origin;
    if (right>end) right=end;
    *x=left-origin; *width=right-left;
}
static void layout(pxa_lvgl_ime_t *ime, bool choose_layout) {
    const pxa_ui_environment_t *e=&ime->config.environment;
    int w=(int)e->width, h=(int)e->height;
    if (w<=0 || h<=0) return;
    if (!ime->candidate_count) ime->expanded=false;
    bool circle=e->display_shape==2, contoured=e->display_shape!=0;
    if (choose_layout) ime->layout=(uint64_t)w*65536 <
        (uint64_t)360*(e->density_q16?e->density_q16:65536)?PXA_IME_K9:PXA_IME_K26;
    if (choose_layout && ime->config.preferences.layout)
        ime->layout=ime->config.preferences.layout==PXA_IME_LAYOUT_NINE?PXA_IME_K9:PXA_IME_K26;
    if (choose_layout) {
        ime->chinese_layout=ime->layout;
        if (ime->language==PXA_IME_ENGLISH) ime->layout=PXA_IME_K26;
    }
    if (ime->sensitive) ime->layout=PXA_IME_K26;
    int margin=px(ime,4); if (margin>8) margin=8;
    int bottom=(int)e->safe_insets[2]+margin;
    int extent=circle && w<h?w:h;
    if (circle) {
        int circle_bottom=(h-1)/2+extent*3/8;
        if (bottom<h-circle_bottom) bottom=h-circle_bottom;
    }
    int top=(int)e->safe_insets[0]+margin;
    int line=ime->font.line_height, row=px(ime,44), padding=circle?6:10;
    if (row<line+4) row=line+4;
    int header_line=line+(circle?4:8);
    int touch_header=px(ime,36);
    if (touch_header>extent/10) touch_header=extent/10;
    if (header_line<touch_header) header_line=touch_header;
    int maximum_header=header_line*2;
    bool composing=*composition(ime) || ime->candidate_count;
    int header=(!ime->language && composing)?maximum_header:header_line;
    int footer=px(ime,40), view=ime->view_visible?line+(circle?6:12):0;
    if (footer>extent/9) footer=extent/9;
    if (footer<line+(circle?4:10)) footer=line+(circle?4:10);
    unsigned rows=keyset(ime)->rows;
    int height=maximum_header+footer+view+row*(int)rows+padding;
    int maximum=extent*3/5;
    int vertical_available=circle?extent-2*margin:h-top-bottom;
    if (maximum>vertical_available-line-8) maximum=vertical_available-line-8;
    if (height>maximum) height=maximum;
    int minimum=maximum_header+footer+view+(line+4)*(int)rows+padding;
    if (height<minimum) height=minimum;
    if (height>vertical_available) height=vertical_available;
    int y=h-bottom-height;
    if (circle && height>extent*11/20) y=(h-height)/2;
    /* Anchor against the maximum composition height on circular displays too.
     * Adding the word rail must not move the keys under the user's fingers. */
    y+=maximum_header-header;
    height-=maximum_header-header;
    /* Extend into the lower arc without moving or compressing the main keys.
     * Keep a single footer when the extra row cannot fit comfortably. */
    ime->arc_toolbar=false;
    if (circle &&
        y+height+footer<=((h+extent)/2-(int)e->safe_insets[2]-margin)) {
        int arc_left,arc_right;
        band_bounds(e,y+height-padding,y+height+footer,4,&arc_left,&arc_right);
        if (arc_right-arc_left>=text_width(ime,"空格")+text_width(ime,LV_SYMBOL_DOWN)+16) {
            ime->arc_toolbar=true; height+=footer; footer*=2;
        }
    }
    int left,right;
    if (contoured) { left=(int)e->safe_insets[3]+margin; right=w-(int)e->safe_insets[1]-margin; }
    else band_bounds(e,y,y+height-1,margin,&left,&right);
    int width=right-left, limit=px(ime,640);
    if (width>limit) { left+=(width-limit)/2; width=limit; }
    if (width<24 || height<header+footer+view+padding) return;
    /* The panel covers text behind inter-key gaps. Physical display masking
     * clips its background; each interactive row still fits the safe contour. */
    lv_obj_set_size(ime->root,width,height); lv_obj_set_pos(ime->root,left,y);
    lv_obj_update_layout(ime->root);
    /* Environment bounds are display coordinates; parent padding and scroll
     * offsets must not shift the keyboard out of the safe display area. */
    lv_area_t actual; lv_obj_get_coords(ime->root,&actual);
    if (actual.x1!=left || actual.y1!=y) {
        lv_obj_set_pos(ime->root,lv_obj_get_x(ime->root)+left-actual.x1,
            lv_obj_get_y(ime->root)+y-actual.y1);
        lv_obj_update_layout(ime->root);
    }
    int key_y=view+header, key_height=height-view-header-footer-padding;
    int key_x,key_width; section_bounds(ime,key_y,key_height,&key_x,&key_width);
    int footer_x,footer_width;
    section_bounds(ime,height-footer-padding,ime->arc_toolbar?footer/2:footer,&footer_x,&footer_width);
    if (ime->editor_view) {
        int x,inner; section_bounds(ime,0,view,&x,&inner);
        lv_obj_set_hidden(ime->editor_view,!ime->view_visible);
        lv_obj_set_pos(ime->editor_view,x,0); lv_obj_set_size(ime->editor_view,inner,view);
        int navigation=line*3;
        if (navigation>inner/2) navigation=inner/2;
        lv_obj_set_pos(ime->editor_navigation,inner-navigation-2,0);
        lv_obj_set_size(ime->editor_navigation,navigation,view-2);
        lv_obj_set_pos(ime->editor_text,4,(view-line)/2);
        lv_obj_set_width(ime->editor_text,inner-navigation-10);
    }
    int header_x,header_width; section_bounds(ime,view,header?header:1,&header_x,&header_width);
    int preedit_w=0;
    if (ime->preedit) {
        lv_obj_set_pos(ime->preedit,header_x,view+2); lv_obj_set_width(ime->preedit,preedit_w);
        lv_obj_set_height(ime->preedit,line+2); lv_label_set_long_mode(ime->preedit,LV_LABEL_LONG_DOT);
    }
    lv_obj_set_pos(ime->candidates,header_x+preedit_w,view);
    if (ime->expanded) {
        int expanded_x,expanded_width;
        section_bounds(ime,view,header+key_height,&expanded_x,&expanded_width);
        lv_obj_set_pos(ime->candidates,expanded_x,view);
        lv_obj_set_size(ime->candidates,expanded_width,header+key_height);
    } else lv_obj_set_size(ime->candidates,header_width-preedit_w,header);
    ime->contour_keys=contoured;
    if (!ime->waiting_release) rebuild_keys(ime);
    for (unsigned row=0;row<4;++row) {
        lv_obj_t *object=key_row(ime,row); if (!object) continue;
        lv_obj_set_hidden(object,ime->expanded);
        int x=key_x,inner=key_width,offset=0,height_row=key_height;
        if (ime->contour_keys) {
            offset=key_height*(int)row/(int)rows;
            height_row=key_height*(int)(row+1)/(int)rows-offset;
            section_bounds(ime,key_y+offset,height_row,&x,&inner);
        }
        lv_obj_set_pos(object,x,key_y+offset); lv_obj_set_size(object,inner,height_row);
    }
    lv_obj_set_pos(ime->toolbar,footer_x,height-footer-padding);
    lv_obj_set_size(ime->toolbar,footer_width,footer);
    if (ime->arc_toolbar) {
        int lower_x,lower_width;
        section_bounds(ime,height-footer/2-padding,footer/2,&lower_x,&lower_width);
        /* The visible cells have widths 6:2. Round side weights outward,
         * reserving matrix padding/gaps so both hit targets fit the chord. */
        int available=lower_width-16; if (available<1) available=1;
        int l=((lower_x-footer_x+2)*8+available-1)/available;
        int r=((footer_x+footer_width-lower_x-lower_width+2)*8+available-1)/available;
        if (l<1) l=1;
        if (r<1) r=1;
        if (l>15) l=15;
        if (r>15) r=15;
        uint8_t weights=(uint8_t)(l|(r<<4));
        if (weights!=ime->footer_weights) {
            lv_buttonmatrix_set_button_width(ime->toolbar,3,(uint32_t)l);
            lv_buttonmatrix_set_button_width(ime->toolbar,6,(uint32_t)r);
            ime->footer_weights=weights;
        }
    }
    /* All backends share the same rails, keys and contour layout. */
    lv_obj_update_layout(ime->candidates);
    pack_spellings(ime);
    if (ime->preedit) lv_obj_set_hidden(ime->preedit,true);
    refresh_editor(ime,true);
}
static lv_area_t cursor_line(pxa_lvgl_ime_t *ime) {
    lv_obj_t *label=lv_textarea_get_label(ime->target);
    lv_obj_update_layout(ime->target);
    lv_area_t area; lv_obj_get_coords(label,&area);
    lv_point_t position; lv_label_get_letter_pos(label,lv_textarea_get_cursor_pos(ime->target),&position);
    int line=lv_obj_get_style_text_font(label,0)->line_height;
    area.x1+=position.x; area.x2=area.x1+2;
    area.y1+=position.y; area.y2=area.y1+line-1;
    return area;
}
static bool line_visible(pxa_lvgl_ime_t *ime, const lv_area_t *line) {
    const pxa_ui_environment_t *e=&ime->config.environment;
    lv_area_t keyboard; lv_obj_get_coords(ime->root,&keyboard);
    int left,right; band_bounds(e,line->y1,line->y2,2,&left,&right);
    if (line->y1<(int)e->safe_insets[0]+2 || line->y2>=keyboard.y1-2 ||
        line->x1<left || line->x2>=right) return false;
    for (lv_obj_t *parent=ime->target;parent;parent=lv_obj_get_parent(parent)) {
        lv_area_t area; lv_obj_get_coords(parent,&area);
        if (line->y1<area.y1 || line->y2>area.y2 || line->x1<area.x1 || line->x2>area.x2) return false;
    }
    return true;
}
/* UTF-8 offsets are local to a bounded caret window. No complete text copy,
 * second editing model, or per-keystroke growing temporary container. */
static const char *next_character(const char *p) {
    if (!*p) return p;
    do { ++p; } while ((*p&0xc0)==0x80);
    return p;
}
static const char *previous_character(const char *begin,const char *p) {
    if (p==begin) return p;
    do { --p; } while (p>begin && (*p&0xc0)==0x80);
    return p;
}
static void editor_view_event(lv_event_t *event) {
    pxa_lvgl_ime_t *ime=lv_event_get_user_data(event);
    if (!ime->target) return;
    if (lv_event_get_current_target(event)==ime->editor_navigation) {
        if (lv_event_get_code(event)!=LV_EVENT_VALUE_CHANGED) return;
        request_switch(ime,lv_buttonmatrix_get_selected_button(ime->editor_navigation)==0?MOVE_LEFT:MOVE_RIGHT);
        return;
    } else {
        lv_indev_t *pointer=lv_indev_active(); if (!pointer) return;
        lv_point_t point; lv_indev_get_point(pointer,&point);
        lv_area_t box; lv_obj_get_coords(ime->editor_text,&box);
        point.x-=box.x1; point.y=ime->font.line_height/2;
        uint32_t index=lv_label_get_letter_on(ime->editor_text,&point,true);
        if (ime->editor_prefix && index) --index;
        uint32_t position=ime->editor_start+index;
        if (position>ime->editor_end) position=ime->editor_end;
        lv_textarea_set_cursor_pos(ime->target,(int32_t)position);
    }
    restart_session(ime); refresh_editor(ime,false);
}
static bool create_editor_view(pxa_lvgl_ime_t *ime) {
    if (ime->editor_view) return true;
    ime->editor_view=lv_obj_create(ime->root);
    if (!ime->editor_view) return false;
    lv_obj_set_style_pad_all(ime->editor_view,0,0);
    lv_obj_set_style_radius(ime->editor_view,4,0);
    lv_obj_set_style_border_width(ime->editor_view,1,0);
    lv_obj_set_style_border_color(ime->editor_view,
        lv_color_hex(ime->config.theme.rgba[PXA_UI_THEME_OUTLINE_VARIANT]>>8),0);
    lv_obj_set_style_bg_color(ime->editor_view,
        lv_color_hex(ime->config.theme.rgba[PXA_UI_THEME_SURFACE_CONTAINER_HIGHEST]>>8),0);
    lv_obj_set_style_text_color(ime->editor_view,
        lv_color_hex(ime->config.theme.rgba[PXA_UI_THEME_TEXT]>>8),0);
    lv_obj_set_scrollable(ime->editor_view,false); lv_obj_set_click_focusable(ime->editor_view,false);
    ime->editor_text=lv_label_create(ime->editor_view);
    ime->editor_caret=lv_obj_create(ime->editor_view);
    ime->editor_navigation=matrix(ime->editor_view,editor_view_event,ime);
    if (!ime->editor_buffer) ime->editor_buffer=calloc(1,257);
    if (!ime->editor_text || !ime->editor_caret || !ime->editor_navigation || !ime->editor_buffer) {
        lv_obj_delete(ime->editor_view); ime->editor_view=ime->editor_text=ime->editor_caret=ime->editor_navigation=NULL;
        return false;
    }
    lv_obj_set_style_text_color(ime->editor_text,
        lv_color_hex(ime->config.theme.rgba[PXA_UI_THEME_TEXT]>>8),0);
    lv_obj_set_clickable(ime->editor_text,true); lv_obj_set_click_focusable(ime->editor_text,false);
    lv_label_set_long_mode(ime->editor_text,LV_LABEL_LONG_CLIP);
    lv_obj_add_event_cb(ime->editor_text,editor_view_event,LV_EVENT_CLICKED,ime);
    lv_obj_set_style_pad_all(ime->editor_caret,0,0);
    lv_obj_set_style_border_width(ime->editor_caret,0,0);
    lv_obj_set_style_bg_color(ime->editor_caret,
        lv_color_hex(ime->config.theme.rgba[PXA_UI_THEME_TEXT]>>8),0);
    lv_obj_set_clickable(ime->editor_caret,false);
    /* Cursor controls are a text rail too: only a held arrow gets a fill. */
    lv_obj_add_style(ime->editor_navigation,&rail_style,LV_PART_ITEMS);
    lv_buttonmatrix_set_map(ime->editor_navigation,navigation_map);
    lv_buttonmatrix_clear_button_ctrl_all(ime->editor_navigation,
        LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    return true;
}
static bool copy_editor_bytes(char **out,const char *text,size_t size) {
    bool changed=memcmp(*out,text,size)!=0;
    memcpy(*out,text,size); *out+=size;
    return changed;
}
static void update_editor_text(pxa_lvgl_ime_t *ime) {
    lv_obj_update_layout(ime->editor_view);
    const char *text=lv_textarea_get_text(ime->target), *caret=text;
    uint32_t cursor=lv_textarea_get_cursor_pos(ime->target), start=cursor, end=cursor;
    for (uint32_t i=0;i<cursor && *caret;++i) caret=next_character(caret);
    uint32_t row_start=0,row_end=UINT32_MAX;
    if (!lv_textarea_get_one_line(ime->target)) {
        lv_obj_t *label=lv_textarea_get_label(ime->target);
        lv_point_t position; lv_label_get_letter_pos(label,cursor,&position);
        lv_point_t point={0,position.y+lv_obj_get_style_text_font(label,0)->line_height/2};
        uint32_t first=lv_label_get_letter_on(label,&point,true);
        point.x=lv_obj_get_width(label);
        uint32_t last=lv_label_get_letter_on(label,&point,true);
        /* Mixed direction rows may return visual endpoints in reverse order. */
        if (first<=cursor && last>=cursor) { row_start=first; row_end=last+1; }
    }
    const char *begin=caret, *finish=caret;
    int available=lv_obj_get_width(ime->editor_text)-8, used=0;
    unsigned count=0;
    while (begin>text && start>row_start && count<32) {
        const char *p=previous_character(text,begin); if (*p=='\n' || *p=='\r') break;
        char glyph[5]={0}; size_t n=(size_t)(begin-p);
        if (ime->sensitive) { glyph[0]='*'; } else memcpy(glyph,p,n);
        int width=text_width(ime,glyph);
        if (used+width>available*2/3) break;
        used+=width; begin=p; --start; ++count;
    }
    bool prefix=begin>text && begin[-1]!='\n' && begin[-1]!='\r';
    if (prefix) used+=text_width(ime,"…");
    while (*finish && end<row_end && *finish!='\n' && *finish!='\r' && count<63) {
        const char *p=next_character(finish); char glyph[5]={0};
        if (ime->sensitive) glyph[0]='*'; else memcpy(glyph,finish,(size_t)(p-finish));
        int width=text_width(ime,glyph);
        if (used+width>available) break;
        used+=width; finish=p; ++end; ++count;
    }
    char *out=ime->editor_buffer;
    bool changed=lv_label_get_text(ime->editor_text)!=ime->editor_buffer;
    if (prefix) changed|=copy_editor_bytes(&out,"…",3);
    for (const char *p=begin;p<finish;) {
        const char *next=next_character(p);
        changed|=copy_editor_bytes(&out,ime->sensitive?"*":p,ime->sensitive?1:(size_t)(next-p));
        p=next;
    }
    changed|=*out!=0;
    *out=0;
    ime->editor_start=start; ime->editor_end=end; ime->editor_cursor=cursor; ime->editor_prefix=prefix;
    /* Reuse the bounded buffer without invalidating an unchanged label.
     * Caret position, font and viewport changes still take effect below. */
    if (changed) lv_label_set_text_static(ime->editor_text,ime->editor_buffer);
    lv_obj_update_layout(ime->editor_view);
    lv_point_t point; lv_label_get_letter_pos(ime->editor_text,cursor-start+(prefix?1:0),&point);
    lv_obj_set_pos(ime->editor_caret,4+point.x,(lv_obj_get_height(ime->editor_view)-ime->font.line_height)/2);
    lv_obj_set_size(ime->editor_caret,1,ime->font.line_height);
}
static void refresh_editor(pxa_lvgl_ime_t *ime, bool reveal) {
    if (!ime->target || ime->refreshing) return;
    ime->refreshing=true;
    lv_obj_update_layout(ime->root);
    lv_area_t line=cursor_line(ime), keyboard, editor;
    lv_obj_get_coords(ime->target,&editor); lv_obj_get_coords(ime->root,&keyboard);
    if (reveal && !line_visible(ime,&line) && !ime->view_visible) {
        int bottom=line.y2;
        if (editor.y2-editor.y1<=(line.y2-line.y1+1)*2) bottom=editor.y2;
        int delta=keyboard.y1-4-bottom;
        bool floating=lv_obj_is_floating(ime->target);
        for (lv_obj_t *parent=lv_obj_get_parent(ime->target);parent;parent=lv_obj_get_parent(parent)) {
            if (!floating && lv_obj_is_scrollable(parent) && (!ime->scroll_parent || ime->scroll_parent==parent)) {
                lv_area_t box; lv_obj_get_coords(parent,&box);
                if (box.y1+ime->font.line_height+4<keyboard.y1 && delta<0) {
                    if (!ime->scroll_parent) {
                        ime->scroll_parent=parent; ime->previous_scroll_y=lv_obj_get_scroll_y(parent);
                        lv_obj_add_event_cb(parent,scroll_parent_deleted,LV_EVENT_DELETE,ime);
                    }
                    lv_obj_scroll_by(parent,0,delta,LV_ANIM_OFF);
                    lv_obj_update_layout(parent); line=cursor_line(ime);
                    if (line_visible(ime,&line)) break;
                }
            }
            floating|=lv_obj_is_floating(parent);
        }
    }
    if (!line_visible(ime,&line) && !ime->view_visible && create_editor_view(ime)) {
        /* Latch the aid until close; toggling it as the editor scrolls would
         * otherwise create a hide/show feedback loop in constrained layouts. */
        ime->view_visible=true; layout(ime,false);
    }
    if (ime->view_visible) update_editor_text(ime);
    ime->editor_cursor=lv_textarea_get_cursor_pos(ime->target);
    lv_obj_get_coords(ime->target,&ime->observed_editor);
    ime->refreshing=false;
}
pxa_status_t pxa_lvgl_ime_focus(pxa_lvgl_ime_t *ime, void *textarea) {
    if (!ime || !textarea || !lv_obj_check_type(textarea,&lv_textarea_class)) return PXA_STATUS_INVALID_ARGUMENT;
    if (ime->target == textarea) { (void)sync_sensitive_mode(ime); return PXA_STATUS_OK; }
    pxa_lvgl_ime_close(ime); ime->target = textarea;
    ime->drawn=false; ime->opened_since=lv_tick_get(); ime->first_draw_ms=ime->ready_ms=0;
    ime->sensitive = lv_textarea_get_password_mode(textarea);
    ime->language = ime->sensitive ? PXA_IME_ENGLISH : ime->config.preferences.language;
    ime->panel = 0; ime->expanded = false; ime->uppercase=ime->caps_lock=false; ime->shift_since=0; ime->fingerprint = text_fingerprint(textarea);
    lv_obj_add_event_cb(textarea,target_event,LV_EVENT_ALL,ime);
    lv_obj_set_hidden(ime->keys, false);
    lv_obj_set_hidden(ime->toolbar, false);
    lv_obj_set_hidden(ime->candidates, false);
    ime->view_visible=false; if (ime->editor_view) lv_obj_set_hidden(ime->editor_view,true);
    layout(ime, true); lv_obj_set_hidden(ime->root, false); lv_obj_move_foreground(ime->root);
    restart_session(ime); return PXA_STATUS_OK;
}
static int text_width(const pxa_lvgl_ime_t *ime, const char *text) {
    lv_point_t size; lv_text_get_size(&size,text,&ime->font,0,0,INT32_MAX,LV_TEXT_FLAG_NONE); return size.x;
}
static void pack_spellings(pxa_lvgl_ime_t *ime) {
    const char *preedit=composition(ime);
    if (!*preedit && !ime->candidate_count) {
        const char *const *map=ime->sensitive?idle_password:ime->language?idle_english:
            !ime->ready?idle_loading:ime->layout==PXA_IME_K9?idle_nine:idle_full;
        if (lv_buttonmatrix_get_map(ime->candidates)!=map) {
            lv_buttonmatrix_set_map(ime->candidates,map); map_controls(ime->candidates);
            lv_buttonmatrix_set_button_ctrl(ime->candidates,1,LV_BUTTONMATRIX_CTRL_DISABLED);
            lv_buttonmatrix_set_button_width(ime->candidates,0,1);
            lv_buttonmatrix_set_button_width(ime->candidates,1,8);
        }
        ime->visible=ime->spelling_visible=0; return;
    }
    const char *words=preedit+strlen(preedit)+1,*spellings=words;
    for (unsigned i=0;i<ime->candidate_count;++i) spellings+=strlen(spellings)+1;
    unsigned used=0;
    int width=lv_obj_get_width(ime->candidates),padding=px(ime,14);
    if (padding>ime->font.line_height) padding=ime->font.line_height;
    ime->candidate_map[used++]=LV_SYMBOL_SETTINGS;
    ime->candidate_map[used++]=*preedit?preedit:"拼音";
    int composition_width=text_width(ime,preedit)+padding;
    if (composition_width>width/3) composition_width=width/3;
    int tool=px(ime,36); if (tool>ime->font.line_height*2) tool=ime->font.line_height*2;
    int available=width-tool-composition_width;
    ime->spelling_visible=0;
    const char *text=spellings;
    for (unsigned i=0;i<ime->spelling_count;++i) {
        int cell=text_width(ime,text)+padding;
        if (i>=ime->spelling_start) {
            if (strcmp(text,preedit)) {
                if (used>2 && cell>available) break;
                ime->candidate_map[used++]=text; available-=cell;
            }
            ++ime->spelling_visible;
        }
        text+=strlen(text)+1;
    }
    unsigned reading_buttons=used;
    ime->candidate_map[used++]="\n";
    ime->visible=0; text=words;
    unsigned columns=ime->expanded?(unsigned)(width/px(ime,80)):0;
    if (columns<2) columns=2;
    if (columns>4) columns=4;
    unsigned grid_rows=ime->expanded?(unsigned)(lv_obj_get_height(ime->candidates)/(ime->font.line_height+6)):0;
    if (grid_rows>2) grid_rows-=2; else grid_rows=1;
    if (grid_rows>4) grid_rows=4;
    unsigned maximum=ime->expanded?columns*grid_rows:ime->candidate_count;
    int row_width=lv_obj_get_content_width(ime->candidates);
    int gap=lv_obj_get_style_pad_column(ime->candidates,0);
    int word_minimum=px(ime,44),toggle_width=word_minimum;
    if (toggle_width>row_width/3) toggle_width=row_width/3;
    if (toggle_width<1) toggle_width=1;
    /* LVGL weights are 1..15. Scale the whole word row at high DPI so a
     * reserved touch target never needs an unrepresentable weight. */
    int word_unit=(toggle_width+14)/15;
    if (word_unit<8) word_unit=8;
    available=row_width-toggle_width;
    for (unsigned i=0;i<ime->candidate_count && i<maximum;++i) {
        int cell=text_width(ime,text)+padding;
        if (cell<word_minimum) cell=word_minimum;
        if (!ime->expanded && ime->visible && cell+gap>available) break;
        if (ime->expanded && i && i%columns==0) ime->candidate_map[used++]="\n";
        ime->candidate_map[used++]=text; ++ime->visible;
        available-=cell+gap; text+=strlen(text)+1;
    }
    if (ime->expanded) {
        ime->candidate_map[used++]="\n";
        ime->candidate_map[used++]=ime->page_depth || !ime->page?"<":"首页"; ime->candidate_map[used++]="键盘"; ime->candidate_map[used++]=">";
    } else ime->candidate_map[used++]=LV_SYMBOL_UP;
    ime->candidate_map[used]="";
    lv_buttonmatrix_set_map(ime->candidates,ime->candidate_map); map_controls(ime->candidates);
    lv_buttonmatrix_clear_button_ctrl_all(ime->candidates,
        LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_DISABLED | LV_BUTTONMATRIX_CTRL_HIDDEN);
    lv_buttonmatrix_set_button_width(ime->candidates,0,1);
    for (unsigned i=1;i<reading_buttons;++i) {
        unsigned weight=(unsigned)((i==1?composition_width:text_width(ime,ime->candidate_map[i])+padding)/8);
        lv_buttonmatrix_set_button_width(ime->candidates,i,weight>15?15:weight?weight:1);
    }
    if (*preedit && ime->spelling_count) lv_buttonmatrix_set_button_ctrl(ime->candidates,1,LV_BUTTONMATRIX_CTRL_CHECKED);
    else lv_buttonmatrix_set_button_ctrl(ime->candidates,1,LV_BUTTONMATRIX_CTRL_DISABLED);
    unsigned word_units=0;
    for (unsigned i=0;i<ime->visible;++i) {
        const char *word=lv_buttonmatrix_get_button_text(ime->candidates,reading_buttons+i);
        unsigned weight=ime->expanded?1:(unsigned)((text_width(ime,word)+padding)/word_unit);
        if (!ime->expanded && weight<(unsigned)word_minimum/word_unit) weight=(unsigned)word_minimum/word_unit;
        weight=weight>15?15:weight?weight:1;
        word_units+=weight;
        lv_buttonmatrix_set_button_width(ime->candidates,reading_buttons+i,weight);
    }
    unsigned control=reading_buttons+ime->visible;
    unsigned control_weight=1;
    if (!ime->expanded) {
        int remaining=row_width-gap*(int)ime->visible-toggle_width;
        if (remaining<1) remaining=1;
        control_weight=(toggle_width*word_units+(unsigned)remaining-1)/(unsigned)remaining;
        if (control_weight<1) control_weight=1;
        if (control_weight>15) control_weight=15;
    }
    lv_buttonmatrix_set_button_width(ime->candidates,control,control_weight);
    if (ime->expanded) {
        if (!ime->page) lv_buttonmatrix_set_button_ctrl(ime->candidates,control,LV_BUTTONMATRIX_CTRL_DISABLED);
        lv_buttonmatrix_set_button_width(ime->candidates,control+1,3);
        lv_buttonmatrix_set_button_width(ime->candidates,control+2,1);
        if (!ime->more && ime->visible>=ime->candidate_count)
            lv_buttonmatrix_set_button_ctrl(ime->candidates,control+2,LV_BUTTONMATRIX_CTRL_DISABLED);
    } else if (!ime->candidate_count) lv_buttonmatrix_set_button_ctrl(ime->candidates,control,LV_BUTTONMATRIX_CTRL_HIDDEN);
}
static pxa_status_t fallback(pxa_lvgl_ime_t *ime) {
    if (ime->local) return PXA_STATUS_OK;
    ime->queue_count=0; ime->awaiting=false; ime->waiting_release=false;
    ime->local=true;
    if (!ime->builtin) ime->builtin=pxa_lvgl_ime_builtin_create(ime->root);
    if (!ime->builtin) { ime->language=PXA_IME_ENGLISH; ime->layout=PXA_IME_K26; }
    ime->traditional=!(pxa_lvgl_ime_builtin_scripts()&1u);
    if (ime->builtin) (void)pxa_lvgl_ime_builtin_set_traditional(ime->builtin,ime->traditional);
    ime->ready=true; ime->provider_ready=ime->builtin!=NULL;
    clear_presentation(ime); layout(ime,false);
    /* Release the failed Guest outside the UI lock; the local editor survives. */
    ime->config.focus(ime->config.context,ime->generation,false,false);
    return PXA_STATUS_OK;
}
pxa_status_t pxa_lvgl_ime_present(pxa_lvgl_ime_t *ime, uint64_t generation, const pxa_ime_frame_t *frame) {
    if (ime) (void)sync_sensitive_mode(ime);
    if (!ime || !ime->target || generation != ime->generation) return PXA_STATUS_BAD_STATE;
    if (!frame) return fallback(ime);
    if (frame->sequence == 1 && !ime->local) { ime->ready=ime->provider_ready=true; ime->ready_ms=lv_tick_elaps(ime->opened_since); }
    if (frame->sequence > 1) ime->awaiting = false; // sequence 1 acknowledges Host RESET
    unsigned count=frame->candidate_count<12?frame->candidate_count:12;
    unsigned spellings=frame->pinyin_count<32?frame->pinyin_count:32;
    if ((frame->preedit.size && !frame->preedit.data) ||
        (frame->commit.size && !frame->commit.data)) return PXA_STATUS_INVALID_ARGUMENT;
    /* Pointer tables share the presentation allocation and only reserve the
     * actual reply window. Empty RESETs do not need three maximum-size maps. */
    size_t pointer_bytes=(count || spellings || frame->preedit.size)?presentation_pointers(count,spellings)*sizeof(char *):0;
    size_t total=pointer_bytes+4;
    const size_t lengths[]={frame->preedit.size,frame->commit.size,frame->items.size};
    for (unsigned i=0;i<3;++i) {
        if (lengths[i]>SIZE_MAX-total) return PXA_STATUS_INVALID_ARGUMENT;
        total+=lengths[i];
    }
    /* Walk encoded items once. Apart from avoiding quadratic rescans, this
     * keeps the shared keyboard independent of the PXA service runtime. */
    size_t checked=0;
    for (unsigned i=0;i<(unsigned)frame->candidate_count+frame->pinyin_count;++i) {
        if (checked>frame->items.size || frame->items.size-checked<2 || !frame->items.data)
            return PXA_STATUS_INVALID_ARGUMENT;
        size_t n=frame->items.data[checked] | ((size_t)frame->items.data[checked+1]<<8);
        checked+=2;
        if (!n || n>frame->items.size-checked) return PXA_STATUS_INVALID_ARGUMENT;
        checked+=n;
    }
    if (checked!=frame->items.size) return PXA_STATUS_INVALID_ARGUMENT;
    char *buffer = malloc(total);
    if (!buffer) return PXA_STATUS_RESOURCE_LIMIT;
    char *cursor=buffer+pointer_bytes;
    if (frame->commit.size) memcpy(cursor,frame->commit.data,frame->commit.size);
    cursor[frame->commit.size]=0;
    ime->editing = true;
    if (frame->edit == PXA_IME_EDIT_INSERT) lv_textarea_add_text(ime->target,cursor);
    else if (frame->edit == PXA_IME_EDIT_BACKSPACE) lv_textarea_delete_char(ime->target);
    else if (frame->edit == PXA_IME_EDIT_CLEAR) lv_textarea_set_text(ime->target,"");
    if (!ime->target || generation!=ime->generation) {
        /* VALUE_CHANGED may replace the editor or make it sensitive. Its
         * observer already reset the session; never restore old candidates. */
        ime->editing=false; free(buffer); return PXA_STATUS_OK;
    }
    ime->fingerprint = text_fingerprint(ime->target); ime->editing = false;
    cursor += frame->commit.size + 1;
    if (frame->preedit.size) memcpy(cursor,frame->preedit.data,frame->preedit.size);
    cursor[frame->preedit.size]=0;
    if (ime->preedit) lv_label_set_text(ime->preedit,*cursor ? cursor : ime->language ? "English" : "拼音");
    cursor += frame->preedit.size+1;
    ime->candidate_map=pointer_bytes?(const char **)buffer:NULL;
    size_t offset=0;
    for (unsigned i=0;i<frame->candidate_count;++i) {
        size_t n=frame->items.data[offset] | ((size_t)frame->items.data[offset+1]<<8); offset+=2;
        if (i<count) { memcpy(cursor,frame->items.data+offset,n); cursor[n]=0; cursor+=n+1; }
        offset+=n;
    }
    ime->spelling_count=spellings;
    for (unsigned i=0;i<spellings;++i) {
        size_t n=frame->items.data[offset] | ((size_t)frame->items.data[offset+1]<<8); offset+=2;
        memcpy(cursor,frame->items.data+offset,n); cursor[n]=0; cursor+=n+1; offset+=n;
    }
    if (ime->spelling_start >= ime->spelling_count) ime->spelling_start = 0;
    ime->candidate_count = count;
    if (!frame->page_start) ime->page_depth = 0;
    ime->page = frame->page_start;
    ime->more = (frame->flags & PXA_IME_MORE_CANDIDATES) != 0;
    char *previous=ime->items; ime->items=buffer;
    /* A committed word returns to typing even when a partial selection leaves
     * more syllables. Paging and spelling-only replies keep the grid open. */
    if (frame->edit==PXA_IME_EDIT_INSERT && frame->commit.size) ime->expanded=false;
    layout(ime,false);
    free(previous);
    if (frame->edit == PXA_IME_EDIT_SUBMIT || (frame->flags & PXA_IME_SUBMIT)) {
        lv_obj_send_event(ime->target,LV_EVENT_READY,NULL); pxa_lvgl_ime_close(ime);
    }
    flush_input(ime); finish_switch(ime); update_interactivity(ime);
    return PXA_STATUS_OK;
}
void *pxa_lvgl_ime_root(const pxa_lvgl_ime_t *ime) { return ime ? ime->root : NULL; }
void *pxa_lvgl_ime_editor_view(const pxa_lvgl_ime_t *ime) { return ime && ime->target && ime->view_visible ? ime->editor_view : NULL; }
bool pxa_lvgl_ime_active(const pxa_lvgl_ime_t *ime) { return ime && ime->target; }
bool pxa_lvgl_ime_loading_allowed(const pxa_lvgl_ime_t *ime) { return ime && ime->target && ime->drawn; }
void pxa_lvgl_ime_timing(const pxa_lvgl_ime_t *ime,pxa_lvgl_ime_timing_t *output) {
    if (!output) return;
    *output=(pxa_lvgl_ime_timing_t){0};
    if (ime) *output=(pxa_lvgl_ime_timing_t){ime->drawn,ime->provider_ready,ime->first_draw_ms,ime->ready_ms};
}
uint64_t pxa_lvgl_ime_generation(const pxa_lvgl_ime_t *ime) { return ime ? ime->generation : 0; }

static pxa_status_t local_present(void *context,const pxa_ime_frame_t *frame) {
    pxa_lvgl_ime_t *ime=context;
    return pxa_lvgl_ime_present(ime,ime->generation,frame);
}

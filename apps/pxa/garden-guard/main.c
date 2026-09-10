#include "pxa_canvas.h"
#include "pxa_app_messages.h"
#include "pxa_game_sfx.h"
#include "pxa_i18n.h"
#include "pxa_storage.h"

#ifndef GARDEN_DEBUG_UNLOCK_ALL
#define GARDEN_DEBUG_UNLOCK_ALL 0
#endif
#if GARDEN_DEBUG_UNLOCK_ALL != 0 && GARDEN_DEBUG_UNLOCK_ALL != 1
#error "GARDEN_DEBUG_UNLOCK_ALL must be 0 or 1"
#endif

#define GAME_NODE 2u
#define BOARD_ROWS 5
#define BOARD_COLS 8
#define GAME_TICK_MS 50u
#define GAME_MAX_CATCHUP_STEPS 2u
#define AUDIO_TICK_MS 20u
#define RENDER_EVERY_TICKS 2u
#define BOARD_X 38
#define BOARD_Y 50
#define CELL_W 30
#define CELL_H 32
#define MOWER_HOME_X 8
#define MAX_PROJECTILES 16
#define MAX_ZOMBIES 6
#define MAX_SUNS 6
#define MAX_BLASTS 4
#define DOUBLE_TAP_WINDOW_US UINT64_C(450000)

#define LIST_HEADER_H 38
#define LIST_VIEW_Y LIST_HEADER_H
#define LIST_VIEW_H (240 - LIST_VIEW_Y)
#define LIST_DRAG_THRESHOLD 6
#define LEVEL_CARD_X 10
#define LEVEL_CARD_W 132
#define LEVEL_CARD_H 86
#define LEVEL_CARD_STRIDE_X 144
#define LEVEL_CARD_STRIDE_Y 94
#define LEVEL_GRID_Y 46
#define ALMANAC_CARD_X 8
#define ALMANAC_CARD_W 280
#define ALMANAC_CARD_H 86
#define ALMANAC_CARD_STRIDE_Y 94
#define ALMANAC_GRID_Y 44

#define SEED_Y 8
#define SEED_VIEW_X 10
#define SEED_VIEW_W 222
#define SEED_X SEED_VIEW_X
#define SEED_CARD_W 44
#define SEED_STRIDE 48
#define SEED_DRAG_THRESHOLD 6
#define PAUSE_X 242
#define PAUSE_Y 12
#define PAUSE_W 28
#define PAUSE_H 28
#define WAVE_BAR_X 214
#define WAVE_BAR_Y 38
#define WAVE_BAR_W 28

#define SUN_IMAGE_SIZE 22
#define SUN_STATUS_X 70
#define SUN_STATUS_Y 216
#define SUN_STATUS_W 156
#define SUN_STATUS_H 20
#define SHOVEL_X 230
#define SHOVEL_Y 210
#define SHOVEL_W 28
#define SHOVEL_H 26
#define SHOVEL_IMAGE_SIZE 24

#define ACTOR_ATLAS_W 352
#define ACTOR_ATLAS_H 112
#define ACTOR_SHOOTER_X 1
#define ACTOR_SUNFLOWER_X 33
#define ACTOR_WALL_X 65
#define ACTOR_BOMB_X 97
#define ACTOR_ZOMBIE_X 129
#define ACTOR_MOWER_X 161
#define ACTOR_REPEATER_X 193
#define ACTOR_ICE_X 225
#define ACTOR_CONE_ZOMBIE_X 257
#define ACTOR_BUCKET_ZOMBIE_X 289
#define ACTOR_SHOVEL_X 324
#define ACTOR_PUFF_X 1
#define ACTOR_SUNSHROOM_X 33
#define ACTOR_SPIKE_X 65
#define ACTOR_FUME_X 97
#define ACTOR_DOOM_X 129
#define ACTOR_MAGNET_X 161
#define ACTOR_GLOOM_X 193
#define ACTOR_POLE_ZOMBIE_X 0
#define ACTOR_GARG_X 40
#define ACTOR_EXTRA_Y 33
#define ACTOR_TALL_ZOMBIE_Y 64
#define ACTOR_SHOVEL_Y 4
#define ACTOR_PLANT_Y 1
#define ACTOR_MOWER_Y 2
#define PROJECTILE_Y_OFFSET 11
#define SPORE_Y_OFFSET 14

#define PROJECTILE_PEA 0u
#define PROJECTILE_ICE_PEA 1u
#define PROJECTILE_SPORE 2u

#define SHOOTER_FIRE_TICKS 29u
#define SUNFLOWER_FIRST_TICKS 140u
#define SUNFLOWER_PRODUCE_TICKS 480u
#define SKY_SUN_TICKS 200u
#define SUN_LIFETIME_TICKS 160u
#define BOMB_FUSE_TICKS 12u
#define BOMB_BLAST_TICKS 12u
#define REPEATER_FIRE_TICKS 36u
#define ICE_FIRE_TICKS 42u
#define ICE_SLOW_TICKS 100u
#define ZOMBIE_MOVE_TICKS 4u
#define ZOMBIE_BITE_TICKS 20u
#define FIRST_ZOMBIE_TICKS 500u
#define FOLLOWUP_ZOMBIE_TICKS 300u

#define PUFF_FIRE_TICKS 29u
#define PUFF_FLASH_TICKS 4u
#define PUFF_RANGE_PX 95
#define SUNSHROOM_FIRST_TICKS 120u
#define SUNSHROOM_PRODUCE_TICKS 480u
#define SUNSHROOM_GROW_TICKS 550u
#define SPIKE_DAMAGE_TICKS 40u
#define FUME_FIRE_TICKS 36u
#define FUME_RANGE_PX 120
#define DOOM_FUSE_TICKS 18u
#define DOOM_CRATER_TICKS 200u
#define MAGNET_PULL_TICKS 300u
#define MAGNET_RANGE_PX 90
#define GLOOM_DAMAGE_TICKS 50u
#define GLOOM_RANGE_PX 90

#define STATE_READY 0u
#define STATE_PLAYING 1u
#define STATE_WIN 2u
#define STATE_LOSS 3u
#define STATE_PAUSED 4u
#define STATE_HOME 5u
#define STATE_LEVELS 6u
#define STATE_SEED_SELECT 7u
#define STATE_ALMANAC 8u

#define SCENE_DAY 0u
#define SCENE_NIGHT 1u

#define PLANT_EMPTY 0u
#define PLANT_SHOOTER 1u
#define PLANT_SUNFLOWER 2u
#define PLANT_WALL 3u
#define PLANT_BOMB 4u
#define PLANT_REPEATER 5u
#define PLANT_ICE 6u
#define PLANT_PUFF 7u
#define PLANT_SUNSHROOM 8u
#define PLANT_SPIKE 9u
#define PLANT_FUME 10u
#define PLANT_DOOM 11u
#define PLANT_MAGNET 12u
#define PLANT_GLOOM 13u
#define PLANT_TYPE_COUNT 14u

#define ZOMBIE_REGULAR 0u
#define ZOMBIE_CONE 1u
#define ZOMBIE_BUCKET 2u
#define ZOMBIE_POLE 3u
#define ZOMBIE_GARG 4u

#define MOWER_READY 0u
#define MOWER_ACTIVE 1u
#define MOWER_SPENT 2u

#define LEVELS_COUNT 12u
#define MAX_CHOSEN 7u
#define STORAGE_GET_REQ 10u
#define STORAGE_SET_REQ 11u
#define SAVE_VERSION 1u

typedef struct {
    uint8_t type;
    uint8_t health;
    uint16_t timer;
    uint8_t stage;
    uint16_t grow_timer;
} plant_t;

typedef struct {
    int16_t x;
    uint8_t row;
    uint8_t kind;
    uint8_t active;
} projectile_t;

typedef struct {
    int16_t x;
    uint8_t row;
    uint8_t type;
    uint8_t health;
    uint8_t bite_timer;
    uint8_t walk_timer;
    uint8_t slow_timer;
    uint8_t has_pole;
    uint8_t active;
} zombie_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t target_y;
    uint16_t lifetime;
    uint8_t value;
    uint8_t active;
} sun_t;

typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t timer;
    uint8_t active;
} blast_t;

typedef struct {
    uint8_t id;
    uint8_t scene;
    uint8_t slots;
    uint8_t wave_goal;
    uint16_t sky_ticks;
    uint16_t first_spawn;
    uint16_t spawn_period;
    uint8_t sun_initial;
    uint8_t unlock_plant;
    pxa_i18n_message_id_t name;
} level_def_t;

static const level_def_t levels[LEVELS_COUNT] = {
    {1, SCENE_DAY, 3, 8,  200, 500, 300, 50, PLANT_WALL, PXA_MSG_LEVEL_ONE_ONE},
    {2, SCENE_DAY, 4, 10, 200, 480, 280, 50, PLANT_BOMB, PXA_MSG_LEVEL_ONE_TWO},
    {3, SCENE_DAY, 4, 12, 190, 460, 270, 75, PLANT_REPEATER, PXA_MSG_LEVEL_ONE_THREE},
    {4, SCENE_DAY, 5, 15, 190, 440, 260, 75, PLANT_ICE, PXA_MSG_LEVEL_ONE_FOUR},
    {5, SCENE_DAY, 5, 18, 180, 420, 250, 100, PLANT_PUFF, PXA_MSG_LEVEL_ONE_FIVE},
    {6, SCENE_NIGHT, 5, 12, 0, 500, 290, 50, PLANT_SUNSHROOM, PXA_MSG_LEVEL_TWO_ONE},
    {7, SCENE_NIGHT, 5, 15, 0, 460, 270, 50, PLANT_SPIKE, PXA_MSG_LEVEL_TWO_TWO},
    {8, SCENE_NIGHT, 6, 20, 0, 440, 260, 75, PLANT_FUME, PXA_MSG_LEVEL_TWO_THREE},
    {9, SCENE_NIGHT, 6, 22, 0, 400, 240, 75, PLANT_DOOM, PXA_MSG_LEVEL_TWO_FOUR},
    {10, SCENE_DAY, 6, 25, 180, 380, 230, 100, PLANT_MAGNET, PXA_MSG_LEVEL_THREE_ONE},
    {11, SCENE_NIGHT, 7, 30, 0, 360, 220, 75, PLANT_GLOOM, PXA_MSG_LEVEL_THREE_TWO},
    {12, SCENE_DAY, 7, 35, 170, 340, 210, 100, 0, PXA_MSG_LEVEL_THREE_THREE},
};

#include "garden_music.inc"

static uint8_t draw_data[16 * 1024];
static uint8_t packet[512];
static uint8_t storage_payload[128];
static uint32_t generation;
static uint32_t random_state = 0x39d8a617u;
static uint32_t sun_count;
static uint8_t defeated;
static uint8_t zombies_spawned;
static uint16_t spawn_timer;
static uint16_t sky_sun_timer;
static uint8_t selected_plant;
static uint8_t shovel_mode;
static uint16_t seed_scroll;
static uint16_t seed_drag_scroll;
static int16_t seed_drag_x;
static uint8_t seed_drag_active;
static uint8_t seed_dragged;
static uint8_t initialized;
static uint8_t state;
static uint64_t last_tick_us;
static uint32_t game_remainder_us;
static uint8_t render_ticks;
static uint64_t last_empty_tap_us;
static uint16_t plant_cooldowns[PLANT_TYPE_COUNT];
static plant_t plants[BOARD_ROWS][BOARD_COLS];
static uint16_t crater_timer[BOARD_ROWS][BOARD_COLS];
static projectile_t projectiles[MAX_PROJECTILES];
static zombie_t zombies[MAX_ZOMBIES];
static sun_t suns[MAX_SUNS];
static blast_t blasts[MAX_BLASTS];
static uint8_t mower_state[BOARD_ROWS];
static int16_t mower_x[BOARD_ROWS];
static pxa_game_sfx_t sfx;
static const char actor_asset[] = "assets/actors.png";
static const char sun_asset[] = "assets/sun.png";

static uint8_t current_level = 0;
static uint8_t max_unlocked = 1;
static uint32_t unlocked_mask = 0;
static uint8_t level_stars[LEVELS_COUNT] = {0};
static uint8_t chosen_plants[MAX_CHOSEN];
static uint8_t chosen_count = 0;
static uint8_t focused_plant = PLANT_SHOOTER;
static uint16_t level_scroll = 0;
static uint16_t almanac_scroll = 0;
static uint16_t list_drag_scroll = 0;
static int16_t list_drag_y = 0;
static uint8_t list_drag_active = 0;
static uint8_t list_dragged = 0;
static uint8_t storage_pending = 0;
static uint8_t win_reward_plant = 0;
static uint32_t home_ticks = 0;
static uint8_t sound_enabled = 1;
static pxa_i18n_t i18n;

static const char* garden_message(pxa_i18n_message_id_t id) {
    return pxa_i18n_cstr(&i18n, id);
}

static size_t garden_text_len(const char* text) {
    size_t size = 0;
    while (text != NULL && text[size] != '\0') ++size;
    return size;
}

static uint32_t random_next(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static int16_t cell_x(int col) { return (int16_t)(BOARD_X + col * CELL_W); }
static int16_t cell_y(int row) { return (int16_t)(BOARD_Y + row * CELL_H); }
static int16_t fume_start_x(int col) { return (int16_t)(cell_x(col)+CELL_W); }
static int16_t fume_end_x(int col) { return (int16_t)(fume_start_x(col)+FUME_RANGE_PX); }

static const level_def_t* current_level_def(void) {
    if (current_level < LEVELS_COUNT) return &levels[current_level];
    return &levels[0];
}
static uint8_t is_night(void) { return current_level_def()->scene == SCENE_NIGHT; }
static void sync_background_music(void) {
    const pxa_game_music_song_t* song = NULL;
    if (state == STATE_HOME || state == STATE_LEVELS || state == STATE_ALMANAC)
        song = &garden_main_menu_song;
    else if (state == STATE_SEED_SELECT)
        song = &garden_choose_seeds_song;
    else if (is_night())
        song = &garden_moongrains_song;
    else if (current_level >= 9)
        song = &garden_loonboon_song;
    else
        song = &garden_grasswalk_song;

    if (song != NULL && sfx.music_song != song)
        pxa_game_sfx_set_song(&sfx, song);
    else if (song == NULL && sfx.music_song != NULL)
        pxa_game_sfx_set_theme(&sfx, PXA_GAME_SFX_THEME_GARDEN);
}
static int is_plant_unlocked(uint8_t type) {
    if (type==PLANT_EMPTY) return 1;
    return (unlocked_mask & (1u<<type)) != 0;
}
static uint8_t plant_cost(uint8_t type) {
    static const uint8_t costs[] = {0,100,50,50,150,200,175,0,25,100,75,125,100,150};
    return type<PLANT_TYPE_COUNT?costs[type]:0;
}
static uint8_t plant_health(uint8_t type) {
    static const uint8_t health[] = {0,6,6,32,1,6,6,4,6,12,6,6,6,6};
    return type<PLANT_TYPE_COUNT?health[type]:0;
}
static uint16_t plant_recharge_ticks(uint8_t type) {
    static const uint16_t recharge[] = {0,150,150,600,1000,300,350,150,150,350,250,750,450,350};
    return type<PLANT_TYPE_COUNT?recharge[type]:0;
}
static uint8_t zombie_health(uint8_t type) {
    static const uint8_t health[] = {4,7,11,6,18};
    uint8_t idx = type < 5 ? type : type%3;
    return health[idx];
}
static int16_t zombie_actor_x(uint8_t type) {
    static const int16_t source_x[] = {ACTOR_ZOMBIE_X, ACTOR_CONE_ZOMBIE_X, ACTOR_BUCKET_ZOMBIE_X, ACTOR_POLE_ZOMBIE_X, ACTOR_GARG_X};
    uint8_t idx = type < 5 ? type : type%3;
    return source_x[idx];
}
static const char* plant_name(uint8_t type) {
    static const pxa_i18n_message_id_t names[] = {
        0, PXA_MSG_PLANT_SHOOTER_NAME, PXA_MSG_PLANT_SUNFLOWER_NAME,
        PXA_MSG_PLANT_WALL_NAME, PXA_MSG_PLANT_BOMB_NAME,
        PXA_MSG_PLANT_REPEATER_NAME, PXA_MSG_PLANT_ICE_NAME,
        PXA_MSG_PLANT_PUFF_NAME, PXA_MSG_PLANT_SUNSHROOM_NAME,
        PXA_MSG_PLANT_SPIKE_NAME, PXA_MSG_PLANT_FUME_NAME,
        PXA_MSG_PLANT_DOOM_NAME, PXA_MSG_PLANT_MAGNET_NAME,
        PXA_MSG_PLANT_GLOOM_NAME};
    if(type>=1 && type<PLANT_TYPE_COUNT) return garden_message(names[type]);
    return "";
}
static uint8_t plant_name_len(uint8_t type){
    return (uint8_t)garden_text_len(plant_name(type));
}
static const char* plant_description(uint8_t type,uint8_t* length){
    static const pxa_i18n_message_id_t descriptions[] = {
        0, PXA_MSG_PLANT_SHOOTER_DESCRIPTION,
        PXA_MSG_PLANT_SUNFLOWER_DESCRIPTION, PXA_MSG_PLANT_WALL_DESCRIPTION,
        PXA_MSG_PLANT_BOMB_DESCRIPTION, PXA_MSG_PLANT_REPEATER_DESCRIPTION,
        PXA_MSG_PLANT_ICE_DESCRIPTION, PXA_MSG_PLANT_PUFF_DESCRIPTION,
        PXA_MSG_PLANT_SUNSHROOM_DESCRIPTION, PXA_MSG_PLANT_SPIKE_DESCRIPTION,
        PXA_MSG_PLANT_FUME_DESCRIPTION, PXA_MSG_PLANT_DOOM_DESCRIPTION,
        PXA_MSG_PLANT_MAGNET_DESCRIPTION, PXA_MSG_PLANT_GLOOM_DESCRIPTION};
    const char* text = type>=1 && type<PLANT_TYPE_COUNT
        ? garden_message(descriptions[type]) : "";
    *length=0;
    while(text[*length]!='\0') ++*length;
    return text;
}
static int plant_is_chosen(uint8_t type){
    for(uint8_t i=0;i<chosen_count;++i) if(chosen_plants[i]==type) return 1;
    return 0;
}
static int plant_is_selectable(uint8_t type){
    return type>PLANT_EMPTY && type<PLANT_TYPE_COUNT &&
           is_plant_unlocked(type) && !plant_is_chosen(type);
}
static int plant_is_mushroom(uint8_t type){
    return type==PLANT_PUFF || type==PLANT_SUNSHROOM ||
           type==PLANT_FUME || type==PLANT_DOOM ||
           type==PLANT_MAGNET || type==PLANT_GLOOM;
}
static void choose_default_plants(void){
    const level_def_t* lvl=current_level_def();
    chosen_count=0;
    if(lvl->scene==SCENE_NIGHT){
        for(uint8_t type=1;type<PLANT_TYPE_COUNT && chosen_count<lvl->slots;++type){
            if(plant_is_mushroom(type) && is_plant_unlocked(type)) chosen_plants[chosen_count++]=type;
        }
    }
    for(uint8_t type=1;type<PLANT_TYPE_COUNT && chosen_count<lvl->slots;++type){
        if(is_plant_unlocked(type) && !plant_is_chosen(type)) chosen_plants[chosen_count++]=type;
    }
}
static void play_sfx(uint8_t kind){ if(sound_enabled) pxa_game_sfx_play(&sfx,kind); }
static uint16_t seed_scroll_limit_chosen(void){
    if(chosen_count<=4) return 0;
    uint16_t content = (uint16_t)(chosen_count*SEED_STRIDE);
    return content > SEED_VIEW_W ? (uint16_t)(content - SEED_VIEW_W + 4) : 0;
}
static void scroll_seed_bar(int16_t delta){
    int32_t next=(int32_t)seed_scroll+delta;
    uint16_t limit=seed_scroll_limit_chosen();
    if(next<0) next=0; else if((uint32_t)next>limit) next=limit;
    seed_scroll=(uint16_t)next;
}
static void garden_save(void);
#include "garden_storage.inc"
#include "garden_gameplay.inc"
#include "garden_render.inc"
#include "garden_input.inc"

int32_t pxa_app_start(const uint8_t* config,uint32_t config_length){
    (void)pxa_i18n_init_from_start_config(
        &i18n,&pxa_app_i18n_bundle,config,config_length);
    if(!pxa_window_fullscreen()) return PXA_STATUS_INTERNAL;
    init_unlocks();
    state=STATE_HOME;
    current_level=0;
    chosen_count=0;
    level_scroll=0;
    almanac_scroll=0;
    if(!render() || !pxa_clock_set_period(AUDIO_TICK_MS)) return PXA_STATUS_INTERNAL;
    sync_background_music();
    pxa_game_sfx_start(&sfx,packet,sizeof(packet));
    garden_load();
    return PXA_STATUS_OK;
}

int32_t pxa_app_on_event(const uint8_t* event,uint32_t length){
    pxa_canvas_event_t parsed;
    pxa_ui_pointer_data_t pointer;
    if(!pxa_canvas_parse_event(event,length,&parsed)) return PXA_EVENT_UNHANDLED;
    {
        int locale_result=pxa_i18n_handle_event(&i18n,&parsed);
        if(locale_result!=0)
            return locale_result==1 && !render()?PXA_STATUS_INTERNAL:PXA_EVENT_HANDLED;
    }
    if(parsed.service==PXA_SERVICE_WINDOW && parsed.opcode==PXA_WINDOW_BACK_REQUESTED){
        if(!navigate_back()) return PXA_EVENT_UNHANDLED;
        if(!render()) return PXA_STATUS_INTERNAL;
        return PXA_EVENT_HANDLED;
    }
    if(pxa_game_sfx_handle_event(&sfx,&parsed,packet,sizeof(packet))) return PXA_EVENT_HANDLED;
    if(parsed.service==PXA_SERVICE_STORAGE){
        if(parsed.opcode==PXA_STORAGE_GET && parsed.request_id==STORAGE_GET_REQ){
            pxa_storage_get_result_t result;
            if(!pxa_storage_parse_get(&parsed,&result)) return PXA_STATUS_INTERNAL;
            if(result.status==PXA_STATUS_OK && result.value_length>=6){
                const uint8_t* v=result.value;
                if(v[0]==SAVE_VERSION){
                    max_unlocked=v[1];
                    if(max_unlocked==0) max_unlocked=1;
                    if(max_unlocked>LEVELS_COUNT) max_unlocked=LEVELS_COUNT;
                    unlocked_mask=(uint32_t)v[2]|((uint32_t)v[3]<<8)|((uint32_t)v[4]<<16)|((uint32_t)v[5]<<24);
                    if(unlocked_mask==0) init_unlocks();
                    else {
                        for(uint8_t i=0;i<LEVELS_COUNT && 6+i < result.value_length;++i) level_stars[i]=v[6+i];
                    }
                }
            } else if(result.status==PXA_STATUS_NOT_FOUND){
                init_unlocks();
            }
            apply_debug_unlocks();
            if(!render()) return PXA_STATUS_INTERNAL;
            return PXA_EVENT_HANDLED;
        } else if(parsed.opcode==PXA_STORAGE_SET && parsed.request_id==STORAGE_SET_REQ){
            storage_pending=0;
            return PXA_EVENT_HANDLED;
        }
        return PXA_EVENT_UNHANDLED;
    }
    if(parsed.service==PXA_SERVICE_CLOCK && parsed.opcode==PXA_CLOCK_TICK && parsed.payload_length==8){
        sync_background_music();
        if(state!=STATE_PAUSED) pxa_game_sfx_tick(&sfx,&parsed);
        if(state!=STATE_PAUSED) random_state^=(uint32_t)pxa_read_u64(parsed.payload);
        // 自动推进调试 - 若HOME停留过久自动进入选卡，避免点击失灵假死
        if(state==STATE_HOME){
            if(++home_ticks > 120){
                home_ticks=0;
                // 不自动跳转，仅重置计数避免干扰正常点击，保留用于观察是否卡死为渲染死循环
            }
        } else {
            home_ticks=0;
        }
        if(state==STATE_PLAYING || state==STATE_READY){
            if(!tick(game_clock_steps(&parsed))) return PXA_STATUS_INTERNAL;
        } else if(state==STATE_HOME || state==STATE_LEVELS || state==STATE_SEED_SELECT || state==STATE_ALMANAC){
            // 确保非游戏状态也定期重绘，避免画面冻结
            static uint8_t idle_render=0;
            if(++idle_render>=10){
                idle_render=0;
                if(!render()) return PXA_STATUS_INTERNAL;
            }
        }
        return PXA_EVENT_HANDLED;
    }
    if(pxa_canvas_parse_pointer(&parsed,GAME_NODE,&pointer)){
        int16_t x=(int16_t)pointer.x;
        int16_t y=(int16_t)pointer.y;
        if((state==STATE_PLAYING || state==STATE_READY) &&
           (pointer.phase==PXA_POINTER_DOWN || pointer.phase==PXA_POINTER_MOVE) && hit_sun(x,y)){
            if(!render()) return PXA_STATUS_INTERNAL;
            return PXA_EVENT_HANDLED;
        }
        if(pointer.phase==PXA_POINTER_MOVE){
            if((state==STATE_PLAYING || state==STATE_READY) && update_seed_gesture(x) && !render()) return PXA_STATUS_INTERNAL;
            if((state==STATE_LEVELS || state==STATE_ALMANAC) && update_list_gesture(y) && !render()) return PXA_STATUS_INTERNAL;
            return PXA_EVENT_HANDLED;
        }
        if(pointer.phase==PXA_POINTER_UP || pointer.phase==PXA_POINTER_CANCEL){
            seed_drag_active=0;
            if(state==STATE_LEVELS || state==STATE_ALMANAC){
                uint8_t had_gesture=list_drag_active;
                uint8_t dragged=list_dragged;
                list_drag_active=0;
                if(pointer.phase==PXA_POINTER_CANCEL || !had_gesture || dragged){
                    if(dragged && !render()) return PXA_STATUS_INTERNAL;
                    return PXA_EVENT_HANDLED;
                }
                if(state==STATE_LEVELS){
                    int idx;
                    if(hit_level_button(x,y,&idx)){
                        if(idx<max_unlocked) open_level_for_selection((uint8_t)idx);
                        else play_sfx(PXA_GAME_SFX_ALERT);
                    }
                }
                if(!render()) return PXA_STATUS_INTERNAL;
            }
            return PXA_EVENT_HANDLED;
        }
        if(pointer.phase!=PXA_POINTER_DOWN) return PXA_EVENT_HANDLED;


        if(state==STATE_HOME){
            if(hit_home_levels(x,y)){
                level_scroll=0;
                state=STATE_LEVELS;
                play_sfx(PXA_GAME_SFX_TAP);
            } else if(hit_home_almanac(x,y)){
                almanac_scroll=0;
                state=STATE_ALMANAC;
                play_sfx(PXA_GAME_SFX_TAP);
            } else if(hit_home_start(x,y) || (y>=90 && y<240)){
                // 扩大判定，任意点击下半屏都进入选卡，避免因圆角或文字偏移导致假死
                current_level = max_unlocked>0 ? max_unlocked-1 : 0;
                choose_default_plants();
                focused_plant=PLANT_EMPTY;
                state=STATE_SEED_SELECT;
                play_sfx(PXA_GAME_SFX_TAP);
            }
        } else if(state==STATE_LEVELS){
            if(hit_back(x,y)) (void)navigate_back();
            else (void)begin_list_gesture(y);
        } else if(state==STATE_SEED_SELECT){
            if(hit_back(x,y)){ state=STATE_LEVELS; play_sfx(PXA_GAME_SFX_TAP); }
            else if(hit_seed_start(x,y)){
                if(chosen_count>0){
                    reset_game_for_level(current_level);
                    play_sfx(PXA_GAME_SFX_ACTION);
                } else {
                    play_sfx(PXA_GAME_SFX_ALERT);
                }
            } else {
                int slot;
                uint8_t plant;
                if(hit_seed_select_button(x,y)){
                    const level_def_t* lvl=current_level_def();
                    if(plant_is_selectable(focused_plant) && chosen_count<lvl->slots){
                        chosen_plants[chosen_count++]=focused_plant;
                        focused_plant=PLANT_EMPTY;
                        play_sfx(PXA_GAME_SFX_TAP);
                    } else {
                        play_sfx(PXA_GAME_SFX_ALERT);
                    }
                } else if(hit_seed_cancel_button(x,y)){
                    focused_plant=PLANT_EMPTY;
                    play_sfx(PXA_GAME_SFX_TAP);
                } else if(hit_seed_chosen(x,y,&slot)){
                    if(slot < (int)chosen_count){
                        for(int i=slot;i< (int)chosen_count-1;++i) chosen_plants[i]=chosen_plants[i+1];
                        --chosen_count;
                        focused_plant=PLANT_EMPTY;
                        play_sfx(PXA_GAME_SFX_TAP);
                    }
                } else if(focused_plant==PLANT_EMPTY && hit_seed_avail(x,y,&plant)){
                    focused_plant=plant;
                    play_sfx(PXA_GAME_SFX_TAP);
                }
            }
        } else if(state==STATE_ALMANAC){
            if(hit_back(x,y)) (void)navigate_back();
            else (void)begin_list_gesture(y);
        } else {
            if(state==STATE_PAUSED){
                if(x>=68 && x<228 && y>=100 && y<124){
                    state=STATE_PLAYING;
                    play_sfx(PXA_GAME_SFX_TAP);
                } else if(x>=68 && x<228 && y>=130 && y<154){
                    reset_game_for_level(current_level);
                    play_sfx(PXA_GAME_SFX_ACTION);
                } else if(x>=68 && x<228 && y>=160 && y<184){
                    state=STATE_HOME;
                    play_sfx(PXA_GAME_SFX_TAP);
                } else if(x>=68 && x<228 && y>=190 && y<214){
                    sound_enabled = !sound_enabled;
                    play_sfx(PXA_GAME_SFX_TAP);
                } else if(hit_pause_button(x,y)){
                    state=STATE_PLAYING;
                    play_sfx(PXA_GAME_SFX_TAP);
                }
                last_empty_tap_us=0;
            } else if(hit_pause_button(x,y)){
                last_empty_tap_us=0;
                if(state==STATE_PLAYING) state=STATE_PAUSED;
                else if(state==STATE_PAUSED) state=STATE_PLAYING;
                play_sfx(PXA_GAME_SFX_TAP);
            } else if(state!=STATE_PAUSED){
                if(state==STATE_WIN){
                    state=STATE_LEVELS;
                    play_sfx(PXA_GAME_SFX_ACTION);
                } else if(state==STATE_LOSS){
                    reset_game_for_level(current_level);
                    play_sfx(PXA_GAME_SFX_ACTION);
                } else if(hit_shovel_button(x,y)){
                    shovel_mode=!shovel_mode;
                    selected_plant=PLANT_EMPTY;
                    last_empty_tap_us=0;
                    play_sfx(PXA_GAME_SFX_TAP);
                } else {
                    if(!begin_seed_gesture(x,y)){
                        if(state==STATE_READY) state=STATE_PLAYING;
                        if(shovel_mode){
                            if(remove_plant(x,y)) shovel_mode=0;
                        } else if(selected_plant==PLANT_EMPTY){
                            (void)collect_suns_on_double_tap(x,y);
                        } else {
                            last_empty_tap_us=0;
                            (void)place_plant(x,y);
                        }
                    }
                }
            }
        }
        if(!render()) return PXA_STATUS_INTERNAL;
        return PXA_EVENT_HANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason){ (void)reason; }

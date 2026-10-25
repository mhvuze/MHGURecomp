// Player settings: mhgurecomp.ini in the game folder (written with the defaults when it is missing).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { DISPLAY_WINDOWED, DISPLAY_FULLSCREEN, DISPLAY_BORDERLESS };
enum { FACE_POSITION, FACE_LABEL };

// [Monster HP]: factors on the HP multiplier of Hub-type quests' large monsters
#define HP_QUESTS_MAX 256
#define HP_SCALE_MIN 0.05f
#define HP_SCALE_MAX 20.0f
typedef struct {
    float scale[4];  // by the number of hunters (index: hunters - 1)
    float village;   // Village quests posted in the Hub / Pub: this first, then scale ([Village Quests] HubHP)
    int quests;
    struct { uint32_t quest; float scale[4]; } quest[HP_QUESTS_MAX];  // per quest, in place of scale
} HpScale;

typedef struct {
    int display_mode;         // DISPLAY_*
    int width, height;        // screen / window size (Fullscreen: the monitor mode)
    int render_w, render_h;   // the game's 3D resolution (16:9)
    int fps;                  // frame rate cap: 30..240
    int vsync;                // 1: present without tearing (mailbox / FIFO, at most the refresh rate), 0: immediate
    int msaa;                 // samples of the 3D scene: 1 (off), 2, 4, 8
    int fxaa;                 // the game's own FXAA pass: 1 on, 0 off
    int face_buttons;         // FACE_*: controller face buttons by position (the Switch layout) or by their labels
    int ldn_port;             // local play: the port a hosted hub listens on (TCP; UDP for LAN search)
    char ldn_join[256];       // local play: hosts to search besides the LAN (host[:port], comma separated)
    int sync_small_monsters;  // multiplayer: small monsters synced like large ones (runtime/net/emsync.c)
    int bonus_content;        // free downloads copied from a donor save into saves without any (runtime/services/bonus.c)
    int village_in_hub;       // L / R at the Hub's / Pub's quest counter switch to Village quests (runtime/hle/villagehub.c)
    HpScale hp;               // large monster HP in Hub-type quests (runtime/hle/hpscale.c)
    float mouse_sensitivity;  // mouse look: the speed for full right stick tilt is divided by it (runtime/host_input.c)
    float aspect_ratio;       // the picture's shape (width / height), 0: the window's; never below 16:10 (config.c)
    float fov;                // 3D camera field of view factor (1: the game's)
    int ui_anchor;            // where the HUD goes on a picture wider than 16:9: UI_*
    int background_input;     // the controller keeps working while the window is in the background (runtime/ui.c)
} Config;
enum { UI_CENTER = 1, UI_21_9, UI_32_9, UI_EDGES };  // the middle 16:9, 21:9, 32:9, the picture's sides

extern Config g_config;
extern uint32_t g_render_size[2];  // read by the game code (tools/gen.py PATCHES: its docked render size)
extern float g_render_aspect;      // the picture's shape, 16:10 or wider: the 3D cameras' aspect (tools/gen.py PATCHES)
extern float g_fov_scale;          // 3D camera field of view factor (tools/gen.py PATCHES)
extern uint32_t g_ui_size[2];      // the game's 2D canvas size (tools/gen.py PATCHES: 0xdbcd48, 0xdbcd54)
extern float g_ui_anchor_aspect;   // the HUD's width / height, 16:9 .. g_render_aspect (nvn/vulkan/exec.c)
float camera_fovy(float fovy, float aspect);  // a 3D camera's vertical field of view for its aspect (gen.py PATCHES)

void config_load(void);  // before the window and the guest start

// The settings a menu offers (the macOS Settings menu: runtime/platform/darwin_menu.m), in ini order. The rest of
// the file (multiplayer addresses, monster HP factors...) is edited by hand.
typedef struct {
    const char *section, *key, *title;  // its ini section and key, its menu title
    const char *choices;                // the values offered, as written in the ini, '|' between them
    const char *labels;                 // their names in the menu, '|' between them (NULL: the values)
    bool live;                          // takes effect at once (config_set); otherwise at the next start
} ConfigSetting;
extern const ConfigSetting g_settings[];
extern const int g_nsettings;
void config_path(char *buf, size_t size);  // the settings file
// A setting's value as the file has it now (its default when the file lacks it), into buf; false: no such key.
bool config_get(const char *key, char *buf, size_t size);
bool config_same_value(const char *a, const char *b);  // two values as the parser reads them ("1.0" == "1", case)
bool config_restart_pending(const char *key);  // its value in the file differs from the one the game started with
                                                // (NULL: any setting that applies at the next start)
// Writes the value into the file (in place: comments and other settings stay) and, for a live setting, into
// g_config. The caller applies a live one where needed (runtime/ui.c ui_setting_changed). false: not written.
bool config_set(const char *key, const char *value);

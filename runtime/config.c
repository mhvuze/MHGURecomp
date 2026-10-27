// Player settings (config.h): mhgurecomp.ini in the game folder (next to the executable, or MHGU_HOME; with a game
// folder argument, the current folder). A missing file is written with the defaults, so
// deleting it resets everything; keys that are missing or not understood keep their defaults (the log says which).
//   MHGU_CONFIG=<path>   another settings file
//   MHGU_WINDOW=WxH      windowed at that size, MHGU_RENDER=WxH  3D resolution, MHGU_MSAA=1|2|4|8, MHGU_FXAA=0|1,
//                        MHGU_PAD_POSITIONAL=0|1 (test runs; override the file)
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL3/SDL.h>

#include "config.h"
#include "rt.h"

Config g_config = {DISPLAY_BORDERLESS, 1920, 1080, 0, 0, 60, 1, 1, 1, FACE_POSITION, 39500, "", 1, 1, 1, {{1, 1, 1, 1}, 2}, 1.0f, 0.0f, 1.0f, UI_CENTER, 1, 0, FSR_NATIVE, 0.8f, 1, 0.9f, 1, FSR_UP_3};
uint32_t g_render_size[2] = {1920, 1080};
float g_render_aspect = 16.0f / 9.0f;
float g_fov_scale = 1.0f;
uint32_t g_ui_size[2] = {1280, 720};
float g_ui_anchor_aspect = 16.0f / 9.0f;
uint32_t g_fsr_output[2];
float g_fsr_lod_bias;

// The few words that differ by platform: the full-screen key, and where the donor save goes (runtime/services/bonus.c)
#ifdef __APPLE__
#define FULLSCREEN_KEYS "Option+Enter"
#define BONUS_PLACE "in the settings folder"
#else
#define FULLSCREEN_KEYS "Alt+Enter"
#define BONUS_PLACE "next to mhgurecomp.exe"
#endif
static const char DEFAULT_INI[] =
    "; MHGU Recomp settings. Delete this file to get the defaults back: it is written again at the next start.\n"
    "\n"
    "[Display]\n"
    "; Fullscreen, Windowed or Borderless (a borderless window covering the whole screen).\n"
    "; " FULLSCREEN_KEYS " or F11 switches between Windowed and the full-screen mode chosen here.\n"
    "Mode = Borderless\n"
    "\n"
    "; Screen size: WxH (1280x720, 1600x900, 1920x1080, 2560x1440...) or Desktop (the monitor's own resolution).\n"
    "; Fullscreen switches the monitor to this mode, Windowed opens a window this size (it can be resized), and\n"
    "; Borderless always covers the screen, scaling the game's image to it.\n"
    "Resolution = 1920x1080\n"
    "\n"
    "; The game's 3D resolution: Auto (as large as fits Resolution, in the picture's shape below) or WxH. Lower is\n"
    "; faster, higher is sharper; the Switch renders 1920x1080 docked. Above Resolution it supersamples (smoothest at\n"
    "; exactly double, e.g. 3840x2160 for 1920x1080).\n"
    "RenderResolution = Auto\n"
    "\n"
    "; The picture's shape: Auto (the window's) or a ratio such as 21:9. Wider than 16:9 (ultrawide screens) shows\n"
    "; more to the sides, 16:10 a little more above and below. Narrower than 16:10 (4:3) gets 16:10 with bars.\n"
    "AspectRatio = Auto\n"
    "\n"
    "; Field of view: 1.0 is the game's, above 1.0 (1.1, 1.2) shows more, below 1.0 less. 0.5 to 2.\n"
    "FOV = 1.0\n"
    "\n"
    "; Where the HUD goes on a picture wider than 16:9: Center (in the middle 16:9, as the game lays it out) or Edges\n"
    "; (out to the sides of the screen). 21:9 or 32:9 spread it that wide, at most to the sides.\n"
    "UIAnchor = Center\n"
    "\n"
    "[Anti-aliasing]\n"
    "; MSAA: Off, 2x, 4x or 8x. Smooths the edges of 3D geometry and of some cut-out leaves and fences; each step\n"
    "; costs GPU time and video memory, a lot on older cards (an R9 270X at 1080p: 56 fps off, 35 at 2x, 28 at 4x).\n"
    "; F7 cycles through them while playing, to compare. (The Switch version has none.)\n"
    "MSAA = Off\n"
    "\n"
    "; FXAA: On or Off. The game's own edge smoothing: a filter over the finished image, slightly blurry.\n"
    "FXAA = On\n"
    "\n"
    "[Upscaler]\n"
    "; FSR: On or Off. AMD FidelityFX Super Resolution 3.1, a temporal upscaler: the game renders its 3D picture at a\n"
    "; lower resolution (FSRQuality) and FSR rebuilds it at full resolution (RenderResolution, or the screen's with\n"
    "; Auto) from several frames, smoothing its edges as it does; the HUD is drawn at full resolution on top. Sharper\n"
    "; than a lower RenderResolution; faster than full resolution where the 3D rendering is what limits the frame\n"
    "; rate (FSR itself takes some GPU time: about 0.5 ms at 1920x1080 on a fast card). On turns MSAA and FXAA off\n"
    "; (FSR does the anti-aliasing).\n"
    "FSR = Off\n"
    "\n"
    "; Upscaler: FSR3 or FSR4. FSR3 = FSR 3.1 (any GPU with Vulkan 1.2). FSR4 = AMD's machine-learning upscaler (RX 9000\n"
    "; series / RDNA 4; it runs AMD's own amdvkffx64.dll from the installed Adrenalin driver, nothing is shipped here).\n"
    "; Same inputs, same FSRQuality ratios, same Sharpness; if FSR 4 cannot start, FSR 3.1 is used (log.txt says why).\n"
    "Upscaler = FSR3\n"
    "\n"
    "; FSRQuality: how much lower the 3D picture is rendered: NativeAA (full resolution: FSR as anti-aliasing - the\n"
    "; best picture, smooth and stable edges at full sharpness; the game is light, so a recent card keeps its frame\n"
    "; rate), Quality (1.5x lower each way: 1280x720 for 1920x1080), Balanced (1.7x), Performance (2x) or\n"
    "; UltraPerformance (3x) for slower cards. Even better: NativeAA or Quality with RenderResolution above the\n"
    "; screen's (e.g. 2880x1620 on a 1920x1080 screen).\n"
    "FSRQuality = NativeAA\n"
    "\n"
    "; Sharpness: FSR's sharpening, 0 (off) to 1 (strongest). 0.8 keeps the picture as sharp as the game's own.\n"
    "Sharpness = 0.8\n"
    "\n"
    "; ObjectMotion: On or Off. On: FSR also learns how characters and monsters move (from their geometry, frame to\n"
    "; frame), so their limbs stay sharp instead of smearing. Off: only the camera's motion (a little faster).\n"
    "ObjectMotion = On\n"
    "\n"
    "; Reactive: 0 (off) to 1. How much FSR trusts the current frame over its history where the game marks moving\n"
    "; objects and their shadows: higher removes ghost trails, lower keeps them smoother. Lowered by itself on surfaces\n"
    "; whose motion ObjectMotion knows.\n"
    "Reactive = 0.9\n"
    "\n"
    "; AnimSurf: On or Off. On: surfaces whose picture changes without moving (banners and flags animated through\n"
    "; their texture, flowing water) are found each frame and shown from the current frame there, so they do not\n"
    "; smear. Off: FSR treats them like everything else.\n"
    "AnimSurf = On\n"
    "\n"
    "[Performance]\n"
    "; Frame rate cap: 30 (the Switch version's rate) to 240. Game speed stays the same at any rate.\n"
    "FPS = 60\n"
    "\n"
    "; VSync: On or Off. On: no tearing, but no more frames than the monitor shows (a 60 Hz monitor stays at 60 fps).\n"
    "; Off: frames are shown as soon as they are done, up to FPS (tearing possible).\n"
    "VSync = On\n"
    "\n"
    "[Controls]\n"
    "; Controller face buttons: Position or Label.\n"
    "; Position: the Switch layout the game is made for - the right button is A (confirm), the bottom one B, the left\n"
    ";   one Y, the top one X, whatever is printed on them (on an Xbox controller the in-game icons then don't match).\n"
    "; Label: by the letter printed on the button (an Xbox controller's A is A; the icons match, the layout is awkward).\n"
    "FaceButtons = Position\n"
    "\n"
    "; The controller while the game's window is in the background (a wiki in front of it, or on another screen):\n"
    "; On (it keeps working) or Off. The keyboard and mouse only work while the window has the focus.\n"
    "BackgroundInput = On\n"
    "\n"
    "[Mouse]\n"
    "; Mouse look: click in the game window to capture the mouse (it then turns the camera, as the right stick), Esc\n"
    "; lets it go. Sensitivity: 1 = full stick tilt at a brisk mouse movement; 0.5 slower, 2 faster (0.1 to 10). The\n"
    "; game's own camera options (speed, inverted axes) apply on top.\n"
    "MouseSensitivity = 1.0\n"
    "\n"
    "[Multiplayer]\n"
    "; Local Play (the Switch's wireless play with people nearby) over the network. Create a hub under Local Play to\n"
    "; host: the others reach your computer on this port. Over the internet the host forwards it in their router,\n"
    "; TCP and UDP (TCP alone works too, but copes worse with a bad connection); nobody else needs to.\n"
    "Port = 39500\n"
    "\n"
    "; Search Hubs finds hubs on your LAN by itself. To reach a host elsewhere, put its address here: an IP or name,\n"
    "; with :port if it isn't the Port above. Several: separate them with commas. Example: Join = 203.0.113.7:39500\n"
    "Join =\n"
    "\n"
    "; Small monsters (Jaggi, Aptonoth...) synced between the players in a zone, like large monsters are: On or Off.\n"
    "; Off is the original game: each player sees them in different places. Everyone in the hunt should use the same.\n"
    "SyncSmallMonsters = On\n"
    "\n"
    "[Monster HP]\n"
    "; Large monster HP in Hub quests by the number of hunters on the quest. Each quest sets its monsters' HP as a\n"
    "; multiplier (most Hub quests are made for about two hunters); these factors multiply it: 0.6 = 60% of the\n"
    "; quest's HP, 1 = the original game. Only HP changes: attack, defense, size and the rest stay as they are.\n"
    "; Applies to every quest but the Village ones (played alone anyway): Hub and G Rank, Arena, Special Permit,\n"
    "; Event and Hub Prowler quests. In a hub, everyone uses the host's settings (each game computes the HP itself).\n"
    "1Player = 1.0\n"
    "2Players = 1.0\n"
    "3Players = 1.0\n"
    "4Players = 1.0\n"
    "\n"
    "; One quest set apart: Quest<number> = factors for 1, 2, 3 and 4 hunters (or one factor for all). The log\n"
    "; (log.txt) names the quest number whenever a monster's HP is set, e.g. \"[hpscale] quest 10107, ...\".\n"
    "; Example: Quest10107 = 1.0, 1.0, 1.2, 1.4\n"
    "\n"
    "[Village Quests]\n"
    "; Village quests at the Hub's and the Hunters' Pub's quest counter, to play them together: On or Off. In the\n"
    "; counter's rank menu or star menu, L or R switches between the Hub's quests and the Village quests (the same\n"
    "; list as at a village: what your village progress has unlocked).\n"
    "VillageQuestsInHub = On\n"
    "\n"
    "; Village quests are made for one hunter: posted in the Hub or Pub, their large monsters' HP is multiplied by this\n"
    "; first, then by the [Monster HP] factor for the number of hunters. 1 = as in the village.\n"
    "HubHP = 2.0\n"
    "\n"
    "[Bonus Content]\n"
    "; The free downloads (event and challenge quests, item packs, Palicos) can't be fetched from Capcom's server any\n"
    "; more. On: they are copied from a save that has them (a \"donor\" save file named system, in a folder called bonus\n"
    "; " BONUS_PLACE ") into a save that has never had any downloads, such as a new one. Only the download data\n"
    "; is written, once; saves that already have downloads are left alone. Without a donor save nothing happens.\n"
    "; Off: saves are never changed.\n"
    "BonusContent = On\n";

// Every setting of the file but the per-quest Quest<number> entries (config.h), in file order: what the Settings window
// shows and the menu offers. Values are written into the file as given here.
#define ONOFF "On|Off"
const ConfigSetting g_settings[] = {
    {"Display", "Display", "Mode", "Mode", SETTING_CHOICE, "Borderless|Fullscreen|Windowed",
     "Borderless (full screen)|Fullscreen (exclusive)|Windowed", true, true},
    {"Display", "Display", "Resolution", "Resolution", SETTING_VALUE, "Desktop|1280x720|1600x900|1920x1080|2560x1440|3840x2160",
     NULL, false, true},
    {"Display", "Display", "RenderResolution", "3D Resolution", SETTING_VALUE, "Auto|1280x720|1920x1080|2560x1440|3840x2160",
     NULL, false, true},
    {"Display", "Display", "AspectRatio", "Aspect Ratio", SETTING_VALUE, "Auto|16:9|16:10|21:9|32:9", NULL, false, true},
    {"Display", "Display", "FOV", "Field of View", SETTING_VALUE, "0.8|0.9|1.0|1.1|1.2|1.3|1.5", NULL, true, true},
    {"Display", "Display", "UIAnchor", "HUD on Wide Screens", SETTING_CHOICE, "Center|Edges|21:9|32:9",
     "Center (16:9)|Edges|21:9|32:9", false, true},
    {"Graphics", "Anti-aliasing", "MSAA", "MSAA", SETTING_CHOICE, "Off|2x|4x|8x", NULL, true, true},
    {"Graphics", "Anti-aliasing", "FXAA", "FXAA", SETTING_CHOICE, ONOFF, NULL, true, true},
    {"Graphics", "Upscaler", "FSR", "FSR 3.1 Upscaler", SETTING_CHOICE, ONOFF, NULL, false, true},
    {"Graphics", "Upscaler", "FSRQuality", "FSR Quality", SETTING_CHOICE, "NativeAA|Quality|Balanced|Performance|UltraPerformance",
     "Native AA|Quality (1.5x)|Balanced (1.7x)|Performance (2x)|Ultra Performance (3x)", false, true},
    {"Graphics", "Upscaler", "Sharpness", "FSR Sharpness", SETTING_VALUE, "0|0.4|0.6|0.8|1.0", NULL, false, false},
    {"Graphics", "Upscaler", "ObjectMotion", "FSR Object Motion", SETTING_CHOICE, ONOFF, NULL, false, false},
    {"Graphics", "Upscaler", "Reactive", "FSR Reactive Mask", SETTING_VALUE, "0|0.5|0.7|0.9|1.0", NULL, false, false},
    {"Graphics", "Upscaler", "AnimSurf", "FSR Animated Surfaces", SETTING_CHOICE, ONOFF, NULL, false, false},
    {"Graphics", "Performance", "FPS", "Frame Rate Cap", SETTING_VALUE, "30|60|90|120|144|240", NULL, false, true},
    {"Graphics", "Performance", "VSync", "VSync", SETTING_CHOICE, ONOFF, NULL, false, true},
    {"Controls", "Controls", "FaceButtons", "Face Buttons", SETTING_CHOICE, "Position|Label",
     "By Position (Switch layout)|By Label", true, true},
    {"Controls", "Controls", "BackgroundInput", "Controller in the Background", SETTING_CHOICE, ONOFF, NULL, true, true},
    {"Controls", "Mouse", "MouseSensitivity", "Mouse Sensitivity", SETTING_VALUE, "0.5|0.75|1.0|1.5|2.0|3.0", NULL, true, true},
    {"Multiplayer", "Multiplayer", "Port", "Port", SETTING_VALUE, "39500", NULL, false, false},
    {"Multiplayer", "Multiplayer", "Join", "Join Addresses", SETTING_TEXT, NULL, NULL, true, false},
    {"Multiplayer", "Multiplayer", "SyncSmallMonsters", "Sync Small Monsters", SETTING_CHOICE, ONOFF, NULL, false, true},
    {"Monster HP", "Monster HP", "1Player", "1 Hunter", SETTING_VALUE, "0.5|0.75|1.0|1.25|1.5|2.0", NULL, true, false},
    {"Monster HP", "Monster HP", "2Players", "2 Hunters", SETTING_VALUE, "0.5|0.75|1.0|1.25|1.5|2.0", NULL, true, false},
    {"Monster HP", "Monster HP", "3Players", "3 Hunters", SETTING_VALUE, "0.5|0.75|1.0|1.25|1.5|2.0", NULL, true, false},
    {"Monster HP", "Monster HP", "4Players", "4 Hunters", SETTING_VALUE, "0.5|0.75|1.0|1.25|1.5|2.0", NULL, true, false},
    {"Multiplayer", "Village Quests", "VillageQuestsInHub", "Village Quests in the Hub", SETTING_CHOICE, ONOFF, NULL, true, true},
    {"Multiplayer", "Village Quests", "HubHP", "Village Quest HP in the Hub", SETTING_VALUE, "1.0|1.5|2.0|3.0", NULL, true, false},
    {"Extras", "Bonus Content", "BonusContent", "Bonus Content", SETTING_CHOICE, ONOFF, NULL, false, true},
};
const int g_nsettings = (int)(sizeof g_settings / sizeof g_settings[0]);
// The per-quest Monster HP entries (Quest<number> = factors): set, listed and removed through config_set / config_quests
// / config_remove like the table's.
static const ConfigSetting quest_setting = {"Monster HP", "Monster HP", "Quest", "Quest", SETTING_TEXT, NULL, NULL, true, false};
static char g_start_values[sizeof g_settings / sizeof g_settings[0]][64];  // as the game started (config_load)

static void ini_path(char *buf, size_t size) {
    const char *e = getenv("MHGU_CONFIG");
    if (e && *e) { snprintf(buf, size, "%s", e); return; }
    snprintf(buf, size, "mhgurecomp.ini");  // (started on its own the game runs in its folder, or MHGU_HOME: main.c)
}

// lower case, spaces and '_' / '-' removed: "Borderless Window" == "borderlesswindow"
static void squash(char *s) {
    char *d = s;
    for (; *s; s++) if (!isspace((unsigned char)*s) && *s != '_' && *s != '-') *d++ = (char)tolower((unsigned char)*s);
    *d = 0;
}

static bool parse_size(const char *v, int *w, int *h) {
    int a, b;
    char x;
    if (sscanf(v, "%d %c %d", &a, &x, &b) != 3 || (x != 'x' && x != 'X' && x != '*')) return false;
    if (a < 320 || b < 180 || a > 16384 || b > 16384) return false;
    *w = a, *h = b;
    return true;
}

// [Monster HP] factors: "0.6", "0.6x", or a comma separated list of up to four (squashed: no spaces). The count read,
// 0 when one is not a number in HP_SCALE_MIN..HP_SCALE_MAX.
static int parse_factors(const char *v, float f[4]) {
    int n = 0;
    while (*v && n < 4) {
        char *end;
        float x = strtof(v, &end);
        if (end == v || !(x >= HP_SCALE_MIN && x <= HP_SCALE_MAX)) return 0;
        f[n++] = x;
        v = end + (*end == 'x');
        if (*v == ',') v++;
        else if (*v) return 0;
    }
    return *v ? 0 : n;
}

// The vertical field of view (radians) a camera of this aspect gets (tools/gen.py PATCHES: the three callers of the
// perspective matrix that load a camera's). One made for the screen (its 16:9 aspect replaced with g_render_aspect)
// gets the FOV factor and, on a picture narrower than 16:9 (16:10), the height that keeps 16:9's width (Vert+): more
// above and below instead of less to the sides. Other cameras keep their own.
float camera_fovy(float fovy, float aspect) {
    if (aspect != g_render_aspect) return fovy;
    fovy *= g_fov_scale;
    if (aspect < 16.0f / 9.0f) fovy = 2.0f * atanf(tanf(fovy * 0.5f) * (16.0f / 9.0f) / aspect);
    return fovy;
}

// A picture shape: "Auto" (0), "21:9" / "21/9", or a number (0.5 .. 10).
static bool parse_aspect(const char *v, float *out) {
    float n, d, a;
    char sep, *end;
    if (!strcasecmp(v, "auto")) { *out = 0; return true; }
    if (sscanf(v, "%f %c %f", &n, &sep, &d) == 3 && (sep == ':' || sep == '/') && n > 0 && d > 0) {
        a = n / d;
    } else {
        a = strtof(v, &end);
        if (end == v || *end) return false;
    }
    if (a < 0.5f || a > 10.0f) return false;
    *out = a;
    return true;
}

// UIAnchor: Center (also 1, 16:9), 21:9 (2), 32:9 (3) or Edges, as Config.ui_anchor's UI_*
static bool parse_anchor(const char *v, int *out) {
    if (!strcasecmp(v, "center") || !strcmp(v, "1") || !strcmp(v, "16:9")) *out = UI_CENTER;
    else if (!strcmp(v, "2") || !strcmp(v, "21:9")) *out = UI_21_9;
    else if (!strcmp(v, "3") || !strcmp(v, "32:9")) *out = UI_32_9;
    else if (!strcasecmp(v, "edges")) *out = UI_EDGES;
    else return false;
    return true;
}

// FSRQuality (squashed): NativeAA, Quality, Balanced, Performance, UltraPerformance, as Config.fsr_quality's FSR_*
static bool parse_fsr_quality(const char *v, int *out) {
    if (!strcmp(v, "nativeaa") || !strcmp(v, "native") || !strcmp(v, "aa")) *out = FSR_NATIVE;
    else if (!strcmp(v, "quality")) *out = FSR_QUALITY;
    else if (!strcmp(v, "balanced")) *out = FSR_BALANCED;
    else if (!strcmp(v, "performance")) *out = FSR_PERFORMANCE;
    else if (!strcmp(v, "ultraperformance") || !strcmp(v, "ultraperf")) *out = FSR_ULTRA_PERFORMANCE;
    else return false;
    return true;
}

static int desktop_w, desktop_h;
static bool render_auto = true;

// A setting into g_config; false: not a value it takes (the default stays). line < 0: quiet (config_check).
static bool set_key(const char *key, char *val, int line) {
    char k[64], v[256];
    snprintf(k, sizeof k, "%s", key);
    snprintf(v, sizeof v, "%s", val);
    squash(k);
    squash(v);
    bool ok = true;
    if (!strcmp(k, "mode")) {
        if (!strcmp(v, "fullscreen") || !strcmp(v, "exclusive") || !strcmp(v, "exclusivefullscreen"))
            g_config.display_mode = DISPLAY_FULLSCREEN;
        else if (!strcmp(v, "windowed") || !strcmp(v, "window")) g_config.display_mode = DISPLAY_WINDOWED;
        else if (!strncmp(v, "borderless", 10)) g_config.display_mode = DISPLAY_BORDERLESS;
        else ok = false;
    } else if (!strcmp(k, "resolution")) {
        if (!strcmp(v, "desktop") || !strcmp(v, "auto")) g_config.width = g_config.height = 0;
        else ok = parse_size(v, &g_config.width, &g_config.height);
    } else if (!strcmp(k, "renderresolution")) {
        if (!strcmp(v, "auto")) render_auto = true;
        else if ((ok = parse_size(v, &g_config.render_w, &g_config.render_h))) render_auto = false;
    } else if (!strcmp(k, "aspectratio")) {
        ok = parse_aspect(v, &g_config.aspect_ratio);
    } else if (!strcmp(k, "fov")) {
        float f = strtof(v, NULL);
        if ((ok = f >= 0.5f && f <= 2.0f)) g_config.fov = f;
    } else if (!strcmp(k, "uianchor")) {
        ok = parse_anchor(v, &g_config.ui_anchor);
    } else if (!strcmp(k, "msaa")) {
        if (!strcmp(v, "off") || !strcmp(v, "none") || !strcmp(v, "0") || !strcmp(v, "1") || !strcmp(v, "1x"))
            g_config.msaa = 1;
        else if (!strcmp(v, "2x") || !strcmp(v, "2")) g_config.msaa = 2;
        else if (!strcmp(v, "4x") || !strcmp(v, "4")) g_config.msaa = 4;
        else if (!strcmp(v, "8x") || !strcmp(v, "8")) g_config.msaa = 8;
        else ok = false;
    } else if (!strcmp(k, "backgroundinput")) {
        if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes")) g_config.background_input = 1;
        else if (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no")) g_config.background_input = 0;
        else ok = false;
    } else if (!strcmp(k, "fxaa")) {
        if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes")) g_config.fxaa = 1;
        else if (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no")) g_config.fxaa = 0;
        else ok = false;
    } else if (!strcmp(k, "fsr")) {
        if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes")) g_config.fsr = 1;
        else if (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no")) g_config.fsr = 0;
        else ok = false;
    } else if (!strcmp(k, "upscaler")) {
        if (!strcmp(v, "fsr3") || !strcmp(v, "fsr3.1") || !strcmp(v, "3")) g_config.fsr_upscaler = FSR_UP_3;
        else if (!strcmp(v, "fsr4") || !strcmp(v, "4")) g_config.fsr_upscaler = FSR_UP_4;
        else ok = false;
    } else if (!strcmp(k, "fsrquality")) {
        ok = parse_fsr_quality(v, &g_config.fsr_quality);
    } else if (!strcmp(k, "sharpness")) {
        char *end;
        float f = strtof(v, &end);
        if ((ok = end != v && f >= 0.0f && f <= 1.0f)) g_config.fsr_sharpness = f;
    } else if (!strcmp(k, "objectmotion")) {
        if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes")) g_config.fsr_objmotion = 1;
        else if (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no")) g_config.fsr_objmotion = 0;
        else ok = false;
    } else if (!strcmp(k, "animsurf")) {
        if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes")) g_config.fsr_animsurf = 1;
        else if (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no")) g_config.fsr_animsurf = 0;
        else ok = false;
    } else if (!strcmp(k, "reactive")) {
        char *end;
        float f = strtof(v, &end);
        if ((ok = end != v && f >= 0.0f && f <= 1.0f)) g_config.fsr_reactive = f;
    } else if (!strcmp(k, "vsync")) {
        if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes")) g_config.vsync = 1;
        else if (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no")) g_config.vsync = 0;
        else ok = false;
    } else if (!strcmp(k, "facebuttons")) {
        if (!strcmp(v, "position") || !strcmp(v, "positional") || !strcmp(v, "nintendo"))
            g_config.face_buttons = FACE_POSITION;
        else if (!strcmp(v, "label") || !strcmp(v, "labels") || !strcmp(v, "xbox"))
            g_config.face_buttons = FACE_LABEL;
        else ok = false;
    } else if (!strcmp(k, "mousesensitivity")) {
        float f = strtof(v, NULL);
        if ((ok = f >= 0.1f && f <= 10.0f)) g_config.mouse_sensitivity = f;
    } else if (!strcmp(k, "port")) {
        int p = atoi(v);
        if (p > 0 && p < 65536) g_config.ldn_port = p;
        else ok = false;
    } else if (!strcmp(k, "join")) {
        snprintf(g_config.ldn_join, sizeof g_config.ldn_join, "%s", val);  // (as written: names keep their dashes)
    } else if (!strcmp(k, "bonuscontent")) {
        if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes")) g_config.bonus_content = 1;
        else if (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no")) g_config.bonus_content = 0;
        else ok = false;
    } else if (!strcmp(k, "villagequestsinhub")) {
        if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes")) g_config.village_in_hub = 1;
        else if (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no")) g_config.village_in_hub = 0;
        else ok = false;
    } else if (!strcmp(k, "hubhp")) {
        float f[4];
        if ((ok = parse_factors(v, f) == 1)) g_config.hp.village = f[0];
    } else if (!strcmp(k, "syncsmallmonsters")) {
        if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes")) g_config.sync_small_monsters = 1;
        else if (!strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no")) g_config.sync_small_monsters = 0;
        else ok = false;
    } else if (k[0] >= '1' && k[0] <= '4' && (!strcmp(k + 1, "player") || !strcmp(k + 1, "players"))) {
        float f[4];
        if ((ok = parse_factors(v, f) == 1)) g_config.hp.scale[k[0] - '1'] = f[0];
    } else if (!strncmp(k, "quest", 5) && isdigit((unsigned char)k[5])) {
        HpScale *h = &g_config.hp;
        char *end;
        unsigned long q = strtoul(k + 5, &end, 10);
        float f[4];
        int n = parse_factors(v, f);
        if ((ok = !*end && q && (n == 1 || n == 4))) {
            int i = 0;
            while (i < h->quests && h->quest[i].quest != q) i++;
            if (i == HP_QUESTS_MAX) {
                if (line >= 0) LOG("[config] line %d: more than %d quests set apart\n", line, HP_QUESTS_MAX);
                return false;
            }
            h->quest[i].quest = (uint32_t)q;
            for (int j = 0; j < 4; j++) h->quest[i].scale[j] = f[n == 4 ? j : 0];
            if (i == h->quests) h->quests++;
        }
    } else if (!strcmp(k, "fps") || !strcmp(k, "framerate")) {
        int f = atoi(v);
        if (f >= 30 && f <= 240) g_config.fps = f;
        else ok = false;
    } else {
        if (line >= 0) LOG("[config] line %d: unknown setting \"%s\" (ignored)\n", line, key);
        return false;
    }
    if (!ok && line >= 0) LOG("[config] line %d: \"%s\" is not a value %s takes; using the default\n", line, val, key);
    return ok;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

// The largest size inside w x h matching the given aspect ratio, even numbers.
static void fit_aspect(int w, int h, float aspect, int *rw, int *rh) {
    if (aspect <= 0.0f) aspect = 16.0f / 9.0f;
    if ((float)w / (float)h > aspect)
        w = (int)((float)h * aspect + 0.5f);
    else
        h = (int)((float)w / aspect + 0.5f);
    *rw = w & ~1, *rh = h & ~1;
}

// Sections of DEFAULT_INI the file lacks (written by an older build) are added with their defaults at its end; the
// player's own settings stay as they are.
static void append_missing_sections(const char *path, const char *seen) {
    FILE *o = NULL;
    for (const char *p = strstr(DEFAULT_INI, "\n["); p;) {
        const char *start = p + 1, *end = strstr(start, "\n["), *close = strchr(start, ']');
        p = end;
        char name[64], key[70];
        snprintf(name, sizeof name, "%.*s", (int)(close - start - 1), start + 1);
        for (char *q = name; *q; q++) *q = (char)tolower((unsigned char)*q);
        snprintf(key, sizeof key, "|%s|", name);
        if (strstr(seen, key)) continue;
        if (!o && !(o = fopen(path, "a"))) return;
        fprintf(o, "\n%.*s", (int)(end ? end - start : (long)strlen(start)), start);
        LOG("[config] added the [%s] section to %s\n", name, path);
    }
    if (o) fclose(o);
}

void config_load(void) {
    char path[1100];
    ini_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) {
        FILE *o = fopen(path, "w");
        if (o) { fputs(DEFAULT_INI, o); fclose(o); LOG("[config] wrote the default settings to %s\n", path); }
        else LOG("[config] %s is missing and could not be written; using the defaults\n", path);
    } else {
        char buf[512], seen[512] = "|";
        int line = 0;
        while (fgets(buf, sizeof buf, f)) {
            line++;
            char *s = trim(buf);
            if (*s == '[' && strchr(s, ']') && strlen(seen) + strlen(s) + 2 < sizeof seen) {
                for (char *q = s + 1; *q != ']'; q++) {
                    size_t n = strlen(seen);
                    seen[n] = (char)tolower((unsigned char)*q), seen[n + 1] = 0;
                }
                strcat(seen, "|");
            }
            if (!*s || *s == ';' || *s == '#' || *s == '[') continue;
            char *eq = strchr(s, '=');
            if (!eq) { LOG("[config] line %d: no '=' (ignored)\n", line); continue; }
            *eq = 0;
            char *val = trim(eq + 1), *cmt = strpbrk(val, ";#");
            if (cmt) *cmt = 0;
            set_key(trim(s), trim(val), line);
        }
        fclose(f);
        append_missing_sections(path, seen);
    }

    const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    // (in pixels: a mode's size is in points where pixel_density is above 1, as on a Retina Mac)
    float density = dm && dm->pixel_density > 0.0f ? dm->pixel_density : 1.0f;
    desktop_w = dm ? (int)(dm->w * density + 0.5f) : 1920, desktop_h = dm ? (int)(dm->h * density + 0.5f) : 1080;
    if (!g_config.width) g_config.width = desktop_w, g_config.height = desktop_h;

    const char *e = getenv("MHGU_WINDOW");
    int w, h;
    if (e && parse_size(e, &w, &h)) g_config.display_mode = DISPLAY_WINDOWED, g_config.width = w, g_config.height = h;
    if (getenv("MHGU_HIDDEN")) g_config.display_mode = DISPLAY_WINDOWED;
    if ((e = getenv("MHGU_MSAA"))) g_config.msaa = atoi(e) >= 8 ? 8 : atoi(e) >= 4 ? 4 : atoi(e) >= 2 ? 2 : 1;
    if ((e = getenv("MHGU_FXAA"))) g_config.fxaa = atoi(e) != 0;
    if ((e = getenv("MHGU_BACKGROUND_INPUT"))) g_config.background_input = atoi(e) != 0;
    if ((e = getenv("MHGU_PAD_POSITIONAL"))) g_config.face_buttons = atoi(e) ? FACE_POSITION : FACE_LABEL;
    if ((e = getenv("MHGU_LDN_PORT")) && atoi(e) > 0) g_config.ldn_port = atoi(e);
    if ((e = getenv("MHGU_SYNC_ZAKO"))) g_config.sync_small_monsters = atoi(e) != 0;
    if ((e = getenv("MHGU_LDN_JOIN"))) snprintf(g_config.ldn_join, sizeof g_config.ldn_join, "%s", e);
    if ((e = getenv("MHGU_ASPECT"))) parse_aspect(e, &g_config.aspect_ratio);
    if ((e = getenv("MHGU_FOV")) && strtof(e, NULL) >= 0.5f && strtof(e, NULL) <= 2.0f) g_config.fov = strtof(e, NULL);
    if ((e = getenv("MHGU_UI_ANCHOR"))) parse_anchor(e, &g_config.ui_anchor);
    e = getenv("MHGU_RENDER");
    if (e && parse_size(e, &w, &h)) render_auto = false, g_config.render_w = w, g_config.render_h = h;
    if ((e = getenv("MHGU_FSR"))) g_config.fsr = atoi(e) != 0;  // (test runs: MHGU_FSR=0|1, MHGU_FSR_QUALITY=quality...)
    if ((e = getenv("MHGU_FSR_UPSCALER"))) g_config.fsr_upscaler = !strcmp(e, "4") || !strcasecmp(e, "fsr4") ? FSR_UP_4 : FSR_UP_3;
    if ((e = getenv("MHGU_FSR_QUALITY"))) {
        char q[32];
        snprintf(q, sizeof q, "%s", e);
        squash(q);
        parse_fsr_quality(q, &g_config.fsr_quality);
    }
    if ((e = getenv("MHGU_FSR_OBJMOTION"))) g_config.fsr_objmotion = atoi(e) != 0;
    if ((e = getenv("MHGU_FSR_ANIMSURF"))) g_config.fsr_animsurf = atoi(e) != 0;
    if ((e = getenv("MHGU_FSR_SHARPNESS")) && strtof(e, NULL) >= 0.0f && strtof(e, NULL) <= 1.0f) g_config.fsr_sharpness = strtof(e, NULL);
    if ((e = getenv("MHGU_FSR_REACTIVE")) && strtof(e, NULL) >= 0.0f && strtof(e, NULL) <= 1.0f) g_config.fsr_reactive = strtof(e, NULL);

    int tw = g_config.width, th = g_config.height;  // (Borderless shows the image at the desktop size)
    if (g_config.display_mode == DISPLAY_BORDERLESS) tw = tw < desktop_w ? tw : desktop_w, th = th < desktop_h ? th : desktop_h;
    // The picture's shape: the window's or AspectRatio's, at least 16:10. The game lays its HUD out for 16:9, which
    // still fits down to 16:10 (a little flatter, nvn/vulkan/exec.c), so a narrower window (4:3) shows 16:10 with bars.
    // Wider than 16:9 shows more to the sides, 16:9 to 16:10 more above and below (camera_fovy, gen.py PATCHES).
    float aspect = g_config.aspect_ratio > 0 ? g_config.aspect_ratio : (float)tw / (float)th;
    if (aspect < 1.6f - 0.005f) {
        if (g_config.aspect_ratio > 0)
            LOG("[config] AspectRatio %.3f is narrower than 16:10, which the HUD needs: using 16:10\n", aspect);
        aspect = 1.6f;
    }
    if (fabsf(aspect - 16.0f / 9.0f) < 0.01f) aspect = 16.0f / 9.0f;  // (1366x768 and the like)
    if (render_auto && g_config.fsr && g_config.display_mode == DISPLAY_BORDERLESS && (tw < desktop_w || th < desktop_h)) {
        // FSR, Borderless: it upscales straight to the size the picture is shown at (the screen, in the picture's shape)
        // instead of to Resolution, which the window would then stretch again (blurring what FSR rebuilt)
        fit_aspect(desktop_w, desktop_h, aspect, &w, &h);
        LOG("[config] FSR, Borderless: upscaling to %dx%d (the screen, in the picture's shape) instead of Resolution %dx%d\n",
            w, h, tw, th);
    } else if (render_auto) {
        fit_aspect(tw, th, aspect, &w, &h);
    } else {
        fit_aspect(g_config.render_w, g_config.render_h, aspect, &w, &h);
        if (w != g_config.render_w || h != g_config.render_h)
            LOG("[config] RenderResolution %dx%d is not the picture's shape (%.3f): using %dx%d\n", g_config.render_w,
                g_config.render_h, aspect, w, h);
    }
    if (g_config.fsr) {  // FSR: that size is its output; the game renders smaller (runtime/nvn/vulkan/fsr.c)
        static const float ratio[] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
        static const char *const names[] = {"NativeAA", "Quality", "Balanced", "Performance", "UltraPerformance"};
        g_fsr_output[0] = (uint32_t)w, g_fsr_output[1] = (uint32_t)h;
        float r = ratio[g_config.fsr_quality];
        fit_aspect((int)(w / r + 0.5f), (int)(h / r + 0.5f), aspect, &w, &h);
        if (g_config.msaa > 1) LOG("[config] FSR is on: MSAA off (FSR does the anti-aliasing)\n");
        if (g_config.fxaa) LOG("[config] FSR is on: FXAA off (FSR does the anti-aliasing)\n");
        g_config.msaa = 1, g_config.fxaa = 0;
        // textures are sampled for the output size (AMD's recommendation: log2(render / output)); MHGU_FSR_LODBIAS
        // overrides (A/B)
        g_fsr_lod_bias = log2f((float)w / (float)g_fsr_output[0]);
        if ((e = getenv("MHGU_FSR_LODBIAS"))) g_fsr_lod_bias = strtof(e, NULL);
        LOG("[config] FSR %s %s (x%.1f): the 3D picture at %dx%d, upscaled to %ux%u, sharpness %.2f, texture LOD bias "
            "%.2f\n", g_config.fsr_upscaler == FSR_UP_4 ? "4 (3.1 if it cannot start)" : "3.1", names[g_config.fsr_quality], r, w, h, g_fsr_output[0], g_fsr_output[1], g_config.fsr_sharpness,
            g_fsr_lod_bias);
    }
    g_config.render_w = w, g_config.render_h = h;
    g_render_size[0] = (uint32_t)w;
    g_render_size[1] = (uint32_t)h;
    g_render_aspect = aspect;
    g_fov_scale = g_config.fov;

    // The HUD's width: 16:9 (Center), 21:9, 32:9 or the picture's (Edges), at most the picture's; the game's 2D canvas
    // gets that width (gen.py PATCHES), exec.c places the HUD's parts in it.
    float ui = g_config.ui_anchor == UI_EDGES  ? aspect
             : g_config.ui_anchor == UI_32_9 ? 32.0f / 9.0f
             : g_config.ui_anchor == UI_21_9 ? 21.0f / 9.0f
                                             : 16.0f / 9.0f;
    g_ui_anchor_aspect = ui < aspect ? ui : aspect;
    g_ui_size[1] = 720;
    g_ui_size[0] = (uint32_t)(720.0f * g_ui_anchor_aspect + 0.5f) & ~1u;
    if (g_ui_size[0] < 1280) g_ui_size[0] = 1280;

    static const char *const modes[] = {"windowed", "fullscreen", "borderless"};
    LOG("[config] %s: %s %dx%d, 3D %dx%d (shape %.3f, FOV x%.2f, HUD %.3f wide), %d fps, vsync %s, MSAA %dx, FXAA %s, "
        "face buttons by %s, controller %s in the background\n", path, modes[g_config.display_mode], g_config.width,
        g_config.height, g_config.render_w, g_config.render_h, g_render_aspect, g_fov_scale, g_ui_anchor_aspect,
        g_config.fps, g_config.vsync ? "on" : "off",
        g_config.msaa, g_config.fxaa ? "on" : "off", g_config.face_buttons == FACE_LABEL ? "label" : "position",
        g_config.background_input ? "on" : "off");
    const HpScale *hp = &g_config.hp;
    LOG("[config] monster HP in Hub quests: x%.2f / %.2f / %.2f / %.2f for 1-4 hunters, %d quest(s) set apart\n",
        hp->scale[0], hp->scale[1], hp->scale[2], hp->scale[3], hp->quests);
    LOG("[config] Village quests at the Hub counter: %s (their HP x%.2f first)\n", g_config.village_in_hub ? "on" : "off",
        hp->village);
    for (int i = 0; i < g_nsettings; i++) config_get(g_settings[i].key, g_start_values[i], sizeof g_start_values[i]);
}

// ---- the settings menu's side (config.h)
void config_path(char *buf, size_t size) { ini_path(buf, size); }

// Is this line "<key> = ..." (any spacing and case)? Its value then, trimmed, without a comment.
static bool line_value(const char *line, const char *key, char *val, size_t size) {
    char k[64], want[64];
    const char *eq = strchr(line, '=');
    const char *s = line;
    while (isspace((unsigned char)*s)) s++;
    if (!eq || *s == ';' || *s == '#' || *s == '[' || (size_t)(eq - s) >= sizeof k) return false;
    snprintf(k, sizeof k, "%.*s", (int)(eq - s), s);
    snprintf(want, sizeof want, "%s", key);
    squash(k);
    squash(want);
    if (strcmp(k, want)) return false;
    char v[256];
    snprintf(v, sizeof v, "%s", eq + 1);
    char *c = strpbrk(v, ";#\r\n");
    if (c) *c = 0;
    snprintf(val, size, "%s", trim(v));
    return true;
}

bool config_get(const char *key, char *buf, size_t size) {
    char path[1100], line[512];
    config_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    bool found = false;
    while (f && !found && fgets(line, sizeof line, f)) found = line_value(line, key, buf, size);
    if (f) fclose(f);
    for (const char *p = DEFAULT_INI; !found && *p;) {  // (not in the file: the default)
        const char *e = strchr(p, '\n');
        snprintf(line, sizeof line, "%.*s", (int)(e ? e - p : (long)strlen(p)), p);
        found = line_value(line, key, buf, size);
        p = e ? e + 1 : p + strlen(p);
    }
    return found;
}

// What the parser makes of a value for a key: g_config (and render_auto) with it set, into *out; g_config unchanged.
static bool parse_into(const char *key, const char *value, Config *out, bool *ra_out) {
    Config saved = g_config;
    bool ra = render_auto;
    char v[256];
    snprintf(v, sizeof v, "%s", value);
    bool ok = set_key(key, v, -1);
    *out = g_config, *ra_out = render_auto;
    g_config = saved;
    render_auto = ra;
    return ok;
}

bool config_same_value(const char *key, const char *a, const char *b) {
    char x[128], y[128], *ex, *ey;
    snprintf(x, sizeof x, "%s", a);
    snprintf(y, sizeof y, "%s", b);
    squash(x);
    squash(y);
    if (!strcmp(x, y)) return true;
    double dx = strtod(x, &ex), dy = strtod(y, &ey);  // (numbers: 1.0 == 1)
    if (ex != x && ey != y && !*ex && !*ey) return dx == dy;
    // (other spellings of one value, such as UIAnchor 1 for Center or MSAA 1x for Off: what the parser makes of them)
    Config ca, cb;
    bool ra, rb;
    return key && parse_into(key, a, &ca, &ra) && parse_into(key, b, &cb, &rb) && ra == rb && !memcmp(&ca, &cb, sizeof ca);
}

bool config_restart_pending(const char *key) {
    char now[64];
    for (int i = 0; i < g_nsettings; i++) {
        if (key ? strcmp(g_settings[i].key, key) : g_settings[i].live) continue;
        if (config_get(g_settings[i].key, now, sizeof now) && !config_same_value(g_settings[i].key, now, g_start_values[i])) return true;
    }
    return false;
}

static bool is_quest_key(const char *key) {
    return !strncasecmp(key, "quest", 5) && isdigit((unsigned char)key[5]) && strspn(key + 5, "0123456789") == strlen(key + 5);
}

static const ConfigSetting *setting_of(const char *key) {
    for (int i = 0; i < g_nsettings; i++) if (!strcmp(g_settings[i].key, key)) return &g_settings[i];
    return is_quest_key(key) ? &quest_setting : NULL;
}

bool config_live(const char *key) {
    const ConfigSetting *st = setting_of(key);
    return st && st->live;
}

bool config_check(const char *key, const char *value) {
    const ConfigSetting *st = setting_of(key);
    if (!st) return false;
    // (the parser drops dashes - "Borderless-Window" - so "-1" would read as 1: no dash in a number or a size)
    if ((st->kind == SETTING_VALUE || st == &quest_setting) && strchr(value, '-')) return false;
    Config parsed;  // (the parser is the judge)
    bool ra;
    return parse_into(key, value, &parsed, &ra);
}

bool config_help(const char *key, char *buf, size_t size) {
    // The comment lines right above "<key> =" in DEFAULT_INI; for a key it has no line for (Quest<number>), the
    // comment block that names it.
    char block[2048] = "", line[512], val[256];
    size_t n = 0;
    buf[0] = 0;
    for (const char *p = DEFAULT_INI; *p;) {
        const char *e = strchr(p, '\n');
        snprintf(line, sizeof line, "%.*s", (int)(e ? e - p : (long)strlen(p)), p);
        p = e ? e + 1 : p + strlen(p);
        if (line[0] == ';') {
            const char *c = line + 1;
            while (*c == ' ') c++;  // (and a continuation line's indent)
            n += snprintf(block + n, n < sizeof block ? sizeof block - n : 0, "%s%s", n ? " " : "", c);
            if (n >= sizeof block) n = sizeof block - 1;
            continue;
        }
        if (line_value(line, key, val, sizeof val) || (block[0] && strstr(block, key) && !strcmp(key, "Quest"))) {
            snprintf(buf, size, "%s", block);
            return block[0] != 0;
        }
        block[0] = 0, n = 0;
    }
    return false;
}

void config_quests(void (*fn)(unsigned quest, const char *value, void *ctx), void *ctx) {
    char path[1100], line[512], key[64], val[256];
    config_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    while (f && fgets(line, sizeof line, f)) {
        const char *s = line, *eq = strchr(line, '=');
        while (isspace((unsigned char)*s)) s++;
        if (!eq || *s == ';' || *s == '#' || (size_t)(eq - s) >= sizeof key) continue;
        snprintf(key, sizeof key, "%.*s", (int)(eq - s), s);
        snprintf(key, sizeof key, "%s", trim(key));
        if (is_quest_key(key) && line_value(line, key, val, sizeof val)) fn((unsigned)strtoul(key + 5, NULL, 10), val, ctx);
    }
    if (f) fclose(f);
}

bool config_remove(const char *key) {
    if (!is_quest_key(key)) return false;
    char path[1100], tmp[1200], line[512], val[256];
    config_path(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(path, "r"), *o = fopen(tmp, "w");
    if (!f || !o) { if (f) fclose(f); if (o) fclose(o); return false; }
    while (fgets(line, sizeof line, f))
        if (!line_value(line, key, val, sizeof val)) fputs(line, o);
    fclose(f);
    if (fclose(o) != 0 || rename(tmp, path) != 0) { remove(tmp); return false; }
    HpScale *h = &g_config.hp;  // (and from the running game's list)
    unsigned long q = strtoul(key + 5, NULL, 10);
    for (int i = 0; i < h->quests; i++)
        if (h->quest[i].quest == q) { h->quest[i] = h->quest[--h->quests]; break; }
    LOG("[config] %s removed\n", key);
    return true;
}

bool config_set(const char *key, const char *value) {
    const ConfigSetting *st = setting_of(key);
    if (!st || !config_check(key, value)) return false;
    const char *name = st == &quest_setting ? key : st->key;  // (the name written for a new line)
    char path[1100], tmp[1200], line[512], old[256];
    config_path(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(path, "r"), *o = fopen(tmp, "w");
    if (!o) { if (f) fclose(f); return false; }
    bool done = false, in_section = false;
    int blanks = 0;  // (blank lines held back: a key added at a section's end goes before them)
    char section[64];
    snprintf(section, sizeof section, "[%s]", st->section);
    while (f && fgets(line, sizeof line, f)) {
        const char *s = line;
        while (isspace((unsigned char)*s)) s++;
        if (!*s) { blanks++; continue; }
        if (!done && in_section && *s == '[') {  // (the key is missing from its section: add it at the section's end)
            fprintf(o, "%s = %s\n", name, value);
            done = true;
        }
        for (; blanks; blanks--) fputs("\n", o);
        if (*s == '[') in_section = !strncasecmp(s, section, strlen(section));
        if (!done && line_value(line, key, old, sizeof old)) {  // (the key's spelling and the line's comment stay)
            const char *eq = strchr(line, '='), *cmt = strpbrk(eq, ";#");
            int cl = cmt ? (int)strcspn(cmt, "\r\n") : 0;
            fprintf(o, "%.*s= %s%s%.*s\n", (int)(eq - line), line, value, cl ? " " : "", cl, cmt ? cmt : "");
            done = true;
            continue;
        }
        fputs(line, o);
    }
    if (!done) fprintf(o, "%s%s\n%s = %s\n", in_section ? "" : "\n", in_section ? "" : section, name, value);
    for (; blanks; blanks--) fputs("\n", o);
    if (f) fclose(f);
    if (fclose(o) != 0 || rename(tmp, path) != 0) { remove(tmp); return false; }
    LOG("[config] %s = %s%s\n", name, value, st->live ? "" : " (at the next start)");
    if (st->live) {
        char v[256];
        snprintf(v, sizeof v, "%s", value);
        set_key(name, v, 0);
        g_fov_scale = g_config.fov;  // (derived at load)
    }
    return true;
}

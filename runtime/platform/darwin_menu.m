// The Settings menu in the macOS menu bar (plat_menu_init, from runtime/ui.c once the window exists), and the actions
// it shares with the Settings window (darwin_ui.h, runtime/platform/darwin_settings.m).
//
// "Settings..." (Cmd-,) opens the window, which has every setting. The menu offers the common ones (the settings
// table's menu entries: runtime/config.c g_settings) for a quick change, even in full screen: a submenu per ini
// section, one per setting with its choices, the current one checked. Choosing writes mhgurecomp.ini (config_set); a
// live setting takes effect at once (runtime/ui.c ui_setting_changed), the others are marked until "Restart to
// Apply". Below: the settings file, and the folders the game keeps its files in (the settings folder is the game's
// home: mods/, cheats/, log.txt, caches).
#import <Cocoa/Cocoa.h>

#include <SDL3/SDL_events.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>

#include "../config.h"
#include "../platform.h"
#include "darwin_ui.h"

extern const char *g_save_dir;  // (runtime/main.c)
void ui_setting_changed(const char *key);  // (runtime/ui.c)

@interface MHGUSettingsMenu : NSObject <NSMenuDelegate, NSMenuItemValidation>
@end

static MHGUSettingsMenu *g_menu_target;
static char g_home[PATH_MAX];  // the game's home folder (plat_standalone made it the current folder)

static NSString *str(const char *s) { return [NSString stringWithUTF8String:s ? s : ""]; }

// The choice values / labels of a setting ('|' between them).
static NSArray<NSString *> *split(const char *s) {
    return s ? [str(s) componentsSeparatedByString:@"|"] : @[];
}

// This screen's size in pixels, below the camera housing where it has one (full screen windows stay below it).
const char *plat_screen_size(void) {
    static char buf[32];
    NSScreen *sc = NSScreen.mainScreen;
    if (!sc) return NULL;
    CGFloat h = sc.frame.size.height;
    if (@available(macOS 12.0, *)) h -= sc.safeAreaInsets.top;
    CGFloat k = sc.backingScaleFactor;
    snprintf(buf, sizeof buf, "%ldx%ld", lround(sc.frame.size.width * k) & ~1L, lround(h * k) & ~1L);
    return buf;
}
static NSString *screen_size(void) { return plat_screen_size() ? str(plat_screen_size()) : nil; }

const char *plat_home(void) { return g_home; }

@implementation MHGUSettingsMenu

- (void)choose:(NSMenuItem *)item {
    NSArray<NSString *> *kv = item.representedObject;
    const char *key = kv[0].UTF8String, *value = kv[1].UTF8String;
    if (!config_set(key, value)) { NSBeep(); return; }
    if (config_live(key)) ui_setting_changed(key);
    plat_settings_refresh();
}

// Checkmarks, and each setting's title with its current value, as a menu opens.
- (void)menuNeedsUpdate:(NSMenu *)menu {
    for (NSMenuItem *item in menu.itemArray) {
        if (item.action == @selector(choose:)) {
            NSArray<NSString *> *kv = item.representedObject;
            char now[256] = "";
            config_get(kv[0].UTF8String, now, sizeof now);
            item.state = config_same_value(now, kv[1].UTF8String) ? NSControlStateValueOn : NSControlStateValueOff;
        } else if ([item.representedObject isKindOfClass:NSNumber.class]) {  // (a setting: its index)
            const ConfigSetting *st = &g_settings[[item.representedObject intValue]];
            char now[256] = "";
            config_get(st->key, now, sizeof now);
            NSString *label = str(now);
            NSArray<NSString *> *values = split(st->choices), *labels = split(st->labels);
            for (NSUInteger i = 0; i < values.count; i++)
                if (config_same_value(now, values[i].UTF8String) && i < labels.count) label = labels[i];
            bool pending = !st->live && config_restart_pending(st->key);
            item.title = [NSString stringWithFormat:@"%@: %@%@", str(st->title), label, pending ? @"  (after restart)" : @""];
        }
    }
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(restart:)) return config_restart_pending(NULL);
    return YES;
}

- (void)restart:(id)sender { (void)sender; plat_restart_game(); }
- (void)showSettings:(id)sender { (void)sender; plat_settings_show(); }
@end

void plat_restart_game(void) {
    // A shell waits for this process to end, then starts the game again: the app through LaunchServices, a bare
    // executable directly (with this environment). Then the window closes as from Quit.
    char exe[PATH_MAX], cmd[3 * PATH_MAX];
    if (!plat_exe_path(exe, sizeof exe)) { NSBeep(); return; }
    char *app = strstr(exe, ".app/Contents/MacOS/");
    if (app) {
        app[4] = 0;
        snprintf(cmd, sizeof cmd, "while kill -0 %d 2>/dev/null; do sleep 0.2; done; open -n '%s'", (int)getpid(), exe);
    } else {
        snprintf(cmd, sizeof cmd, "while kill -0 %d 2>/dev/null; do sleep 0.2; done; exec '%s'", (int)getpid(), exe);
    }
    extern char **environ;
    char *argv[] = {(char *)"/bin/sh", (char *)"-c", cmd, NULL};
    pid_t pid;
    if (posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ) != 0) { NSBeep(); return; }
    SDL_Event e = {.type = SDL_EVENT_QUIT};
    SDL_PushEvent(&e);
}

@implementation MHGUSettingsMenu (Folders)

static void open_path(NSString *path, bool create) {
    if (create) [NSFileManager.defaultManager createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:nil];
    [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:path]];
}
void plat_open_home(void) { open_path(str(g_home), false); }
- (void)openHome:(id)sender { (void)sender; plat_open_home(); }
- (void)openMods:(id)sender { (void)sender; open_path([str(g_home) stringByAppendingPathComponent:@"mods"], true); }
- (void)openCheats:(id)sender { (void)sender; open_path([str(g_home) stringByAppendingPathComponent:@"cheats"], true); }
- (void)openSaves:(id)sender { (void)sender; open_path(str(g_save_dir), true); }
- (void)showLog:(id)sender {
    (void)sender;
    NSString *log = [str(g_home) stringByAppendingPathComponent:@"log.txt"];
    [NSWorkspace.sharedWorkspace selectFile:log inFileViewerRootedAtPath:@""];
}
- (void)editSettings:(id)sender { (void)sender; plat_edit_settings(); }
@end

void plat_edit_settings(void) {
    char path[PATH_MAX];
    config_path(path, sizeof path);
    NSURL *file = [NSURL fileURLWithPath:str(path) relativeToURL:[NSURL fileURLWithPath:str(g_home) isDirectory:YES]];
    NSURL *editor = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:@"com.apple.TextEdit"];
    if (editor)  // (a .ini has no app of its own on a fresh Mac)
        [NSWorkspace.sharedWorkspace openURLs:@[ file ] withApplicationAtURL:editor
                                configuration:[NSWorkspaceOpenConfiguration configuration] completionHandler:nil];
    else
        [NSWorkspace.sharedWorkspace openURL:file];
}

static NSMenuItem *action_item(NSString *title, SEL action, NSString *key) {
    NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key ? key : @""];
    it.target = g_menu_target;
    return it;
}

void plat_menu_init(void) {
    @autoreleasepool {
        if (!getcwd(g_home, sizeof g_home)) g_home[0] = 0;
        g_menu_target = [MHGUSettingsMenu new];
        NSMenu *bar = NSApp.mainMenu;
        if (!bar) return;
        NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Settings"];
        menu.delegate = g_menu_target;
        NSMenu *section_menu = nil;
        const char *section = "";
        [menu addItem:action_item(@"Settings…", @selector(showSettings:), @",")];
        [menu addItem:NSMenuItem.separatorItem];
        for (int i = 0; i < g_nsettings; i++) {
            const ConfigSetting *st = &g_settings[i];
            if (!st->menu) continue;
            if (strcmp(st->section, section)) {  // (a new ini section: a submenu of its own)
                section = st->section;
                section_menu = [[NSMenu alloc] initWithTitle:str(section)];
                section_menu.delegate = g_menu_target;
                NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:str(section) action:nil keyEquivalent:@""];
                it.submenu = section_menu;
                [menu addItem:it];
            }
            NSMenu *choices = [[NSMenu alloc] initWithTitle:str(st->title)];
            choices.delegate = g_menu_target;
            NSMutableArray<NSString *> *values = [split(st->choices) mutableCopy], *labels = [split(st->labels) mutableCopy];
            if (labels.count != values.count) labels = [values mutableCopy];
            NSString *here = screen_size();
            if (!strcmp(st->key, "Resolution") && here && ![values containsObject:here]) {  // (this screen's own)
                [values insertObject:here atIndex:1];
                [labels insertObject:[NSString stringWithFormat:@"This Screen (%@)", here] atIndex:1];
            }
            for (NSUInteger c = 0; c < values.count; c++) {
                NSMenuItem *it = action_item(labels[c], @selector(choose:), nil);
                it.representedObject = @[ str(st->key), values[c] ];
                [choices addItem:it];
            }
            NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:str(st->title) action:nil keyEquivalent:@""];
            it.representedObject = @(i);
            it.submenu = choices;
            [section_menu addItem:it];
        }
        [menu addItem:NSMenuItem.separatorItem];
        [menu addItem:action_item(@"Restart to Apply Changes", @selector(restart:), nil)];
        [menu addItem:action_item(@"Edit Settings File…", @selector(editSettings:), nil)];
        [menu addItem:NSMenuItem.separatorItem];
        [menu addItem:action_item(@"Open Settings Folder", @selector(openHome:), nil)];
        [menu addItem:action_item(@"Open Mods Folder", @selector(openMods:), nil)];
        [menu addItem:action_item(@"Open Cheats Folder", @selector(openCheats:), nil)];
        [menu addItem:action_item(@"Open Saves Folder", @selector(openSaves:), nil)];
        [menu addItem:action_item(@"Show Log in Finder", @selector(showLog:), nil)];
        NSMenuItem *top = [[NSMenuItem alloc] initWithTitle:@"Settings" action:nil keyEquivalent:@""];
        top.submenu = menu;
        NSInteger at = bar.numberOfItems > 1 ? 1 : bar.numberOfItems;  // (after the app menu)
        [bar insertItem:top atIndex:at];
    }
}

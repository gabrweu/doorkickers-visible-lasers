/*
 * dk2ml.h - Door Kickers 2 Modloader, plugin API.
 *
 * A plugin is a 64-bit DLL that exports DK2ML_PluginInit, in one of:
 *   <enabled mod folder>\native\*.dll   loaded while the mod is enabled in the in-game Mods menu
 *   <game folder>\mods_native\*.dll     always loaded
 * Other DLLs there are dependencies (found in the plugin's folder). A plugin file name already loaded from another mod
 * is skipped.
 *
 * Required export:
 *   int DK2ML_PluginInit(const DK2ML_API* api, const DK2ML_PluginInfo* info);   // 0 = success
 *
 * Init runs on the main thread before any game code, so no game objects exist yet. Install hooks there. Game names
 * resolve from the shipped DoorKickers2.pdb.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#define DK2ML_INLINE static inline
#else
#define DK2ML_INLINE static __inline
#endif

#define DK2ML_API_VERSION 3

typedef enum DK2ML_Status {
    DK2ML_OK = 0,
    DK2ML_ERROR = 1,
} DK2ML_Status;

/* --- safe hooks ---
 * The game is built with LTCG: callers keep values in ABI-volatile registers (xmm2-5, r8-r11, ...) across calls, and a
 * plain detour clobbers them. A safe hook saves every register around its callbacks and runs the original as the
 * caller set it up. Use safe hooks on all game functions.
 *
 * Chains: pres run in creation order, posts in reverse. A pre returning DK2ML_SKIP_ORIGINAL ends the chain: later
 * callbacks and the skipper's own post don't run; earlier callbacks' posts run at once with the skipper's result.
 * scratch[] is per callback. EnableHook/DisableHook switch only the calling plugin's callbacks. A pre that ran gets
 * its post for that call, even if disabled in between.
 *
 * Nesting: posts are tracked per thread up to 64 hooked calls deep. Deeper, callbacks with a post are skipped entirely
 * (pre too); pre-only callbacks still run.
 *
 * Identical code folding: a hook on a folded address (small getters, trivial virtuals, ...) runs for every function
 * folded into it; the loader logs their names. Check the arguments (e.g. rcx) if that matters.
 *
 * Crash containment: a crashing callback is caught, the call continues as if unhooked, and all of the plugin's hooks
 * pass through from then on. Same if DK2ML_PluginInit crashes or returns non-zero. Not covered: the plugin's own
 * threads, damage done before the crash (e.g. heap corruption), and a crash in a game function the callback calls
 * while a post-hooked call (any plugin's) is between them on the stack. */

typedef struct DK2ML_Xmm {
    uint64_t lo, hi;
} DK2ML_Xmm;

/* All registers on entry (pre) or as the original left them (post). Changes reach the original or the caller. */
typedef struct DK2ML_Regs {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14, r15, rflags;
    DK2ML_Xmm xmm[16];
    /* pre: stack[0] return address, [1..4] home space of args 0-3, [5] arg 4 (DK2ML_Arg n = 4), [6] arg 5, ...
     * A float is its bit pattern in the low 32 bits. */
    uint64_t* stack;
    /* per callback; pre's writes reach post for the same call */
    uint64_t scratch[4];
} DK2ML_Regs;

#define DK2ML_CALL_ORIGINAL 0
#define DK2ML_SKIP_ORIGINAL 1 /* return to the caller with the registers in DK2ML_Regs (rax/xmm0 = result) */

typedef int (*DK2ML_PreFn)(DK2ML_Regs* regs, void* user);
typedef void (*DK2ML_PostFn)(DK2ML_Regs* regs, void* user);

/* Argument n (0 = first; `this` is 0) in a pre callback. Integers/pointers 0-3: rcx, rdx, r8, r9. Floats/doubles 0-3:
 * xmm0-3 (DK2ML_ArgFloat/DK2ML_ArgDouble). 4+: stack. Under 64 bits only the low bytes are defined, so cast:
 * (int)DK2ML_Arg(r, 1), (uint8_t)DK2ML_Arg(r, 2) != 0 for a bool. dk2ml::Arg<T> (dk2ml.hpp) does this. */
DK2ML_INLINE uint64_t DK2ML_Arg(const DK2ML_Regs* r, int n)
{
    switch (n) {
    case 0: return r->rcx;
    case 1: return r->rdx;
    case 2: return r->r8;
    case 3: return r->r9;
    default: return r->stack[n + 1];
    }
}

/* Sets argument n for the original (pre). Stack arguments are written in place. */
DK2ML_INLINE void DK2ML_SetArg(DK2ML_Regs* r, int n, uint64_t value)
{
    switch (n) {
    case 0: r->rcx = value; break;
    case 1: r->rdx = value; break;
    case 2: r->r8 = value; break;
    case 3: r->r9 = value; break;
    default: r->stack[n + 1] = value; break;
    }
}

DK2ML_INLINE float DK2ML_ArgFloat(const DK2ML_Regs* r, int n)
{
    uint32_t bits = (uint32_t)(n < 4 ? r->xmm[n].lo : r->stack[n + 1]);

    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

DK2ML_INLINE void DK2ML_SetArgFloat(DK2ML_Regs* r, int n, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));

    if (n < 4) {
        r->xmm[n].lo = (r->xmm[n].lo & 0xFFFFFFFF00000000ull) | bits;
    } else {
        r->stack[n + 1] = bits;
    }
}

DK2ML_INLINE double DK2ML_ArgDouble(const DK2ML_Regs* r, int n)
{
    uint64_t bits = n < 4 ? r->xmm[n].lo : r->stack[n + 1];

    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

/* Result: rax (integers/pointers) or xmm0 (floats/doubles). Read in post; set in post or a skipping pre. */
DK2ML_INLINE void DK2ML_SetResult(DK2ML_Regs* r, uint64_t value)
{
    r->rax = value;
}

DK2ML_INLINE float DK2ML_ResultFloat(const DK2ML_Regs* r)
{
    uint32_t bits = (uint32_t)r->xmm[0].lo;

    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

DK2ML_INLINE void DK2ML_SetResultFloat(DK2ML_Regs* r, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));

    r->xmm[0].lo = (r->xmm[0].lo & 0xFFFFFFFF00000000ull) | bits;
}

/* --- options ---
 * AddOption settings appear under the mod on the Modloader screen (main menu) as game widgets. The plugin owns the
 * values: the loader reads *value when the screen opens, writes it on change, then calls onChange (save there).
 * Callbacks are crash-contained. AddOption returns DK2ML_ERROR (logged) for structSize < this sizeof(DK2ML_Option), an
 * unknown type, a missing value pointer, FLOAT/INT with max <= min, or CHOICE without 1-64 choices. */

typedef enum DK2ML_OptionType {
    DK2ML_OPTION_HEADER = 0, /* section title; no value */
    DK2ML_OPTION_BOOL = 1,   /* value: bool* (1 byte); checkbox */
    DK2ML_OPTION_FLOAT = 2,  /* value: float*; slider from min to max */
    DK2ML_OPTION_INT = 3,    /* value: int*; integer slider from min to max */
    DK2ML_OPTION_CHOICE = 4, /* value: int*, index into choices; left/right selector */
    DK2ML_OPTION_KEY = 5,    /* value: int*, virtual-key code: a key or middle/back/forward mouse button; Esc
                              * cancels. Left/right Shift/Ctrl/Alt are stored as VK_SHIFT/VK_CONTROL/VK_MENU. */
    DK2ML_OPTION_BUTTON = 6, /* no value; onChange runs on click (e.g. "Reset to defaults") */
} DK2ML_OptionType;

typedef struct DK2ML_Option {
    uint32_t structSize;        /* sizeof(DK2ML_Option); bigger is accepted, extra fields ignored */
    DK2ML_OptionType type;
    const char* label;          /* UTF-8; copied, up to 512 bytes */
    const char* tooltip;        /* optional; copied, up to 512 bytes */
    void* value;                /* see the type; valid for the life of the game */
    float min, max;             /* FLOAT and INT; max > min */
    const char* format;         /* FLOAT/INT, optional: one printf conversion (%f/%g/%e, %d/%i) plus text, e.g.
                                 * "%.0f deg", under 64 characters. Otherwise "%.2f" or "%d". */
    const char* const* choices; /* CHOICE: choiceCount UTF-8 strings (1-64); copied, up to 512 bytes each */
    int choiceCount;
    /* After the player changed the value (BUTTON: clicked). option is the loader's copy. */
    void (*onChange)(const struct DK2ML_Option* option, void* user);
    void* user;
} DK2ML_Option;

/* --- events ---
 * The loader hooks each source once for all subscribers. Subscribe from DK2ML_PluginInit. Callbacks run on the main
 * thread in subscription order, crash-contained (a crash switches the plugin off). If this build lacks an event's
 * names, it's logged and never sent. */

typedef enum DK2ML_EventType {
    DK2ML_EVENT_FRAME = 1,          /* every frame, inside ImGui's frame; not during random-map generation */
    DK2ML_EVENT_GUI_LOADED = 2,     /* the game (re)loaded its GUI; GUI items found before are gone */
    DK2ML_EVENT_STATE_CHANGED = 3, /* GameClient::m_state changed (checked per frame). old/newState: eCGameState
                                     * values (GetEnumValue, e.g. "CGAMESTATE_RUNNING"); -1 = no GameClient */
    DK2ML_EVENT_MAP_LOADED = 4, /* a mission starts or restarts (the game resets the view camera) */
    DK2ML_EVENT_PLUGINS_LOADED = 5, /* once, after every plugin's DK2ML_PluginInit, before any game code;
                                     * GetInterface works from here on */
    DK2ML_EVENT_GUI_EVENT = 6, /* a game GUI event (GUI::Events::eEventType) via SubscribeGuiEvent, after the game
                                     * handled it, so it can't block it (safe-hook the handler for that) */
    DK2ML_EVENT_WINDOW_RESIZED = 7, /* the game window changed size; width/height are the new client size */
} DK2ML_EventType;

typedef struct DK2ML_Event {
    uint32_t structSize; /* sizeof(DK2ML_Event) */
    DK2ML_EventType type;
    void* gameClient; /* the GameClient (*g_pGameClient), or NULL */
    int64_t oldState, newState; /* STATE_CHANGED; for other events, newState is the current state */
    uint32_t guiEventId; /* GUI_EVENT: the event id */
    const void* guiEventParams; /* GUI_EVENT: the game's GUI::sEventParams*, may be NULL */
    int32_t width, height; /* WINDOW_RESIZED: client size in pixels */
} DK2ML_Event;

typedef void (*DK2ML_EventFn)(const DK2ML_Event* event, void* user);

/* --- interfaces ---
 * A plugin publishes a function table under a name (e.g. "author.mod.Thing") during init; others look it up by name.
 * First field structSize; new fields at the end. GetInterface is NULL during init (use PLUGINS_LOADED) and once the
 * publisher is switched off, so look it up each time. A crash in another plugin's function counts as the caller's. */

/* --- tasks ---
 * AddTask runs fn(user) on the main thread at the next frame start, crash-contained: how to call game functions from
 * other threads. FIFO; a task added while tasks run waits a frame. None run during random-map generation. Without a
 * frame tick in this build, tasks queued during init or PLUGINS_LOADED are dropped and later AddTask calls fail. */
typedef void (*DK2ML_TaskFn)(void* user);

/* --- GUI kit ---
 * Game GUI items (GUI::Item and subclasses) as pointers, driven through the game's own functions and actions. Main
 * thread only; NULL items accepted. GUI_LOADED invalidates found items. If this build lacks a name the kit needs, it's
 * logged once and the kit does nothing (NULL, "", 0 or DK2ML_ERROR).
 *
 * Item events: in the plugin's GUI XML, add <Action type="Callback" target="<any existing item>"/> under the event
 * (OnClick, OnHover, OnScrollDown, ...) and bind it with GuiSetCallback. Clones copy their template's callbacks, so
 * bind before cloning. Callbacks are crash-contained. */

/* item: the event's item (a clone for clones). cursorX/Y: GUI coordinates (2560x1440, (0,0) at the center, +y up). */
typedef void (*DK2ML_GuiCallbackFn)(void* item, float cursorX, float cursorY, void* user);

/* --- Esc-menu buttons (API 2) ---
 * AddMenuButton puts a square button into the in-mission Esc menus (single missions and campaigns), in the rows of 3
 * above the menu's own buttons that every plugin shares: in load order, then call order, each row from the left, going
 * up; up to 45 (more are hidden). The loader
 * makes, places and shows the items, so the plugin ships no GUI XML for it. onClick runs on the main thread,
 * crash-contained; a switched-off plugin's buttons disappear. AddMenuButton returns DK2ML_ERROR (logged) for structSize
 * < this sizeof(DK2ML_MenuButton), a NULL onClick, or neither icon nor label. On an API 1 loader the field is missing:
 * check api->structSize > offsetof(DK2ML_API, AddMenuButton), or declare DK2ML_PLUGIN_MANIFEST(2, ...). */
typedef struct DK2ML_MenuButton {
    uint32_t structSize;   /* sizeof(DK2ML_MenuButton); bigger is accepted, extra fields ignored */
    const char* tooltip;   /* optional; copied, up to 512 bytes */
    const char* icon;      /* optional: texture path as in GUI XML, e.g. "data/textures/gui/x_normal.dds", from the
                            * mod's folder like its other files; drawn 64x64. NULL: label instead. Copied. */
    const char* iconHover; /* optional (NULL: icon); copied */
    const char* label;     /* without an icon: a short text, e.g. "CAM"; copied, up to 512 bytes */
    DK2ML_GuiCallbackFn onClick; /* item: the clicked button; Esc stays open */
    void* user;
} DK2ML_MenuButton;

/* --- replay data (API 3) ---
 * A plugin's own bytes, kept inside the replay file the game writes when a mission ends and handed back when that
 * replay starts, also after the player saved it under a name. For state the replay can't repeat on its own, e.g. a
 * switch the player flipped mid-mission: save it with its game time, look it up by game time while replaying.
 *
 * The loader appends one block for all plugins after the game's data, where the game stops reading, each plugin's
 * bytes under its key. save and load run on the main thread, crash-contained; a switched-off plugin's don't run.
 * AddReplayData returns DK2ML_ERROR (logged) for structSize < this sizeof(DK2ML_ReplayData), a key that isn't 1-63
 * printable ASCII (no spaces) or is taken, or a NULL save or load. Without the game's replay names in this build, it's
 * logged once and neither runs. On an API 2 loader the fields are missing: check
 * api->structSize > offsetof(DK2ML_API, AddReplayData), or declare DK2ML_PLUGIN_MANIFEST(3, ...). */
typedef struct DK2ML_ReplayWriter DK2ML_ReplayWriter; /* the loader's; valid only during save */

typedef struct DK2ML_ReplayData {
    uint32_t structSize; /* sizeof(DK2ML_ReplayData); bigger is accepted, extra fields ignored */
    const char* key;     /* unique, e.g. "author.mod"; copied. Keep it: replays saved with it are read back by it. */
    /* The game just wrote the mission's replay file: add this mission's data with ReplayWrite(out, ...). Nothing
     * written: nothing stored. The bytes are the plugin's own format; give them a version. */
    void (*save)(DK2ML_ReplayWriter* out, void* user);
    /* A replay starts (also when it's restarted or skipped through): this key's bytes from its file, or NULL and 0
     * if it has none (an older replay, or the plugin wasn't running): go back to the defaults then. Keep this apart
     * from what the mission records: watching right after a mission, save runs during the start and load then gets
     * those bytes. It can run more than once per start, its order with MAP_LOADED isn't fixed, and IsReplaying may
     * not be set yet. The bytes may come from another player's file: check every count and size. data lives only
     * for the call. */
    void (*load)(const void* data, size_t size, void* user);
    void* user;
} DK2ML_ReplayData;

#define DK2ML_REPLAY_MAX_DATA (1u << 20) /* bytes per key */

/* Threading: AddOption, Subscribe, SubscribeGuiEvent, PublishInterface, AddMenuButton and AddReplayData only during
 * DK2ML_PluginInit. Create hooks there too; CreateSafeHook also works later, from any thread. EnableHook/DisableHook,
 * Log, the PDB lookups (ResolveSymbol, GetFieldOffset, GetTypeSize, GetEnumValue), GetInterface and AddTask: any
 * thread, any time. GUI kit, ReplayWrite and IsReplaying: main thread only. */
typedef struct DK2ML_API {
    uint32_t apiVersion; /* the loader's DK2ML_API_VERSION */
    uint32_t structSize; /* sizeof(DK2ML_API) of the loader */

    /* Game function or global address by undecorated name, e.g. "GameClient::UpdateCamera" (a global's address, not
     * its value). Overloads: the first match plus a logged warning; a decorated name ('?...') picks one. NULL if not
     * found. */
    void* (*ResolveSymbol)(const char* name);

    /* Byte offset of a data member, e.g. GetFieldOffset("Camera", "pitch"). -1 if not found. */
    int32_t (*GetFieldOffset)(const char* typeName, const char* fieldName);

    /* sizeof a game type, e.g. GetTypeSize("Camera"). 0 if not found. */
    uint32_t (*GetTypeSize)(const char* typeName);

    /* Enumerator by C++ name, e.g. GetEnumValue("GUI::eAction", "ACTION_ADD_CHILD", &v). DK2ML_OK if found. */
    DK2ML_Status (*GetEnumValue)(const char* enumType, const char* name, int64_t* out);

    /* Safe hook on target (see "safe hooks"). pre may change arguments or skip the original; post runs after it.
     * Either may be NULL. Callbacks start disabled. DK2ML_ERROR (logged) if creation fails or the plugin was switched
     * off. */
    DK2ML_Status (*CreateSafeHook)(void* target, DK2ML_PreFn pre, DK2ML_PostFn post, void* user);

    /* Switch the calling plugin's callbacks on target. DK2ML_ERROR if target has no safe hook (any plugin's) or the
     * switch fails; EnableHook also once the plugin was switched off. */
    DK2ML_Status (*EnableHook)(void* target);
    DK2ML_Status (*DisableHook)(void* target);

    /* printf-style line to dk2ml.log, prefixed with the plugin's name. */
    void (*Log)(const char* fmt, ...);

    /* Game install folder, with trailing backslash. */
    const wchar_t* (*GetGameDir)(void);

    /* Settings folder that survives mod updates (Steam replaces Workshop mod folders), created on first call:
     * %LOCALAPPDATA%\KillHouseGames\DoorKickers2\dk2ml\<dll name without .dll>\. Empty string if unavailable. */
    const wchar_t* (*GetConfigDir)(void);

    /* Non-zero while the game window has focus (check before GetAsyncKeyState). */
    int (*IsGameFocused)(void);

    /* GameClient::m_state now (GameClient::eCGameState), or -1 while there is no GameClient. */
    int64_t (*GetGameState)(void);

    /* The game version as written into saves and mod.xml (e.g. 112); 0 if unknown. */
    uint32_t (*GetGameVersion)(void);

    /* The game's main window (an HWND), or NULL before it exists. */
    void* (*GetGameWindow)(void);

    /* Adds an option to the mod's page, in call order (see "options"). Init only; later: DK2ML_ERROR. */
    DK2ML_Status (*AddOption)(const DK2ML_Option* option);

    /* fn(event, user) for every event of this type (see "events"). Init only. DK2ML_ERROR for an unknown type,
     * DK2ML_EVENT_GUI_EVENT (use SubscribeGuiEvent), NULL fn, or after init. Subscribing twice gives two callbacks. */
    DK2ML_Status (*Subscribe)(DK2ML_EventType type, DK2ML_EventFn fn, void* user);

    /* fn for game GUI event guiEventId (GUI::Events::eEventType, e.g. "GUI_GAME_CUSTOMIZE_OPENED" via GetEnumValue)
     * as DK2ML_EVENT_GUI_EVENT. Init only. DK2ML_ERROR for id 0 or > 0xFFFF, NULL fn, or after init. An id past this
     * build's last event is accepted, logged, never sent. */
    DK2ML_Status (*SubscribeGuiEvent)(uint32_t guiEventId, DK2ML_EventFn fn, void* user);

    /* Publishes table under name; raise version when adding fields (see "interfaces"). name: 1-63 printable ASCII, no
     * spaces, unique. Init only. DK2ML_ERROR if the name is taken or invalid, or table is NULL. table must live as
     * long as the game. */
    DK2ML_Status (*PublishInterface)(const char* name, uint32_t version, const void* table);

    /* name's table if its version >= minVersion and the publisher runs, else NULL (always during init). versionOut
     * (optional): the published version. Any thread. */
    const void* (*GetInterface)(const char* name, uint32_t minVersion, uint32_t* versionOut);

    /* Queues fn(user) for the next frame start (see "tasks"). Any thread. DK2ML_ERROR if fn is NULL, 4096 tasks (all
     * plugins) are waiting, the plugin was switched off, or there's no frame tick. */
    DK2ML_Status (*AddTask)(DK2ML_TaskFn fn, void* user);

    /* GUI kit (see above). GuiFind: first item named name under `under` (NULL = the whole GUI), recursive. */
    void* (*GuiFind)(void* under, const char* name);
    void* (*GuiParent)(void* item);
    const char* (*GuiName)(void* item); /* XML name; "" if unnamed (clones have no name) */

    /* Up to max children into out (NULL: count only), in drawing order (last on top). Returns the child count, which
     * can exceed max. */
    int (*GuiChildren)(void* item, void** out, int max);

    int (*GuiIsShown)(void* item); /* the item's own flag; a shown item in a hidden parent isn't drawn */
    void (*GuiShow)(void* item, int show);

    /* Text of a StaticText, or all three state texts of a Button; exact types only (others and NULL utf8:
     * DK2ML_ERROR). A leading '@' (game text key) is removed. The game keeps at most as many characters as the XML
     * text had. */
    DK2ML_Status (*GuiSetText)(void* item, const char* utf8);

    /* The game's AddChild action: moves item last among parent's children (drawn and clicked on top); doesn't show
     * it. A mod's own GUI items start at the top level. */
    DK2ML_Status (*GuiAddChild)(void* parent, void* item);

    /* The game's SetOrigin action: relative to the parent's center, +y up. Also sets the align offset, so for an
     * aligned item (align="l" etc.) x/y replace the XML origin. */
    DK2ML_Status (*GuiSetOrigin)(void* item, float x, float y);

    DK2ML_Status (*GuiClick)(void* item); /* runs the item's OnClick actions */

    /* Binds item's <Action type="Callback"> actions for itemEvent (GUI::Item::eItemEventType, e.g. "EVENT_CLICK" via
     * GetEnumValue) to fn. DK2ML_ERROR if fn is NULL, itemEvent is out of range, the item has no Callback action for
     * it, the plugin was switched off, or 4096 distinct (plugin, fn, user) callbacks exist. The same fn and user again
     * reuse their callback. */
    DK2ML_Status (*GuiSetCallback)(void* item, int itemEvent, DK2ML_GuiCallbackFn fn, void* user);

    /* While any plugin captures (1), the game acts as if a menu is open: clicks and keys don't reach the map. 0
     * releases. A switched-off plugin's capture is released and its calls fail. Without GameGUI::IsAnyMenuOpened in
     * this build: DK2ML_OK, no effect. */
    DK2ML_Status (*CaptureGameInput)(int capture);

    /* Non-zero if one of the game's own menus is open, ignoring captures. */
    int (*IsGameMenuOpen)(void);

    /* --- API 2 --- */

    /* Adds a button to the Esc menus (see "Esc-menu buttons"). Init only; later: DK2ML_ERROR. */
    DK2ML_Status (*AddMenuButton)(const DK2ML_MenuButton* button);

    /* --- API 3 --- */

    /* Keeps data in replay files (see "replay data"). Init only; later: DK2ML_ERROR. */
    DK2ML_Status (*AddReplayData)(const DK2ML_ReplayData* data);

    /* Appends size bytes to the save callback's data; several calls add up. Only inside save, with its out.
     * DK2ML_ERROR (logged) otherwise, for NULL data with size > 0, or past DK2ML_REPLAY_MAX_DATA: then nothing of this
     * save is stored. */
    DK2ML_Status (*ReplayWrite)(DK2ML_ReplayWriter* out, const void* data, size_t size);

    /* Non-zero while the GameClient plays a replay (GameClient::CGAMEFLAG_REPLAYING); 0 without a GameClient or if this
     * build lacks the names. */
    int (*IsReplaying)(void);
} DK2ML_API;

typedef struct DK2ML_PluginInfo {
    const wchar_t* pluginPath; /* full path of the plugin DLL */
    const wchar_t* pluginDir; /* folder of the plugin DLL, with trailing backslash */
    const wchar_t* modDir; /* mod folder, with trailing backslash; empty for mods_native */
} DK2ML_PluginInfo;

typedef int (*DK2ML_PluginInitFn)(const DK2ML_API* api, const DK2ML_PluginInfo* info);

#define DK2ML_PLUGIN_INIT_NAME "DK2ML_PluginInit"

/* --- manifest ---
 * Optional. Shown on the Modloader screen, in dk2ml.log and crash reports; read from the DLL without running it. A
 * loader with DK2ML_API_VERSION < minApiVersion refuses the plugin and says why. UTF-8 texts, cut to array size. Once,
 * at file scope:
 *   DK2ML_PLUGIN_MANIFEST(DK2ML_API_VERSION, "Free Camera", "1.2.0", "Some Author", "https://...", 112);
 * Arguments: minApiVersion, name, version, author, url (may be ""), gameVersion (may be 0). */
typedef struct DK2ML_Manifest {
    uint32_t structSize; /* sizeof(DK2ML_Manifest) */
    uint32_t minApiVersion; /* lowest DK2ML_API_VERSION the plugin needs */
    char name[64]; /* player-facing name */
    char version[32]; /* plugin version, e.g. "1.2.0" */
    char author[64];
    char url[160]; /* homepage or Workshop page; shown as text */
    uint32_t gameVersion; /* game version it was made for, as in mod.xml (e.g. 112); shown, not enforced */
} DK2ML_Manifest;

#define DK2ML_MANIFEST_NAME "DK2ML_PluginManifest"
#define DK2ML_PLUGIN_MANIFEST(minApiVersion, name, version, author, url, gameVersion)                                  \
    DK2ML_EXPORT const DK2ML_Manifest DK2ML_PluginManifest = {                                                         \
        sizeof(DK2ML_Manifest), (minApiVersion), name, version, author, url, (gameVersion)}

#ifdef __cplusplus
}
#define DK2ML_EXPORT extern "C" __declspec(dllexport)
#else
#define DK2ML_EXPORT __declspec(dllexport)
#endif

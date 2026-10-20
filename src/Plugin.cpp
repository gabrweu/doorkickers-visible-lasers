// era's Lasers Enhanced: visible lasers without night vision, the stock IR ones with it.
// Manifest, settings, Modloader-screen options, the hotkey and event wiring.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <string>

#include "BuildTime.h"
#include "Lasers.h"
#include "game/Game.h"

// Keep gameVersion in step with mod.xml.
DK2ML_PLUGIN_MANIFEST(1, "era's Lasers Enhanced", PLUGIN_VERSION, "era",
                      "https://github.com/gabrweu/doorkickers-visible-lasers", 112);

// clang-format off
const ColorPreset kColorPresets[kColorPresetCount] = {
    {"Red", 255, 0, 0}, {"Green", 0, 255, 0}, {"Blue", 0, 64, 255}, {"Cyan", 0, 255, 255},
    {"Magenta", 255, 0, 255}, {"Yellow", 255, 255, 0}, {"Orange", 255, 128, 0}, {"Purple", 160, 32, 255},
    {"White", 255, 255, 255}, {"Pink", 255, 105, 180}, {"Custom", 0, 0, 0},
};
// clang-format on

Settings g_settings;

namespace {

const DK2ML_API* g_api = nullptr;

// --- settings: [lasers] in <config dir>\settings.ini ---
constexpr const wchar_t* kSection = L"lasers";
std::wstring g_settingsFile; // empty: defaults only

constexpr DWORD kSaveDelayMs = 500; // sliders change every frame while dragged
bool g_dirty = false;
DWORD g_lastChange = 0;

int ReadInt(const wchar_t* key, int fallback)
{
    return GetPrivateProfileIntW(kSection, key, fallback, g_settingsFile.c_str());
}

// 0 (unbound), a key, or middle/side mouse (left/right are the game's); left/right modifiers become the generic one.
int ValidKey(int vk, int fallback)
{
    return vk == 0 || (vk >= VK_MBUTTON && vk <= 0xFE) ? GenericModifier(vk) : fallback;
}

float ReadFloat(const wchar_t* key, float fallback)
{
    wchar_t text[64];
    GetPrivateProfileStringW(kSection, key, L"", text, 64, g_settingsFile.c_str());
    wchar_t* end = nullptr;
    float value = std::wcstof(text, &end);
    return end == text ? fallback : value;
}

float ReadFloat(const wchar_t* key, float fallback, Range range)
{
    return std::clamp(ReadFloat(key, fallback), range.min, range.max);
}

// The section's lines, written in one go.
struct SectionWriter {
    std::wstring text;

    void Int(const std::wstring& key, int value) { Line(key, std::to_wstring(value)); }

    void Float(const std::wstring& key, float value)
    {
        wchar_t buffer[32];
        std::swprintf(buffer, 32, L"%.2f", value);
        Line(key, buffer);
    }

    void Line(const std::wstring& key, const std::wstring& value)
    {
        text += key + L"=" + value;
        text += L'\0';
    }
};

void LoadStyle(const std::wstring& prefix, LaserStyle& style)
{
    style.color = std::clamp(ReadInt((prefix + L"Color").c_str(), style.color), 0, kColorPresetCount - 1);
    style.red = std::clamp(ReadInt((prefix + L"Red").c_str(), style.red), 0, 255);
    style.green = std::clamp(ReadInt((prefix + L"Green").c_str(), style.green), 0, 255);
    style.blue = std::clamp(ReadInt((prefix + L"Blue").c_str(), style.blue), 0, 255);
    style.thickness = ReadFloat((prefix + L"Thickness").c_str(), style.thickness, kThicknessRange);
    style.strength = ReadFloat((prefix + L"Strength").c_str(), style.strength, kStrengthRange);
    style.dotBrightness = ReadFloat((prefix + L"Dot").c_str(), style.dotBrightness, kDotRange);
    style.dotSize = ReadFloat((prefix + L"DotSize").c_str(), style.dotSize, kDotSizeRange);
}

void SaveStyle(SectionWriter& out, const std::wstring& prefix, const LaserStyle& style)
{
    out.Int(prefix + L"Color", style.color);
    out.Int(prefix + L"Red", style.red);
    out.Int(prefix + L"Green", style.green);
    out.Int(prefix + L"Blue", style.blue);
    out.Float(prefix + L"Thickness", style.thickness);
    out.Float(prefix + L"Strength", style.strength);
    out.Float(prefix + L"Dot", style.dotBrightness);
    out.Float(prefix + L"DotSize", style.dotSize);
}

// Per-trooper mode keeps the global visible switch on; the troopers' own switches start off instead.
void ResetMissionState()
{
    g_settings.nvg.on = true;
    g_settings.visible.on = g_settings.perUnitLasers || g_settings.visibleAtStart;
    Units_Clear();
}

void LoadSettings()
{
    if (g_api->GetConfigDir()[0]) {
        g_settingsFile = std::wstring(g_api->GetConfigDir()) + L"settings.ini";
        Settings& s = g_settings;
        s.toggleKey = ValidKey(ReadInt(L"toggleKey", s.toggleKey), s.toggleKey);
        s.perUnitLasers = ReadInt(L"perUnitLasers", s.perUnitLasers) != 0;
        s.visibleAtStart = ReadInt(L"visibleAtStart", s.visibleAtStart) != 0;
        s.heightCm = ReadFloat(L"heightCm", s.heightCm, kHeightRange);
        s.backCm = ReadFloat(L"backCm", s.backCm, kBackRange);
        s.sideCm = ReadFloat(L"sideCm", s.sideCm, kSideRange);
        LoadStyle(L"visible", s.visible);
        LoadStyle(L"nvg", s.nvg);
        Guns_Load(g_settingsFile);
    }
    ResetMissionState(); // as at a mission start, until the first one
}

// The loader reports every step of a slider drag.
void SaveOption(const DK2ML_Option* option, void*)
{
    if (option->type == DK2ML_OPTION_FLOAT || option->type == DK2ML_OPTION_INT) {
        Settings_SaveSoon();
    } else {
        Settings_Save();
    }
}

void AddOption(DK2ML_OptionType type, const char* label, const char* tooltip, void* value, float min = 0.0f,
               float max = 0.0f, const char* format = nullptr)
{
    DK2ML_Option option = {sizeof(DK2ML_Option), type, label, tooltip, value, min, max, format};
    option.onChange = SaveOption;
    g_api->AddOption(&option);
}

void AddHeader(const char* label)
{
    DK2ML_Option header = {sizeof(DK2ML_Option), DK2ML_OPTION_HEADER, label};
    g_api->AddOption(&header);
}

void AddStyleOptions(const char* header, LaserStyle& style)
{
    const char* colorNames[kColorPresetCount]; // the loader copies them
    for (int i = 0; i < kColorPresetCount; ++i) {
        colorNames[i] = kColorPresets[i].name;
    }

    AddHeader(header);
    DK2ML_Option color = {sizeof(DK2ML_Option), DK2ML_OPTION_CHOICE, "Color", "The beam's and the dot's color",
                          &style.color};
    color.choices = colorNames;
    color.choiceCount = kColorPresetCount;
    color.onChange = SaveOption;
    g_api->AddOption(&color);

    AddOption(DK2ML_OPTION_INT, "Custom color: red", "Used when Color is Custom", &style.red, 0, 255);
    AddOption(DK2ML_OPTION_INT, "Custom color: green", "Used when Color is Custom", &style.green, 0, 255);
    AddOption(DK2ML_OPTION_INT, "Custom color: blue", "Used when Color is Custom", &style.blue, 0, 255);
    AddOption(DK2ML_OPTION_FLOAT, "Beam thickness", "The beam's width, relative to the game's", &style.thickness,
              kThicknessRange.min, kThicknessRange.max, "%.2fx");
    AddOption(DK2ML_OPTION_FLOAT, "Beam strength", "How opaque the beam is, relative to the game's", &style.strength,
              kStrengthRange.min, kStrengthRange.max, "%.2fx");
    AddOption(DK2ML_OPTION_FLOAT, "Dot brightness", "How bright the dot's light is, relative to the game's; 0 hides it",
              &style.dotBrightness, kDotRange.min, kDotRange.max, "%.2fx");
    AddOption(DK2ML_OPTION_FLOAT, "Dot size", "The dot's size, relative to the game's", &style.dotSize,
              kDotSizeRange.min, kDotSizeRange.max, "%.2fx");
}

void AddOptions()
{
    AddHeader("era's Lasers Enhanced");
    AddOption(DK2ML_OPTION_KEY, "Toggle lasers",
              "Turns the lasers in view on or off: IR with night vision on, visible with it off. With Per-trooper "
              "lasers: the selected trooper's, Shift for the whole team (none if the key is Shift itself)",
              &g_settings.toggleKey);
    AddOption(DK2ML_OPTION_BOOL, "Per-trooper lasers",
              "Laser on/off buttons in each trooper's right-click menu, and the key works per trooper. Choices last "
              "for the mission",
              &g_settings.perUnitLasers);
    AddOption(DK2ML_OPTION_BOOL, "Visible lasers on at mission start",
              "Missions start with visible lasers on. Off: turn them on with the key or per trooper",
              &g_settings.visibleAtStart);

    AddHeader("Laser position");
    AddOption(DK2ML_OPTION_FLOAT, "Height above barrel",
              "Raises the beam and the dot. For guns without their own position (set per gun in the Esc menu's "
              "window)",
              &g_settings.heightCm, kHeightRange.min, kHeightRange.max, "%.1f cm");
    AddOption(DK2ML_OPTION_FLOAT, "Start behind muzzle",
              "Starts the beam further back along the gun. For guns without their own position", &g_settings.backCm,
              kBackRange.min, kBackRange.max, "%.1f cm");
    AddOption(DK2ML_OPTION_FLOAT, "Left / right of barrel",
              "Moves the beam and the dot sideways: right is positive, left negative. For guns without their own "
              "position",
              &g_settings.sideCm, kSideRange.min, kSideRange.max, "%.1f cm");

    AddStyleOptions("Visible lasers (without night vision)", g_settings.visible);
    AddStyleOptions("IR lasers (with night vision)", g_settings.nvg);
}

// --- hotkey ---
bool g_toggleWasDown = false;
int g_hotkey = 0; // the key g_toggleWasDown belongs to
bool g_capturing = false;

bool KeyDown(int vk)
{
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// Ctrl/Alt held: someone else's combination.
bool Chord(int vk)
{
    return vk != VK_CONTROL && vk != VK_MENU && (KeyDown(VK_CONTROL) || KeyDown(VK_MENU));
}

bool CanToggle()
{
    return game::MissionRunning() && !game::Editing() && !dk2ml::GameMenuOpen(g_api) && !game::ChatOpen() &&
           !Window_IsRebinding() && !Window_ImGuiWantsKeyboard() && !Chord(g_settings.toggleKey);
}

void Toggle()
{
    if (g_settings.perUnitLasers) {
        // The game's status keys use Shift for the team; a Shift key binding has no team toggle.
        if (g_settings.toggleKey != VK_SHIFT && KeyDown(VK_SHIFT)) {
            Units_ToggleAll();
        } else {
            Units_ToggleSelected();
        }
        return;
    }

    LaserStyle& kind = game::NvgView() ? g_settings.nvg : g_settings.visible;
    kind.on = !kind.on;
}

void OnFrame(const DK2ML_Event*, void*)
{
    bool capture = Window_WantsInput();
    if (capture != g_capturing && dk2ml::CaptureInput(g_api, capture)) {
        g_capturing = capture;
    }
    Window_Draw();
    if (g_dirty && GetTickCount() - g_lastChange >= kSaveDelayMs) {
        Settings_SaveNow();
    }

    // A just-rebound key is still held: it waits for its next press.
    bool down = g_api->IsGameFocused() && KeyDown(g_settings.toggleKey);
    if (g_settings.toggleKey != g_hotkey) {
        g_hotkey = g_settings.toggleKey;
        g_toggleWasDown = down;
    }
    if (down && !g_toggleWasDown && CanToggle()) {
        Toggle();
    }
    g_toggleWasDown = down;
}

void OnMapLoaded(const DK2ML_Event*, void*)
{
    ResetMissionState();
}

} // namespace

void Settings_Save()
{
    if (g_settingsFile.empty()) {
        return;
    }

    const Settings& s = g_settings;
    SectionWriter out;
    out.Int(L"toggleKey", s.toggleKey);
    out.Int(L"perUnitLasers", s.perUnitLasers);
    out.Int(L"visibleAtStart", s.visibleAtStart);
    out.Float(L"heightCm", s.heightCm);
    out.Float(L"backCm", s.backCm);
    out.Float(L"sideCm", s.sideCm);
    SaveStyle(out, L"visible", s.visible);
    SaveStyle(out, L"nvg", s.nvg);
    out.text += L'\0';
    WritePrivateProfileSectionW(kSection, out.text.c_str(), g_settingsFile.c_str());
    Guns_Save(g_settingsFile);
}

void Settings_SaveSoon()
{
    g_dirty = true;
    g_lastChange = GetTickCount();
}

void Settings_SaveNow()
{
    if (g_dirty) {
        g_dirty = false;
        Settings_Save();
    }
}

std::string Utf8(std::wstring_view text)
{
    int count =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), count, nullptr, nullptr);
    return utf8;
}

int GenericModifier(int vk)
{
    switch (vk) {
    case VK_LSHIFT:
    case VK_RSHIFT: return VK_SHIFT;
    case VK_LCONTROL:
    case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU:
    case VK_RMENU: return VK_MENU;
    default: return vk;
    }
}

uint32_t StyleRgb(const LaserStyle& style)
{
    int index = std::clamp(style.color, 0, kColorPresetCount - 1);
    const ColorPreset& preset = kColorPresets[index];
    bool custom = index == kCustomColor;
    uint32_t r = static_cast<uint32_t>(custom ? style.red : preset.red);
    uint32_t g = static_cast<uint32_t>(custom ? style.green : preset.green);
    uint32_t b = static_cast<uint32_t>(custom ? style.blue : preset.blue);
    return (b << 16) | (g << 8) | r;
}

DK2ML_EXPORT int DK2ML_PluginInit(const DK2ML_API* api, const DK2ML_PluginInfo*)
{
    if (api->structSize < offsetof(DK2ML_API, AddMenuButton)) {
        return 1; // older than API 1
    }

    g_api = api;
    game::api = api;
    LoadSettings();

    if (!dk2ml::ResolveAll(api)) {
        return 2;
    }
    if (!Lasers_Init(api)) {
        return 3;
    }
    if (!UnitButton_Init(api)) {
        api->Log("per-trooper buttons unavailable in this game build; the toggle key still works");
    }
    if (!dk2ml::On(api, DK2ML_EVENT_FRAME, OnFrame) || !dk2ml::On(api, DK2ML_EVENT_MAP_LOADED, OnMapLoaded)) {
        return 4;
    }

    AddOptions();
    MenuButton_Register(api);
    api->Log("ready (%s, built %s; settings: %ls)", PLUGIN_VERSION, PLUGIN_BUILD_TIME,
             g_settingsFile.empty() ? L"none" : g_settingsFile.c_str());
    return 0;
}

// Manifest, settings, Modloader-screen options, hotkeys, event wiring.
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
DK2ML_PLUGIN_MANIFEST(1, "era's Lights and Lasers", PLUGIN_VERSION, "era",
                      "https://github.com/gabrweu/doorkickers-visible-lasers", 112);

// clang-format off
const ColorPreset kColorPresets[kColorPresetCount] = {
    {"Red", 255, 0, 0}, {"Green", 0, 255, 0}, {"Blue", 0, 64, 255}, {"Cyan", 0, 255, 255},
    {"Magenta", 255, 0, 255}, {"Yellow", 255, 255, 0}, {"Orange", 255, 128, 0}, {"Purple", 160, 32, 255},
    {"White", 255, 255, 255}, {"Pink", 255, 105, 180}, {"Custom", 0, 0, 0},
};
// clang-format on

Settings g_settings;

const char* const kBehaviorModeNames[kBehaviorModeCount] = {"Strobe", "Steady on"};

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

// 0, a key, or middle/side mouse (left/right are the game's). Left/right modifiers become the generic one.
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

void LoadLightStyle(const std::wstring& prefix, LightStyle& style)
{
    style.color = std::clamp(ReadInt((prefix + L"Color").c_str(), style.color), 0, kColorPresetCount - 1);
    style.red = std::clamp(ReadInt((prefix + L"Red").c_str(), style.red), 0, 255);
    style.green = std::clamp(ReadInt((prefix + L"Green").c_str(), style.green), 0, 255);
    style.blue = std::clamp(ReadInt((prefix + L"Blue").c_str(), style.blue), 0, 255);
    style.brightness = ReadFloat((prefix + L"Brightness").c_str(), style.brightness, kBrightnessRange);
    style.rangeM = ReadFloat((prefix + L"Range").c_str(), style.rangeM, kLightRange);
    style.widthDeg = ReadFloat((prefix + L"Width").c_str(), style.widthDeg, kWidthRange);
    style.sway = ReadFloat((prefix + L"Sway").c_str(), style.sway, kSwayRange);
    style.flicker = ReadFloat((prefix + L"Flicker").c_str(), style.flicker, kFlickerRange);
    style.glow = ReadInt((prefix + L"Glow").c_str(), style.glow) != 0;
    style.glowSize = ReadFloat((prefix + L"GlowSize").c_str(), style.glowSize, kGlowSizeRange);
    style.glowAngleDeg = ReadFloat((prefix + L"GlowAngle").c_str(), style.glowAngleDeg, kGlowAngleRange);
    style.cone = ReadInt((prefix + L"Cone").c_str(), style.cone) != 0;
    style.coneLengthM = ReadFloat((prefix + L"ConeLength").c_str(), style.coneLengthM, kConeLengthRange);
    style.coneStrength = ReadFloat((prefix + L"ConeStrength").c_str(), style.coneStrength, kConeStrengthRange);
}

void SaveLightStyle(SectionWriter& out, const std::wstring& prefix, const LightStyle& style)
{
    out.Int(prefix + L"Color", style.color);
    out.Int(prefix + L"Red", style.red);
    out.Int(prefix + L"Green", style.green);
    out.Int(prefix + L"Blue", style.blue);
    out.Float(prefix + L"Brightness", style.brightness);
    out.Float(prefix + L"Range", style.rangeM);
    out.Float(prefix + L"Width", style.widthDeg);
    out.Float(prefix + L"Sway", style.sway);
    out.Float(prefix + L"Flicker", style.flicker);
    out.Int(prefix + L"Glow", style.glow);
    out.Float(prefix + L"GlowSize", style.glowSize);
    out.Float(prefix + L"GlowAngle", style.glowAngleDeg);
    out.Int(prefix + L"Cone", style.cone);
    out.Float(prefix + L"ConeLength", style.coneLengthM);
    out.Float(prefix + L"ConeStrength", style.coneStrength);
}

void LoadBehaviors(const std::wstring& prefix, Behaviors& b)
{
    b.onLaser = ReadInt((prefix + L"BehaviorLaser").c_str(), b.onLaser) != 0;
    b.onLight = ReadInt((prefix + L"BehaviorLight").c_str(), b.onLight) != 0;
    b.mode = std::clamp(ReadInt((prefix + L"BehaviorMode").c_str(), b.mode), 0, kBehaviorModeCount - 1);
    b.onlyIfOn = ReadInt((prefix + L"BehaviorOnlyIfOn").c_str(), b.onlyIfOn) != 0;
    b.engageOnAim = ReadInt((prefix + L"EngageOnAim").c_str(), b.engageOnAim) != 0;
    b.aimSettleS = ReadFloat((prefix + L"EngageSettleS").c_str(), b.aimSettleS, kSettleRange);
    b.holdS = ReadFloat((prefix + L"EngageHoldS").c_str(), b.holdS, kHoldRange);
    b.strobeHz = ReadFloat((prefix + L"StrobeHz").c_str(), b.strobeHz, kStrobeRange);
}

void SaveBehaviors(SectionWriter& out, const std::wstring& prefix, const Behaviors& b)
{
    out.Int(prefix + L"BehaviorLaser", b.onLaser);
    out.Int(prefix + L"BehaviorLight", b.onLight);
    out.Int(prefix + L"BehaviorMode", b.mode);
    out.Int(prefix + L"BehaviorOnlyIfOn", b.onlyIfOn);
    out.Int(prefix + L"EngageOnAim", b.engageOnAim);
    out.Float(prefix + L"EngageSettleS", b.aimSettleS);
    out.Float(prefix + L"EngageHoldS", b.holdS);
    out.Float(prefix + L"StrobeHz", b.strobeHz);
}

// Per-trooper mode keeps the global switches on; Units holds the start states.
void ResetMissionState()
{
    for (int i = 0; i < kKindCount; ++i) {
        Kind kind = static_cast<Kind>(i);
        Settings_On(kind) = g_settings.perUnitLasers || Settings_AtStart(kind);
    }
    Units_Clear();
    Lights_MissionStart();
    Engagement_MissionStart();
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
        s.lightToggleKey = ValidKey(ReadInt(L"lightToggleKey", s.lightToggleKey), s.lightToggleKey);
        s.lightAtStart = ReadInt(L"lightAtStart", s.lightAtStart) != 0;
        s.irLaserAtStart = ReadInt(L"irLaserAtStart", s.irLaserAtStart) != 0;
        s.lightShadows = ReadInt(L"lightShadows", s.lightShadows) != 0;
        s.lightHeightCm = ReadFloat(L"lightHeightCm", s.lightHeightCm, kHeightRange);
        s.lightBackCm = ReadFloat(L"lightBackCm", s.lightBackCm, kBackRange);
        s.lightSideCm = ReadFloat(L"lightSideCm", s.lightSideCm, kSideRange);
        LoadLightStyle(L"light", s.light);
        LoadBehaviors(L"visible", s.visibleBehaviors);
        LoadBehaviors(L"nvg", s.nvgBehaviors);
        s.behaviorsAtStart = ReadInt(L"behaviorsAtStart", s.behaviorsAtStart) != 0;
        Guns_Load(g_settingsFile);
    }
    ResetMissionState();
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

void AddColorOptions(const char* tooltip, int* color, int* red, int* green, int* blue)
{
    const char* colorNames[kColorPresetCount]; // the loader copies them
    for (int i = 0; i < kColorPresetCount; ++i) {
        colorNames[i] = kColorPresets[i].name;
    }

    DK2ML_Option choice = {sizeof(DK2ML_Option), DK2ML_OPTION_CHOICE, "Color", tooltip, color};
    choice.choices = colorNames;
    choice.choiceCount = kColorPresetCount;
    choice.onChange = SaveOption;
    g_api->AddOption(&choice);

    AddOption(DK2ML_OPTION_INT, "Custom color: red", "Used when Color is Custom", red, 0, 255);
    AddOption(DK2ML_OPTION_INT, "Custom color: green", "Used when Color is Custom", green, 0, 255);
    AddOption(DK2ML_OPTION_INT, "Custom color: blue", "Used when Color is Custom", blue, 0, 255);
}

void AddStyleOptions(const char* header, LaserStyle& style)
{
    AddHeader(header);
    AddColorOptions("The beam's and the dot's color", &style.color, &style.red, &style.green, &style.blue);
    AddOption(DK2ML_OPTION_FLOAT, "Beam thickness", "The beam's width, relative to the game's", &style.thickness,
              kThicknessRange.min, kThicknessRange.max, "%.2fx");
    AddOption(DK2ML_OPTION_FLOAT, "Beam strength", "How opaque the beam is, relative to the game's", &style.strength,
              kStrengthRange.min, kStrengthRange.max, "%.2fx");
    AddOption(DK2ML_OPTION_FLOAT, "Dot brightness", "How bright the dot's light is, relative to the game's; 0 hides it",
              &style.dotBrightness, kDotRange.min, kDotRange.max, "%.2fx");
    AddOption(DK2ML_OPTION_FLOAT, "Dot size", "The dot's size, relative to the game's", &style.dotSize,
              kDotSizeRange.min, kDotSizeRange.max, "%.2fx");
}

void AddLightStyleOptions(const char* header, LightStyle& style)
{
    AddHeader(header);
    AddColorOptions("The light's color", &style.color, &style.red, &style.green, &style.blue);
    AddOption(DK2ML_OPTION_FLOAT, "Brightness", "How bright the light is; the maps' own spot lights are about 1 to 2.5",
              &style.brightness, kBrightnessRange.min, kBrightnessRange.max, "%.2f");
    AddOption(DK2ML_OPTION_FLOAT, "Range", "How far the light reaches", &style.rangeM, kLightRange.min, kLightRange.max,
              "%.1f m");
    AddOption(DK2ML_OPTION_FLOAT, "Beam width", "The light cone's full width", &style.widthDeg, kWidthRange.min,
              kWidthRange.max, "%.0f deg");
    AddOption(DK2ML_OPTION_FLOAT, "Hand sway",
              "The beam wanders a little around the aim, more while moving; 0 = steady", &style.sway, kSwayRange.min,
              kSwayRange.max, "%.1fx");
    AddOption(DK2ML_OPTION_FLOAT, "Flicker", "A subtle variation in brightness; 0 = constant", &style.flicker,
              kFlickerRange.min, kFlickerRange.max, "%.1fx");
    AddOption(DK2ML_OPTION_BOOL, "Lens glow", "A glow at the light, so a trooper with the light on stands out",
              &style.glow);
    AddOption(DK2ML_OPTION_FLOAT, "Glow size", "The lens glow's size", &style.glowSize, kGlowSizeRange.min,
              kGlowSizeRange.max, "%.2fx");
    AddOption(DK2ML_OPTION_FLOAT, "Glow seen within",
              "How far off the beam's direction the camera still sees the glow, as with a real lens; 180 = from "
              "everywhere. The stock camera looks mostly down, so small angles rarely show it",
              &style.glowAngleDeg, kGlowAngleRange.min, kGlowAngleRange.max, "%.0f deg");
    AddOption(DK2ML_OPTION_BOOL, "Beam cone", "A visible cone of light from the lens, stopping at walls", &style.cone);
    AddOption(DK2ML_OPTION_FLOAT, "Cone length", "How far the visible cone reaches (at most to the wall)",
              &style.coneLengthM, kConeLengthRange.min, kConeLengthRange.max, "%.1f m");
    AddOption(DK2ML_OPTION_FLOAT, "Cone strength", "How bright the visible cone is", &style.coneStrength,
              kConeStrengthRange.min, kConeStrengthRange.max, "%.2fx");
}

void AddBehaviorOptions(const char* header, Behaviors& b)
{
    AddHeader(header);
    AddOption(DK2ML_OPTION_BOOL, "Behavior on lasers", "Troopers strobe their laser, or keep it on, while engaging",
              &b.onLaser);
    AddOption(DK2ML_OPTION_BOOL, "Behavior on lights", "Troopers strobe their light, or keep it on, while engaging",
              &b.onLight);
    DK2ML_Option mode = {sizeof(DK2ML_Option), DK2ML_OPTION_CHOICE, "Behavior mode",
                         "Strobe: blinks while engaging. Steady on: stays on while engaging, then back to its switch",
                         &b.mode};
    mode.choices = kBehaviorModeNames;
    mode.choiceCount = kBehaviorModeCount;
    mode.onChange = SaveOption;
    g_api->AddOption(&mode);
    AddOption(DK2ML_OPTION_BOOL, "Only if the laser/light is already on",
              "Only lasers and lights already switched on take part. Off: engaging lights up ones switched off too",
              &b.onlyIfOn);
    AddOption(DK2ML_OPTION_BOOL, "Start when aiming at a seen enemy",
              "Engaging starts when a trooper aims at an enemy it can see, before the first shot. Off: shots only",
              &b.engageOnAim);
    AddOption(DK2ML_OPTION_FLOAT, "Aim held for",
              "How long a trooper must keep aiming at an enemy it sees before it counts: sweeping past one doesn't",
              &b.aimSettleS, kSettleRange.min, kSettleRange.max, "%.2f s");
    AddOption(DK2ML_OPTION_FLOAT, "Keep the behavior after", "How long engaging lasts after the last shot or sighting",
              &b.holdS, kHoldRange.min, kHoldRange.max, "%.2f s");
    AddOption(DK2ML_OPTION_FLOAT, "Strobe speed", "Blinks per second in Strobe mode", &b.strobeHz, kStrobeRange.min,
              kStrobeRange.max, "%.1f Hz");
}

void AddOptions()
{
    AddHeader("era's Lights and Lasers");
    AddOption(DK2ML_OPTION_KEY, "Toggle lasers",
              "Turns the lasers in view on or off: IR with night vision on, visible with it off. With Per-trooper "
              "lasers: the selected trooper's, Shift for the whole team (none if the key is Shift itself)",
              &g_settings.toggleKey);
    AddOption(DK2ML_OPTION_KEY, "Toggle lights",
              "Turns the tactical lights on or off, seen with and without night vision. With Per-trooper lasers and "
              "lights: the selected trooper's, Shift for the whole team",
              &g_settings.lightToggleKey);
    AddOption(DK2ML_OPTION_BOOL, "Per-trooper lasers and lights",
              "Laser and light on/off buttons in each trooper's right-click menu, and the keys work per trooper. "
              "Choices last for the mission",
              &g_settings.perUnitLasers);
    AddOption(DK2ML_OPTION_BOOL, "Visible lasers on at mission start",
              "Missions start with visible lasers on. Off: turn them on with the key or per trooper",
              &g_settings.visibleAtStart);
    AddOption(DK2ML_OPTION_BOOL, "IR lasers on at mission start",
              "Missions start with IR lasers on, as in the stock game. Off: turn them on with the key or per trooper",
              &g_settings.irLaserAtStart);

    AddHeader("Laser position");
    AddOption(DK2ML_OPTION_FLOAT, "Above / below barrel",
              "Raises or lowers the beam and the dot: up is positive, down negative. For guns without their own "
              "position (set per gun in the Esc menu's window)",
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

    AddHeader("Tactical lights");
    AddOption(DK2ML_OPTION_BOOL, "Lights on at mission start",
              "Missions start with lights on. Off: turn them on with the key or per trooper", &g_settings.lightAtStart);
    AddOption(DK2ML_OPTION_BOOL, "Walls block the lights",
              "Lights cast shadows, so they don't shine through walls. Costs some performance on maps with many "
              "shadowed lights",
              &g_settings.lightShadows);
    AddOption(DK2ML_OPTION_FLOAT, "Light above / below barrel",
              "Raises or lowers the light: up is positive, down negative. For guns without their own light position "
              "(set per gun in the Esc menu's window)",
              &g_settings.lightHeightCm, kHeightRange.min, kHeightRange.max, "%.1f cm");
    AddOption(DK2ML_OPTION_FLOAT, "Light behind muzzle",
              "Moves the light back along the gun. For guns without their own light position", &g_settings.lightBackCm,
              kBackRange.min, kBackRange.max, "%.1f cm");
    AddOption(DK2ML_OPTION_FLOAT, "Light left / right of barrel",
              "Moves the light sideways: right is positive, left negative. For guns without their own light position",
              &g_settings.lightSideCm, kSideRange.min, kSideRange.max, "%.1f cm");
    AddLightStyleOptions("Light style", g_settings.light);

    AddHeader("Engagement behavior");
    AddOption(
        DK2ML_OPTION_BOOL, "Engagement behavior on at mission start",
        "Troopers start with the engagement behavior on. Off: turn it on per trooper in the right-click menu (with "
        "Per-trooper lasers and lights)",
        &g_settings.behaviorsAtStart);
    AddBehaviorOptions("Engagement behavior without night vision", g_settings.visibleBehaviors);
    AddBehaviorOptions("Engagement behavior with night vision", g_settings.nvgBehaviors);
}

// --- hotkeys ---
struct Hotkey {
    int* key;
    bool light;
    bool wasDown;
    int bound; // the key wasDown belongs to
};

Hotkey g_hotkeys[] = {{&g_settings.toggleKey, false, false, 0}, {&g_settings.lightToggleKey, true, false, 0}};
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

bool CanToggle(int key)
{
    return game::MissionRunning() && !game::Editing() && !dk2ml::GameMenuOpen(g_api) && !game::ChatOpen() &&
           !Window_IsRebinding() && !Window_ImGuiWantsKeyboard() && !Chord(key);
}

void Toggle(int key, bool light)
{
    if (g_settings.perUnitLasers) {
        // Shift: the team, like the game's status keys. Not when the key is Shift itself.
        if (key != VK_SHIFT && KeyDown(VK_SHIFT)) {
            Units_ToggleAll(light);
        } else {
            Units_ToggleSelected(light);
        }
        return;
    }

    bool& on = Settings_On(Units_KindInView(light));
    on = !on;
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
    for (Hotkey& hotkey : g_hotkeys) {
        int key = *hotkey.key;
        bool down = g_api->IsGameFocused() && KeyDown(key);
        if (key != hotkey.bound) {
            hotkey.bound = key;
            hotkey.wasDown = down;
        }
        if (down && !hotkey.wasDown && CanToggle(key)) {
            Toggle(key, hotkey.light);
        }
        hotkey.wasDown = down;
    }
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
    out.Int(L"lightToggleKey", s.lightToggleKey);
    out.Int(L"lightAtStart", s.lightAtStart);
    out.Int(L"irLaserAtStart", s.irLaserAtStart);
    out.Int(L"lightShadows", s.lightShadows);
    out.Float(L"lightHeightCm", s.lightHeightCm);
    out.Float(L"lightBackCm", s.lightBackCm);
    out.Float(L"lightSideCm", s.lightSideCm);
    SaveLightStyle(out, L"light", s.light);
    SaveBehaviors(out, L"visible", s.visibleBehaviors);
    SaveBehaviors(out, L"nvg", s.nvgBehaviors);
    out.Int(L"behaviorsAtStart", s.behaviorsAtStart);
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

namespace {

uint32_t Rgb(int color, int red, int green, int blue)
{
    int index = std::clamp(color, 0, kColorPresetCount - 1);
    const ColorPreset& preset = kColorPresets[index];
    bool custom = index == kCustomColor;
    uint32_t r = static_cast<uint32_t>(custom ? red : preset.red);
    uint32_t g = static_cast<uint32_t>(custom ? green : preset.green);
    uint32_t b = static_cast<uint32_t>(custom ? blue : preset.blue);
    return (b << 16) | (g << 8) | r;
}

} // namespace

uint32_t StyleRgb(const LaserStyle& style)
{
    return Rgb(style.color, style.red, style.green, style.blue);
}

uint32_t StyleRgb(const LightStyle& style)
{
    return Rgb(style.color, style.red, style.green, style.blue);
}

bool& Settings_On(Kind kind)
{
    switch (kind) {
    case kVisibleLaser: return g_settings.visible.on;
    case kIrLaser: return g_settings.nvg.on;
    case kLight: return g_settings.light.on;
    default: return g_settings.behaviorsOn;
    }
}

bool Settings_AtStart(Kind kind)
{
    switch (kind) {
    case kVisibleLaser: return g_settings.visibleAtStart;
    case kIrLaser: return g_settings.irLaserAtStart;
    case kLight: return g_settings.lightAtStart;
    default: return g_settings.behaviorsAtStart;
    }
}

Behaviors& Settings_Behaviors(bool nvgView)
{
    return nvgView ? g_settings.nvgBehaviors : g_settings.visibleBehaviors;
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
    if (!Lasers_Init(api) || !Lights_Init(api) || !Engagement_Init(api)) {
        return 3;
    }
    if (!Units_AddReplayData(api)) {
        api->Log("per-trooper switches aren't kept in replays; they start from the start settings");
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

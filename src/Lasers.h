#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "dk2ml.hpp"

// --- settings (Plugin.cpp) ---
struct ColorPreset {
    const char* name;
    int red, green, blue;
};

constexpr int kColorPresetCount = 11;
constexpr int kRedColor = 0;
constexpr int kGreenColor = 1;
constexpr int kWhiteColor = 8;
constexpr int kCustomColor = kColorPresetCount - 1;
extern const ColorPreset kColorPresets[kColorPresetCount];

struct LaserStyle {
    int color = kRedColor;
    int red = 255, green = 0, blue = 0; // used when color is Custom
    float thickness = 1.0f;
    float strength = 1.0f; // beam alpha multiplier
    float dotBrightness = 1.0f;
    float dotSize = 1.0f;
    bool on = true; // per mission, not saved
};

struct LightStyle {
    int color = kWhiteColor;
    int red = 255, green = 255, blue = 255; // used when color is Custom
    float brightness = 2.0f; // colorMultiplier
    float rangeM = 10.0f;
    float widthDeg = 50.0f; // full cone angle
    bool glow = true;
    float glowSize = 0.4f;
    float glowAngleDeg = 65.0f; // max camera angle off the beam; 180: no limit
    bool cone = true;
    float coneLengthM = 3.0f;
    float coneStrength = 1.0f;
    bool on = true; // per mission, not saved
};

struct Range {
    float min, max;
};

constexpr Range kThicknessRange{0.25f, 5.0f};
constexpr Range kStrengthRange{0.25f, 4.0f};
constexpr Range kDotRange{0.0f, 4.0f};
constexpr Range kDotSizeRange{0.25f, 5.0f};
constexpr Range kHeightRange{-20.0f, 20.0f}; // cm, up positive
constexpr Range kBackRange{0.0f, 50.0f};   // cm
constexpr Range kSideRange{-10.0f, 10.0f}; // cm, right positive
constexpr Range kBrightnessRange{0.25f, 8.0f};
constexpr Range kLightRange{2.0f, 25.0f};   // m
constexpr Range kWidthRange{10.0f, 120.0f}; // degrees
constexpr Range kGlowSizeRange{0.25f, 4.0f};
constexpr Range kGlowAngleRange{20.0f, 180.0f}; // degrees
constexpr Range kConeLengthRange{0.5f, 10.0f}; // m
constexpr Range kConeStrengthRange{0.1f, 2.0f};
constexpr Range kHoldRange{0.25f, 5.0f};    // s
constexpr Range kStrobeRange{2.0f, 15.0f};  // Hz
constexpr Range kSettleRange{0.0f, 2.0f};   // s

// IR lasers show only in the NVG view, visible ones only outside it, the light in both.
enum Kind { kVisibleLaser, kIrLaser, kLight, kBehaviors, kKindCount };

enum BehaviorMode { kBehaviorStrobe, kBehaviorSteady, kBehaviorModeCount };

extern const char* const kBehaviorModeNames[kBehaviorModeCount];

// Strobe or hold on the laser/light while engaging (Engagement.cpp). One set per view.
struct Behaviors {
    bool onLaser = false;
    bool onLight = false;
    int mode = kBehaviorStrobe;
    bool onlyIfOn = false;     // only lasers/lights already switched on
    bool engageOnAim = true;   // also from aiming at a seen enemy, not only shots
    float aimSettleS = 0.3f;   // aim held this long
    float holdS = 1.5f;        // after the last shot or sighting
    float strobeHz = 6.0f;
};

struct Settings {
    bool visibleAtStart = false;
    int toggleKey = 'K'; // virtual-key code, 0 = unbound
    bool perUnitLasers = true; // also the lights
    float heightCm = 4.0f;
    float backCm = 11.4f;
    float sideCm = 1.0f;
    LaserStyle visible{kRedColor, 255, 0, 0, 1.0f, 2.0f, 1.0f};
    LaserStyle nvg{kGreenColor, 0, 255, 0, 1.0f, 1.0f, 1.0f};
    bool lightAtStart = false;
    bool irLaserAtStart = true;
    int lightToggleKey = 'J';
    bool lightShadows = true;
    float lightHeightCm = 1.0f; // default light position
    float lightBackCm = 4.0f;
    float lightSideCm = 3.5f;
    LightStyle light;
    Behaviors visibleBehaviors{false, true, kBehaviorStrobe, false, true, 0.15f, 1.0f, 5.0f}; // without night vision
    Behaviors nvgBehaviors{true, false, kBehaviorSteady, false, true, 0.15f, 0.75f};          // with night vision
    bool behaviorsAtStart = false;
    bool behaviorsOn = true;       // per mission, not saved
};

extern Settings g_settings;

constexpr Kind MakeKind(bool light, bool ir)
{
    return light ? kLight : ir ? kIrLaser : kVisibleLaser;
}

constexpr bool IsIr(Kind kind)
{
    return kind == kIrLaser;
}

constexpr bool IsLight(Kind kind)
{
    return kind == kLight;
}

void Settings_Save();
void Settings_SaveSoon();         // debounced
void Settings_SaveNow();          // flushes a pending SaveSoon
bool& Settings_On(Kind kind);     // global switch
bool Settings_AtStart(Kind kind);
Behaviors& Settings_Behaviors(bool nvgView);
uint32_t StyleRgb(const LaserStyle& style); // 0x00BBGGRR, Vertex3D::color's byte order
uint32_t StyleRgb(const LightStyle& style);
int GenericModifier(int vk); // left/right Shift, Ctrl, Alt to the generic key
std::string Utf8(std::wstring_view text);

// --- lasers/Lasers.cpp ---
bool Lasers_Init(const DK2ML_API* api);

// --- lasers/Lights.cpp ---
namespace game {
struct Vector3;
}

bool Lights_Init(const DK2ML_API* api);
// reachM: distance to the wall along the beam
void Lights_Show(void* human, const LightStyle& style, const game::Vector3& from, const game::Vector3& forward,
                 float reachM);
void Lights_Hide(void* human);
void Lights_Remove(void* human); // trooper dead or deleted
void Lights_MissionStart();

// --- lasers/Engagement.cpp ---
bool Engagement_Init(const DK2ML_API* api);
bool Engagement_Active(void* human, const Behaviors& behaviors); // fired, or aimed at a seen enemy, within holdS
bool Engagement_StrobeOn(void* human, float strobeHz); // the strobe's phase, offset per trooper
void Engagement_Forget(void* human);
void Engagement_MissionStart();

// --- lasers/Units.cpp ---
Kind Units_KindInView(bool light); // the light, or the IR laser in the NVG view, else the visible one
bool Units_On(void* human, Kind kind); // always true while perUnitLasers is off
bool Units_OnId(uint32_t id, Kind kind);
void* Units_Human(uint32_t id); // or null
void Units_Toggle(uint32_t id, Kind kind);
void Units_ToggleSelected(bool light); // the kind in view
void Units_ToggleAll(bool light);      // the kind in view, all the local player's troopers
void Units_SetAll(Kind kind, bool on); // also troopers that appear later
bool Units_AnyOn(Kind kind);           // any of the local player's troopers
void Units_SetPerUnit(bool perUnit); // sets Settings::perUnitLasers, carrying the on/off state over
void Units_Clear();                  // on MAP_LOADED
// The recording kept in replay files (init). False: the loader is older than API 3, replays use the start settings.
bool Units_AddReplayData(const DK2ML_API* api);

// --- lasers/Guns.cpp ---
struct GunOffset {
    float heightCm, backCm, sideCm;
};

using GunTable = std::map<std::string, GunOffset, std::less<>>;
using GunLaserTable = std::map<std::string, bool, std::less<>>; // also the lights

const char* Guns_Name(void* human); // or null
bool Guns_IsFirearm(void* human);
bool Guns_StockLaser(void* human); // the game's hasLaser
bool Guns_HasLaser(void* human); // with this gun's override
void Guns_SetLaser(std::string_view name, bool on);
void Guns_RemoveLaser(std::string_view name);
const GunLaserTable& Guns_Lasers();
bool Guns_HasLight(void* human); // override, else Guns_HasLaser
void Guns_SetLight(std::string_view name, bool on);
void Guns_RemoveLight(std::string_view name);
const GunLaserTable& Guns_Lights();
GunOffset Guns_Offset(void* human);
GunOffset* Guns_Find(std::string_view name);
void Guns_Set(std::string_view name, GunOffset offset);
void Guns_Remove(std::string_view name);
const GunTable& Guns_All();
GunOffset Guns_LightOffset(void* human); // this gun's, else the Settings one
GunOffset* Guns_FindLightOffset(std::string_view name);
void Guns_SetLightOffset(std::string_view name, GunOffset offset);
void Guns_RemoveLightOffset(std::string_view name);
const GunTable& Guns_LightOffsets();
void Guns_Load(const std::wstring& file);
void Guns_Save(const std::wstring& file);

// --- ui ---
bool UnitButton_Init(const DK2ML_API* api);
void MenuButton_Register(const DK2ML_API* api);
void Window_Toggle();
bool Window_IsRebinding();
bool Window_WantsInput();
bool Window_ImGuiWantsKeyboard(); // any ImGui window
void Window_Draw(); // every frame

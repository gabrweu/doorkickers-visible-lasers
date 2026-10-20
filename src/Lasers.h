// Cross-file declarations.
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

constexpr int kColorPresetCount = 11; // the last one is Custom: the style's own red, green, blue
constexpr int kRedColor = 0;
constexpr int kGreenColor = 1;
constexpr int kCustomColor = kColorPresetCount - 1;
extern const ColorPreset kColorPresets[kColorPresetCount];

// One kind of laser: visible (NVG off, faked) or IR (stock, NVG on).
struct LaserStyle {
    int color = kRedColor;
    int red = 255, green = 0, blue = 0; // used when color is Custom
    float thickness = 1.0f;
    float strength = 1.0f; // beam alpha multiplier
    float dotBrightness = 1.0f;
    float dotSize = 1.0f;
    bool on = true; // per mission, not saved
};

struct Range {
    float min, max;
};

constexpr Range kThicknessRange{0.25f, 5.0f};
constexpr Range kStrengthRange{0.25f, 4.0f};
constexpr Range kDotRange{0.0f, 4.0f};
constexpr Range kDotSizeRange{0.25f, 5.0f};
constexpr Range kHeightRange{0.0f, 20.0f}; // cm
constexpr Range kBackRange{0.0f, 50.0f};   // cm
constexpr Range kSideRange{-10.0f, 10.0f}; // cm, right positive

struct Settings {
    bool visibleAtStart = false;
    int toggleKey = 'K'; // virtual-key code, 0 = unbound
    bool perUnitLasers = true;
    float heightCm = 4.0f;
    float backCm = 12.5f;
    float sideCm = 0.0f;
    LaserStyle visible{kRedColor, 255, 0, 0, 1.0f, 2.0f, 1.0f};
    LaserStyle nvg{kGreenColor, 0, 255, 0, 1.0f, 1.0f, 1.0f}; // the stock look
};

extern Settings g_settings;

void Settings_Save();
void Settings_SaveSoon();                    // debounced
void Settings_SaveNow();                     // a pending debounced save, now
uint32_t StyleRgb(const LaserStyle& style); // 0x00BBGGRR, Vertex3D::color's byte order
int GenericModifier(int vk);                 // left/right Shift, Ctrl, Alt to the generic key
std::string Utf8(std::wstring_view text);

// --- lasers/Lasers.cpp ---
bool Lasers_Init(const DK2ML_API* api);

// --- lasers/Units.cpp: per-trooper on/off per kind, for the current mission ---
bool Units_LaserOn(void* human, bool ir); // always true while perUnitLasers is off
bool Units_LaserOnId(uint32_t id, bool ir);
void* Units_Human(uint32_t id); // the Human_Client with that entity id, or null
void Units_Toggle(uint32_t id, bool ir);
void Units_ToggleSelected(); // the kind in view
void Units_ToggleAll();      // the kind in view, every trooper the local player commands
void Units_SetAll(bool ir, bool on); // every trooper, also ones that appear later, until the next mission
bool Units_AnyOn(bool ir);           // any of the local player's troopers
void Units_SetPerUnit(bool perUnit); // switches Settings::perUnitLasers mid-mission, carrying the on/off state over
void Units_Clear();                  // mission start

// --- lasers/Guns.cpp: per-gun laser position and laser override ---
struct GunOffset {
    float heightCm, backCm, sideCm;
};

using GunTable = std::map<std::string, GunOffset, std::less<>>;
using GunLaserTable = std::map<std::string, bool, std::less<>>;

const char* Guns_Name(void* human); // equipped gun's name, or null
bool Guns_IsFirearm(void* human);
bool Guns_StockLaser(void* human); // the game's hasLaser
bool Guns_HasLaser(void* human); // with this gun's override
const bool* Guns_LaserOverride(void* item); // an Equipment_Template's override, or null
void Guns_SetLaser(std::string_view name, bool on);
void Guns_RemoveLaser(std::string_view name);
const GunLaserTable& Guns_Lasers();
GunOffset Guns_Offset(void* human);
GunOffset* Guns_Find(std::string_view name);
void Guns_Set(std::string_view name, GunOffset offset);
void Guns_Remove(std::string_view name);
const GunTable& Guns_All();
void Guns_Load(const std::wstring& file);
void Guns_Save(const std::wstring& file);

// --- ui ---
bool UnitButton_Init(const DK2ML_API* api);
void MenuButton_Register(const DK2ML_API* api);
void Window_Toggle();
bool Window_IsRebinding();
bool Window_WantsInput(); // keep the game's input while ImGui uses it
bool Window_ImGuiWantsKeyboard(); // any ImGui window is taking keys (typing)
void Window_Draw(); // every frame

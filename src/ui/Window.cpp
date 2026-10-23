// The in-mission ImGui settings window. Closes outside missions.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "BuildTime.h"
#include "Lasers.h"
#include "game/Game.h"

namespace {

namespace imgui = game::imgui;

bool g_open = false;

int* g_rebindSlot = nullptr;
bool g_keysHeldAtRebindStart[256] = {};

constexpr DWORD kResetConfirmMs = 3000;
DWORD g_resetArmedAt = 0;

constexpr int kVkUnassigned = 0x07;
constexpr int kVkReserved = 0xFF;

std::string KeyName(int vk)
{
    if (vk <= 0) {
        return "none";
    }
    switch (vk) { // no scan codes
    case VK_MBUTTON: return "Middle mouse";
    case VK_XBUTTON1: return "Mouse 4 (back)";
    case VK_XBUTTON2: return "Mouse 5 (forward)";
    }

    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lParam = static_cast<LONG>(scan) << 16;
    switch (vk) { // extended keys
        // clang-format off
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
        lParam |= 1 << 24;
        break;
        // clang-format on
    }

    wchar_t name[64] = {};
    int length = scan ? GetKeyNameTextW(lParam, name, 64) : 0;
    if (length > 0) {
        return Utf8(std::wstring_view(name, length));
    }
    char hex[16];
    sprintf_s(hex, "0x%02X", vk);
    return hex;
}

bool Button(const char* label)
{
    return imgui::ButtonEx(label, imgui::ImVec2{0, 0}, 0);
}

// ImGui's SmallButton, rebuilt: the game's one ignores its label.
bool SmallButton(const char* label)
{
    imgui::PushStyleVarVec2(static_cast<int>(imgui::ImGuiStyleVar_FramePadding.Get()), {8.0f, 0.0f});
    bool pressed =
        imgui::ButtonEx(label, imgui::ImVec2{0, 0}, static_cast<int>(imgui::ImGuiButtonFlags_AlignTextBaseLine.Get()));
    imgui::PopStyleVar(1);
    return pressed;
}

// Ctrl+click can type a value out of range.
bool Slider(const char* label, float* value, Range range, const char* format)
{
    bool changed = imgui::SliderScalar(label, static_cast<int>(imgui::ImGuiDataType_Float.Get()), value, &range.min,
                                       &range.max, format, 0);
    *value = std::clamp(*value, range.min, range.max);
    return changed;
}

void Separator()
{
    imgui::SeparatorEx(static_cast<int>(imgui::ImGuiSeparatorFlags_Horizontal.Get()));
}

constexpr imgui::ImVec4 kAccent{0.97f, 0.63f, 0.0f, 1.0f}; // the right-click toggles' amber
constexpr float kIndent = 10.0f;
constexpr int kStyleVars = 7; // PushStyle's count

int StyleVar(const dk2ml::Enum& var)
{
    return static_cast<int>(var.Get());
}

// Around Begin..End (Begin reads WindowPadding), so other windows keep their style.
void PushStyle()
{
    imgui::PushStyleVarVec2(StyleVar(imgui::ImGuiStyleVar_WindowPadding), {14.0f, 12.0f});
    imgui::PushStyleVarVec2(StyleVar(imgui::ImGuiStyleVar_FramePadding), {8.0f, 4.0f});
    imgui::PushStyleVarVec2(StyleVar(imgui::ImGuiStyleVar_ItemSpacing), {8.0f, 5.0f});
    imgui::PushStyleVarVec2(StyleVar(imgui::ImGuiStyleVar_ItemInnerSpacing), {6.0f, 4.0f});
    imgui::PushStyleVarFloat(StyleVar(imgui::ImGuiStyleVar_WindowRounding), 6.0f);
    imgui::PushStyleVarFloat(StyleVar(imgui::ImGuiStyleVar_FrameRounding), 4.0f);
    imgui::PushStyleVarFloat(StyleVar(imgui::ImGuiStyleVar_GrabRounding), 4.0f);
}

void PopStyle()
{
    imgui::PopStyleVar(kStyleVars);
}

void BeginSection(const char* title)
{
    imgui::Spacing();
    Separator();
    imgui::Spacing();
    imgui::TextColored(kAccent, "%s", title);
    imgui::Indent(kIndent);
}

void EndSection()
{
    imgui::Unindent(kIndent);
}

// Keys held when rebinding started count only after a release. Left/right mouse are the game's order buttons.
void UpdateRebind()
{
    if (!game::api->IsGameFocused()) {
        return;
    }

    for (int vk = VK_MBUTTON; vk < kVkReserved; ++vk) {
        if (vk == kVkUnassigned) {
            continue;
        }

        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (!down) {
            g_keysHeldAtRebindStart[vk] = false;
            continue;
        }
        if (g_keysHeldAtRebindStart[vk]) {
            continue;
        }

        if (vk != VK_ESCAPE) {
            *g_rebindSlot = GenericModifier(vk);
        }
        g_rebindSlot = nullptr;
        return;
    }
}

void KeyRow(const char* label, int* slot)
{
    if (g_rebindSlot == slot) {
        imgui::Text("%s: press a key or mouse button (Esc cancels)...", label);
        return;
    }

    imgui::AlignTextToFramePadding();
    imgui::Text("%s: %s", label, KeyName(*slot).c_str());
    imgui::SameLine(0.0f, -1.0f);
    char button[96];
    sprintf_s(button, "Change##%s", label);
    if (Button(button) && !g_rebindSlot) {
        g_rebindSlot = slot;
        for (int vk = 0; vk < 256; ++vk) {
            g_keysHeldAtRebindStart[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        }
    }
}

bool PresetName(void*, int index, const char** text)
{
    if (index < 0 || index >= kColorPresetCount) {
        return false;
    }
    *text = kColorPresets[index].name;
    return true;
}

bool BehaviorModeName(void*, int index, const char** text)
{
    if (index < 0 || index >= kBehaviorModeCount) {
        return false;
    }
    *text = kBehaviorModeNames[index];
    return true;
}

int ToByte(float channel)
{
    return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
}

// "Active right now" (per-trooper: any trooper's) and the start state.
bool DrawSwitches(const char* id, Kind kind, bool* atStart)
{
    bool changed = false;
    char label[96];
    sprintf_s(label, "Active right now##%s", id);
    if (g_settings.perUnitLasers) {
        bool on = Units_AnyOn(kind);
        if (imgui::Checkbox(label, &on)) {
            Units_SetAll(kind, on);
        }
    } else {
        imgui::Checkbox(label, &Settings_On(kind));
    }
    imgui::SameLine(0.0f, 24.0f);
    sprintf_s(label, "On at mission start##%s", id);
    changed |= imgui::Checkbox(label, atStart);
    return changed;
}

bool DrawColor(const char* id, int* color, int* red, int* green, int* blue)
{
    bool changed = false;
    char label[96];
    sprintf_s(label, "Color##%s", id);
    changed |= imgui::Combo(label, color, PresetName, nullptr, kColorPresetCount, -1);
    if (*color == kCustomColor) {
        float rgba[4] = {*red / 255.0f, *green / 255.0f, *blue / 255.0f, 1.0f};
        sprintf_s(label, "Custom color##%s", id);
        if (imgui::ColorEdit4(label, rgba, static_cast<int>(imgui::ImGuiColorEditFlags_NoAlpha.Get()))) {
            *red = ToByte(rgba[0]);
            *green = ToByte(rgba[1]);
            *blue = ToByte(rgba[2]);
            changed = true;
        }
    }
    return changed;
}

bool DrawStyle(const char* title, const char* id, Kind kind, LaserStyle& style, bool* atStart)
{
    char label[96];
    BeginSection(title);
    bool changed = DrawSwitches(id, kind, atStart);
    imgui::Spacing();
    changed |= DrawColor(id, &style.color, &style.red, &style.green, &style.blue);

    imgui::Spacing();
    sprintf_s(label, "Beam thickness##%s", id);
    changed |= Slider(label, &style.thickness, kThicknessRange, "%.2fx");
    sprintf_s(label, "Beam strength##%s", id);
    changed |= Slider(label, &style.strength, kStrengthRange, "%.2fx");
    sprintf_s(label, "Dot brightness##%s", id);
    changed |= Slider(label, &style.dotBrightness, kDotRange, "%.2fx");
    sprintf_s(label, "Dot size##%s", id);
    changed |= Slider(label, &style.dotSize, kDotSizeRange, "%.2fx");
    EndSection();
    return changed;
}

bool DrawLightStyle(const char* title, const char* id, Kind kind, LightStyle& style, bool* atStart)
{
    char label[96];
    BeginSection(title);
    bool changed = DrawSwitches(id, kind, atStart);
    imgui::Spacing();
    changed |= DrawColor(id, &style.color, &style.red, &style.green, &style.blue);

    imgui::Spacing();
    sprintf_s(label, "Brightness##%s", id);
    changed |= Slider(label, &style.brightness, kBrightnessRange, "%.2f");
    sprintf_s(label, "Range##%s", id);
    changed |= Slider(label, &style.rangeM, kLightRange, "%.1f m");
    sprintf_s(label, "Beam width##%s", id);
    changed |= Slider(label, &style.widthDeg, kWidthRange, "%.0f deg");

    imgui::Spacing();
    sprintf_s(label, "Lens glow##%s", id);
    changed |= imgui::Checkbox(label, &style.glow);
    if (style.glow) {
        sprintf_s(label, "Glow size##%s", id);
        changed |= Slider(label, &style.glowSize, kGlowSizeRange, "%.2fx");
        sprintf_s(label, "Glow seen within##%s", id);
        changed |= Slider(label, &style.glowAngleDeg, kGlowAngleRange, "%.0f deg");
    }
    sprintf_s(label, "Beam cone (stops at walls)##%s", id);
    changed |= imgui::Checkbox(label, &style.cone);
    if (style.cone) {
        sprintf_s(label, "Cone length##%s", id);
        changed |= Slider(label, &style.coneLengthM, kConeLengthRange, "%.1f m");
        sprintf_s(label, "Cone strength##%s", id);
        changed |= Slider(label, &style.coneStrength, kConeStrengthRange, "%.2fx");
    }
    EndSection();
    return changed;
}

bool DrawPosition(const char* id, const char* backLabel, float* heightCm, float* backCm, float* sideCm)
{
    bool changed = false;
    char label[96];
    sprintf_s(label, "Above / below barrel##%s", id);
    changed |= Slider(label, heightCm, kHeightRange, "%.1f cm");
    sprintf_s(label, "%s##%s", backLabel, id);
    changed |= Slider(label, backCm, kBackRange, "%.1f cm");
    sprintf_s(label, "Left / right of barrel##%s", id);
    changed |= Slider(label, sideCm, kSideRange, "%.1f cm");
    return changed;
}

bool DrawResetButton()
{
    bool resetArmed = g_resetArmedAt != 0 && GetTickCount() - g_resetArmedAt < kResetConfirmMs;
    if (!Button(resetArmed ? "Click again to reset all settings##reset" : "Reset all settings##reset")) {
        return false;
    }

    if (!resetArmed) {
        g_resetArmedAt = GetTickCount();
        return false;
    }
    // Keep the mission's on/off; Units_SetPerUnit switches the mode.
    bool perUnit = g_settings.perUnitLasers;
    bool on[kKindCount];
    for (int i = 0; i < kKindCount; ++i) {
        on[i] = Settings_On(static_cast<Kind>(i));
    }
    g_settings = Settings{};
    g_settings.perUnitLasers = perUnit;
    for (int i = 0; i < kKindCount; ++i) {
        Settings_On(static_cast<Kind>(i)) = on[i];
    }
    Units_SetPerUnit(Settings{}.perUnitLasers);
    g_rebindSlot = nullptr;
    g_resetArmedAt = 0;
    return true;
}

// No EndTabItem needed: NoPushId.
bool BeginTab(const char* label)
{
    return imgui::BeginTabItem(label, nullptr, static_cast<int>(imgui::ImGuiTabItemFlags_NoPushId.Get()));
}

bool DrawLasersTab()
{
    bool changed = false;
    Settings& s = g_settings;
    imgui::Spacing();
    KeyRow("Toggle lasers", &s.toggleKey);

    BeginSection("Laser position (guns without their own)");
    changed |= DrawPosition("laser", "Start behind muzzle", &s.heightCm, &s.backCm, &s.sideCm);
    imgui::TextDisabled("up and right are positive, down and left negative");
    EndSection();

    changed |=
        DrawStyle("Visible lasers (without night vision)", "visible", kVisibleLaser, s.visible, &s.visibleAtStart);
    changed |= DrawStyle("IR lasers (with night vision)", "nvg", kIrLaser, s.nvg, &s.irLaserAtStart);
    return changed;
}

bool DrawLightsTab()
{
    bool changed = false;
    Settings& s = g_settings;
    imgui::Spacing();
    KeyRow("Toggle lights", &s.lightToggleKey);

    BeginSection("Tactical lights");
    changed |= imgui::Checkbox("Walls block the lights (costs some performance)", &s.lightShadows);
    EndSection();
    BeginSection("Light position (guns without their own)");
    changed |= DrawPosition("light", "Behind muzzle", &s.lightHeightCm, &s.lightBackCm, &s.lightSideCm);
    imgui::TextDisabled("up and right are positive, down and left negative");
    EndSection();
    changed |= DrawLightStyle("Lights (with and without night vision)", "light", kLight, s.light, &s.lightAtStart);
    return changed;
}

// Returns the gun whose Remove was clicked, or empty.
std::string DrawOffsetList(const char* title, const char* id, const GunTable& table)
{
    BeginSection(title);
    if (table.empty()) {
        imgui::TextDisabled("none yet");
    }
    std::string removed;
    for (const auto& [gun, offset] : table) {
        float up = std::fabs(offset.heightCm);
        const char* upName = offset.heightCm < 0.0f ? "down" : "up";
        if (offset.sideCm == 0.0f) {
            imgui::Text("%s: %.1f cm %s, %.1f cm back", gun.c_str(), up, upName, offset.backCm);
        } else {
            imgui::Text("%s: %.1f cm %s, %.1f cm back, %.1f cm %s", gun.c_str(), up, upName, offset.backCm,
                        std::fabs(offset.sideCm), offset.sideCm > 0.0f ? "right" : "left");
        }
        imgui::SameLine(0.0f, -1.0f);
        std::string button = std::string("Remove##") + id + gun;
        if (SmallButton(button.c_str())) {
            removed = gun;
        }
    }
    EndSection();
    return removed;
}

bool DrawGunsTab()
{
    bool changed = false;
    imgui::Spacing();
    imgui::TextDisabled("Override position for specific guns. Select a trooper to edit.");

    void* input = *game::g_pGameInput;
    void* human = input ? game::GameInput_GetSelectedHuman(input) : nullptr;
    const char* name = human ? Guns_Name(human) : nullptr;
    BeginSection("Selected gun");
    if (!human) {
        imgui::TextDisabled("No trooper selected");
    } else if (!name) {
        imgui::TextDisabled("The selected trooper holds no gun");
    } else {
        imgui::Text("Gun: %s", name);
        if (Guns_IsFirearm(human)) {
            bool stock = Guns_StockLaser(human);
            bool laser = Guns_HasLaser(human);
            if (imgui::Checkbox("Laser capable##gunlaser", &laser)) {
                if (laser == stock) {
                    Guns_RemoveLaser(name);
                } else {
                    Guns_SetLaser(name, laser);
                }
                changed = true;
            }
            imgui::SameLine(0.0f, -1.0f);
            imgui::TextDisabled(stock ? "(the game gives it one)" : "(the game gives it none)");

            bool light = Guns_HasLight(human);
            if (imgui::Checkbox("Light capable##gunlight", &light)) {
                if (light == laser) {
                    Guns_RemoveLight(name);
                } else {
                    Guns_SetLight(name, light);
                }
                changed = true;
            }
            imgui::SameLine(0.0f, -1.0f);
            imgui::TextDisabled(Guns_Lights().count(name) ? "(set for this gun)" : "(follows the laser)");

            bool own = Guns_Find(name) != nullptr;
            if (imgui::Checkbox("Gun specific laser position##gun", &own)) {
                if (own) {
                    Guns_Set(name, {g_settings.heightCm, g_settings.backCm, g_settings.sideCm});
                } else {
                    Guns_Remove(name);
                }
                changed = true;
            }
            if (GunOffset* offset = Guns_Find(name)) {
                imgui::Indent(kIndent);
                changed |=
                    DrawPosition("gun", "Start behind muzzle", &offset->heightCm, &offset->backCm, &offset->sideCm);
                imgui::Unindent(kIndent);
            }

            bool ownLight = Guns_FindLightOffset(name) != nullptr;
            if (imgui::Checkbox("Gun specific light position##gunlightpos", &ownLight)) {
                if (ownLight) {
                    Guns_SetLightOffset(name,
                                        {g_settings.lightHeightCm, g_settings.lightBackCm, g_settings.lightSideCm});
                } else {
                    Guns_RemoveLightOffset(name);
                }
                changed = true;
            }
            if (GunOffset* offset = Guns_FindLightOffset(name)) {
                imgui::Indent(kIndent);
                changed |=
                    DrawPosition("gunlightpos", "Behind muzzle", &offset->heightCm, &offset->backCm, &offset->sideCm);
                imgui::Unindent(kIndent);
            }
        } else {
            imgui::TextDisabled("Not a firearm: no laser or light");
        }
    }
    EndSection();

    std::string removed = DrawOffsetList("Customized guns (laser position)", "laserpos", Guns_All());
    if (!removed.empty()) {
        Guns_Remove(removed);
        changed = true;
    }
    removed = DrawOffsetList("Customized guns (light position)", "lightpos", Guns_LightOffsets());
    if (!removed.empty()) {
        Guns_RemoveLightOffset(removed);
        changed = true;
    }

    BeginSection("Customized guns (laser capability override)");
    if (Guns_Lasers().empty()) {
        imgui::TextDisabled("none yet");
    }
    removed.clear();
    for (const auto& [gun, on] : Guns_Lasers()) {
        imgui::Text("%s: laser %s", gun.c_str(), on ? "added" : "removed");
        imgui::SameLine(0.0f, -1.0f);
        std::string button = "Remove##laser" + gun;
        if (SmallButton(button.c_str())) {
            removed = gun;
        }
    }
    if (!removed.empty()) {
        Guns_RemoveLaser(removed);
        changed = true;
    }
    EndSection();

    BeginSection("Customized guns (light override)");
    if (Guns_Lights().empty()) {
        imgui::TextDisabled("none yet");
    }
    removed.clear();
    for (const auto& [gun, on] : Guns_Lights()) {
        imgui::Text("%s: light %s", gun.c_str(), on ? "added" : "removed");
        imgui::SameLine(0.0f, -1.0f);
        std::string button = "Remove##light" + gun;
        if (SmallButton(button.c_str())) {
            removed = gun;
        }
    }
    if (!removed.empty()) {
        Guns_RemoveLight(removed);
        changed = true;
    }
    EndSection();
    return changed;
}

bool DrawBehaviors(const char* title, const char* id, Behaviors& b)
{
    bool changed = false;
    char label[96];
    BeginSection(title);
    imgui::Text("Behavior while engaging a target, on:");
    sprintf_s(label, "Laser##%s", id);
    changed |= imgui::Checkbox(label, &b.onLaser);
    imgui::SameLine(0.0f, 24.0f);
    sprintf_s(label, "Light##%s", id);
    changed |= imgui::Checkbox(label, &b.onLight);

    imgui::Spacing();
    sprintf_s(label, "Mode##%s", id);
    changed |= imgui::Combo(label, &b.mode, BehaviorModeName, nullptr, kBehaviorModeCount, -1);
    sprintf_s(label, "Only if the laser/light is already on##%s", id);
    changed |= imgui::Checkbox(label, &b.onlyIfOn);
    sprintf_s(label, "Start when aiming at a seen enemy##%s", id);
    changed |= imgui::Checkbox(label, &b.engageOnAim);
    imgui::TextDisabled("otherwise from the first shot");
    if (b.engageOnAim) {
        sprintf_s(label, "Aim held for##%s", id);
        changed |= Slider(label, &b.aimSettleS, kSettleRange, "%.2f s");
    }

    imgui::Spacing();
    sprintf_s(label, "Keep the behavior after##%s", id);
    changed |= Slider(label, &b.holdS, kHoldRange, "%.2f s");
    if (b.mode == kBehaviorStrobe) {
        sprintf_s(label, "Strobe speed##%s", id);
        changed |= Slider(label, &b.strobeHz, kStrobeRange, "%.1f Hz");
    }
    EndSection();
    return changed;
}

bool DrawBehaviorsTab()
{
    imgui::Spacing();
    imgui::TextDisabled("Visual only: enemies don't react.");
    BeginSection("Engagement behavior");
    bool changed = DrawSwitches("behave", kBehaviors, &g_settings.behaviorsAtStart);
    EndSection();
    changed |= DrawBehaviors("Without night vision", "visible", g_settings.visibleBehaviors);
    changed |= DrawBehaviors("With night vision", "nvg", g_settings.nvgBehaviors);
    return changed;
}

bool DrawSettings()
{
    bool changed = false;
    Settings& s = g_settings;
    if (g_rebindSlot) {
        UpdateRebind();
        changed |= g_rebindSlot == nullptr;
    }

    imgui::TextDisabled("era's Lights and Lasers version %s, built %s", PLUGIN_VERSION, PLUGIN_BUILD_TIME);

    BeginSection("Controls");
    bool perUnit = s.perUnitLasers;
    if (imgui::Checkbox("Per-trooper lasers and lights (buttons in the right-click menu)", &perUnit)) {
        Units_SetPerUnit(perUnit);
        changed = true;
    }
    EndSection();

    imgui::Spacing();
    if (imgui::BeginTabBar(nullptr, 0, static_cast<int>(imgui::ImGuiTabBarFlags_IsFocused.Get()))) {
        if (BeginTab("Lasers")) {
            changed |= DrawLasersTab();
        }
        if (BeginTab("Lights")) {
            changed |= DrawLightsTab();
        }
        if (BeginTab("Guns")) {
            changed |= DrawGunsTab();
        }
        if (BeginTab("Behaviors")) {
            changed |= DrawBehaviorsTab();
        }
        imgui::EndTabBar();
    }

    imgui::Spacing();
    Separator();
    imgui::Spacing();
    changed |= DrawResetButton();
    return changed;
}

} // namespace

void Window_Toggle()
{
    g_open = !g_open;
}

bool Window_IsRebinding()
{
    return g_rebindSlot != nullptr;
}

// Capturing blocks the map and HUD, so only while ImGui uses the input or a rebind waits.
bool Window_WantsInput()
{
    void* context = *imgui::GImGui;
    if (!g_open || !context) {
        return false;
    }

    int io = imgui::ImGuiContext_IO.Offset();
    return g_rebindSlot || dk2ml::At<bool>(context, io + imgui::ImGuiIO_WantCaptureMouse.Offset()) ||
           Window_ImGuiWantsKeyboard();
}

// Any ImGui window's: a focused text field or a Ctrl+clicked slider.
bool Window_ImGuiWantsKeyboard()
{
    void* context = *imgui::GImGui;
    return context &&
           dk2ml::At<bool>(context, imgui::ImGuiContext_IO.Offset() + imgui::ImGuiIO_WantCaptureKeyboard.Offset());
}

void Window_Draw()
{
    if (!g_open) {
        g_rebindSlot = nullptr;
        return;
    }

    bool open = game::MissionRunning() && !game::Editing();
    bool changed = false;
    if (open) {
        int flags =
            static_cast<int>(imgui::ImGuiWindowFlags_NoCollapse.Get() | imgui::ImGuiWindowFlags_AlwaysAutoResize.Get());
        PushStyle();
        if (imgui::Begin("era's Lights and Lasers##dk2ml_vlasers", &open, flags)) {
            changed = DrawSettings();
            imgui::SameLine(0.0f, -1.0f); // after Reset
            if (Button("Close")) {
                open = false;
            }
        }
        imgui::End();
        PopStyle();
    }

    if (changed) {
        Settings_SaveSoon();
    }
    if (!open) {
        g_open = false;
        g_rebindSlot = nullptr;
        Settings_SaveNow();
    }
}

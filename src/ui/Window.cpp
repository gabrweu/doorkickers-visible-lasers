// The in-mission settings window (ImGui), and the per-gun window glued to its right edge. Closes outside missions.
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

constexpr DWORD kSaveDelayMs = 500; // sliders change every frame while dragged
bool g_dirty = false;
DWORD g_lastChange = 0;

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
    case VK_RMENU: case VK_RCONTROL:
        lParam |= 1 << 24;
        break;
        // clang-format on
    }

    char name[64] = {};
    if (scan && GetKeyNameTextA(lParam, name, sizeof(name)) > 0) {
        return name;
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

constexpr imgui::ImVec4 kAccent{0.97f, 0.63f, 0.0f, 1.0f}; // the right-click toggles' "on" amber
constexpr float kIndent = 10.0f;
constexpr int kStyleVars = 7; // pushed by PushStyle

int StyleVar(const dk2ml::Enum& var)
{
    return static_cast<int>(var.Get());
}

// From before Begin (it reads WindowPadding) to after End, so other windows keep their style.
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

// A titled block: spaced off the one above, its content indented until EndSection.
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
        bool genericModifier = vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU;
        if (g_keysHeldAtRebindStart[vk] || genericModifier) {
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

    imgui::AlignTextToFramePadding(); // level with the button
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

int ToByte(float channel)
{
    return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
}

// id keeps the two sections' ImGui IDs apart.
bool DrawStyle(const char* title, const char* onLabel, const char* id, LaserStyle& style, bool* atStart = nullptr)
{
    bool changed = false;
    char label[96];
    BeginSection(title);

    sprintf_s(label, "%s##%s", onLabel, id);
    changed |= imgui::Checkbox(label, &style.on);
    imgui::SameLine(0.0f, 24.0f);
    if (atStart) {
        sprintf_s(label, "On at mission start##%s", id);
        changed |= imgui::Checkbox(label, atStart);
    } else {
        imgui::TextDisabled("(on at every mission start)");
    }

    imgui::Spacing();
    sprintf_s(label, "Color##%s", id);
    changed |= imgui::Combo(label, &style.color, PresetName, nullptr, kColorPresetCount, -1);
    if (style.color == kCustomColor) {
        float rgba[4] = {style.red / 255.0f, style.green / 255.0f, style.blue / 255.0f, 1.0f};
        sprintf_s(label, "Custom color##%s", id);
        if (imgui::ColorEdit4(label, rgba, static_cast<int>(imgui::ImGuiColorEditFlags_NoAlpha.Get()))) {
            style.red = ToByte(rgba[0]);
            style.green = ToByte(rgba[1]);
            style.blue = ToByte(rgba[2]);
            changed = true;
        }
    }

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
    bool visibleOn = g_settings.visible.on; // per mission, not a setting
    bool nvgOn = g_settings.nvg.on;
    g_settings = Settings{};
    g_settings.visible.on = visibleOn;
    g_settings.nvg.on = nvgOn;
    g_rebindSlot = nullptr;
    g_resetArmedAt = 0;
    return true;
}

bool DrawSettings()
{
    bool changed = false;
    Settings& s = g_settings;
    if (g_rebindSlot) {
        UpdateRebind();
        changed |= g_rebindSlot == nullptr;
    }

    imgui::TextDisabled("era's Lasers Enhanced version %s, built %s", PLUGIN_VERSION, PLUGIN_BUILD_TIME);

    BeginSection("Controls");
    changed |= imgui::Checkbox("Per-trooper lasers (button in the right-click menu)", &s.perUnitLasers);
    KeyRow("Toggle lasers", &s.toggleKey);
    EndSection();

    BeginSection("Laser Position (for guns without override)");
    changed |= Slider("Height above barrel", &s.heightCm, kHeightRange, "%.1f cm");
    changed |= Slider("Start behind muzzle", &s.backCm, kBackRange, "%.1f cm");
    changed |= Slider("Left / right of barrel", &s.sideCm, kSideRange, "%.1f cm");
    imgui::TextDisabled("right is positive, left negative");
    EndSection();

    changed |=
        DrawStyle("Visible lasers (without night vision)", "Active right now", "visible", s.visible, &s.visibleAtStart);
    changed |= DrawStyle("IR lasers (with night vision)", "Active right now", "nvg", s.nvg);

    imgui::Spacing();
    Separator();
    imgui::Spacing();
    changed |= DrawResetButton(); // Close follows on its line
    return changed;
}

bool DrawGunPanel()
{
    bool changed = false;
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
            imgui::TextDisabled(stock ? "(the game gives it one)" : "(the game DOESN'T give it)");
        } else {
            imgui::TextDisabled("Not a firearm: no laser");
        }

        bool own = Guns_Find(name) != nullptr;
        if (imgui::Checkbox("Gun specific position##gun", &own)) {
            if (own) {
                Guns_Set(name, {g_settings.heightCm, g_settings.backCm, g_settings.sideCm});
            } else {
                Guns_Remove(name);
            }
            changed = true;
        }
        if (GunOffset* offset = Guns_Find(name)) {
            imgui::Indent(kIndent);
            changed |= Slider("Height above barrel##gun", &offset->heightCm, kHeightRange, "%.1f cm");
            changed |= Slider("Start behind muzzle##gun", &offset->backCm, kBackRange, "%.1f cm");
            changed |= Slider("Left / right of barrel##gun", &offset->sideCm, kSideRange, "%.1f cm");
            imgui::Unindent(kIndent);
        }
    }
    EndSection();

    BeginSection("Customized guns (position)");
    if (Guns_All().empty()) {
        imgui::TextDisabled("none yet");
    }
    std::string removed;
    for (const auto& [gun, offset] : Guns_All()) {
        if (offset.sideCm == 0.0f) {
            imgui::Text("%s: %.1f cm up, %.1f cm back", gun.c_str(), offset.heightCm, offset.backCm);
        } else {
            imgui::Text("%s: %.1f cm up, %.1f cm back, %.1f cm %s", gun.c_str(), offset.heightCm, offset.backCm,
                        std::fabs(offset.sideCm), offset.sideCm > 0.0f ? "right" : "left");
        }
        imgui::SameLine(0.0f, -1.0f);
        std::string button = "Remove##" + gun;
        if (SmallButton(button.c_str())) {
            removed = gun;
        }
    }
    if (!removed.empty()) {
        Guns_Remove(removed);
        changed = true;
    }
    EndSection();

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
    return changed;
}

// No SetNextWindowPos: Pos is written before Begin through last frame's window (same context only), and after it for
// the first frame, which ImGui hides while sizing the window.
constexpr float kGunWindowGap = 8.0f;
void* g_gunWindow = nullptr;
void* g_gunWindowContext = nullptr;

bool DrawGunWindow(imgui::ImVec2 pos)
{
    void* context = *imgui::GImGui;
    if (!context) {
        return false;
    }
    if (g_gunWindow && g_gunWindowContext == context) {
        imgui::ImGuiWindow_Pos(g_gunWindow) = pos;
    }

    int flags =
        static_cast<int>(imgui::ImGuiWindowFlags_NoCollapse.Get() | imgui::ImGuiWindowFlags_AlwaysAutoResize.Get() |
                         imgui::ImGuiWindowFlags_NoMove.Get() | imgui::ImGuiWindowFlags_NoSavedSettings.Get());
    bool changed = false;
    PushStyle();
    if (imgui::Begin("Gun specific configuration##dk2ml_vlasers_guns", nullptr, flags)) {
        changed = DrawGunPanel();
    }
    g_gunWindow = imgui::ImGuiContext_CurrentWindow(context);
    g_gunWindowContext = context;
    if (g_gunWindow) {
        imgui::ImGuiWindow_Pos(g_gunWindow) = pos;
    }
    imgui::End();
    PopStyle();
    return changed;
}

bool RightOfCurrentWindow(imgui::ImVec2* pos)
{
    void* context = *imgui::GImGui;
    void* window = context ? imgui::ImGuiContext_CurrentWindow(context) : nullptr;
    if (!window) {
        return false;
    }

    imgui::ImVec2 origin = imgui::ImGuiWindow_Pos(window);
    imgui::ImVec2 size = imgui::ImGuiWindow_Size(window);
    *pos = {origin.x + size.x + kGunWindowGap, origin.y};
    return true;
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

// A capture blocks the whole map and HUD, so only while ImGui uses the input (R28). A rebind takes every key.
bool Window_WantsInput()
{
    void* context = *imgui::GImGui;
    if (!g_open || !context) {
        return false;
    }

    int io = imgui::ImGuiContext_IO.Offset();
    return g_rebindSlot || dk2ml::At<bool>(context, io + imgui::ImGuiIO_WantCaptureMouse.Offset()) ||
           dk2ml::At<bool>(context, io + imgui::ImGuiIO_WantCaptureKeyboard.Offset());
}

void Window_Draw()
{
    if (g_dirty && GetTickCount() - g_lastChange >= kSaveDelayMs) {
        g_dirty = false;
        Settings_Save();
    }

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
        if (imgui::Begin("era's Lasers Enhanced##dk2ml_vlasers", &open, flags)) {
            changed = DrawSettings();
            imgui::SameLine(0.0f, -1.0f);
            if (Button("Close")) {
                open = false;
            }
        }
        imgui::ImVec2 gunWindowPos{};
        bool besideIt = RightOfCurrentWindow(&gunWindowPos);
        imgui::End();
        PopStyle();
        if (open && besideIt) {
            changed |= DrawGunWindow(gunWindowPos);
        }
    }

    if (changed) {
        g_dirty = true;
        g_lastChange = GetTickCount();
    }
    if (!open) {
        g_open = false;
        g_rebindSlot = nullptr;
        if (g_dirty) {
            g_dirty = false;
            Settings_Save();
        }
    }
}

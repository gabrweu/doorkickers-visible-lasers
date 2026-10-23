// Fakes the night/NVG flags around Init and UpdateLaserLight, styles and moves the laser, and places the light
// (Lights.cpp) on its beam.
#include <algorithm>
#include <cmath>

#include "Lasers.h"
#include "game/Game.h"

namespace {

constexpr int kLightColorFloats = 6; // Vector3[2]

// UpdateLaserLight scratch: [0] human, [1] gun template given a laser for the call, [2] state, [3] night flag.
constexpr uint64_t kRestore = 1;
constexpr uint64_t kWasNight = 2;
constexpr uint64_t kWasNvg = 4;
constexpr uint64_t kFaked = 8; // visible laser view
constexpr uint64_t kLaserOn = 16;
constexpr uint64_t kLightOn = 32;

// Restored through the saved pointer: g_pGameCommon also points at the server.
bool* FlagIn(uint64_t slot)
{
    return reinterpret_cast<bool*>(slot);
}

float* LightColors(void* light, const dk2ml::Field<uint8_t>& params)
{
    return &dk2ml::At<float>(light, params.Offset() + game::sLightParams_colors.Offset());
}

// The laser light is animated, so m_light isn't copied to m_lightCurrent: colors and radius go into both. The
// multiplier is animated from m_light and set from the template's.
void StyleLight(void* light, const LaserStyle& style)
{
    uint32_t rgb = StyleRgb(style);
    float color[3] = {static_cast<float>(rgb & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                      static_cast<float>((rgb >> 16) & 0xFF) / 255.0f};
    for (const dk2ml::Field<uint8_t>* params : {&game::Light_Client_m_light, &game::Light_Client_m_lightCurrent}) {
        float* colors = LightColors(light, *params);
        for (int i = 0; i < kLightColorFloats; ++i) {
            colors[i] = color[i % 3];
        }
    }

    void* lightTemplate = game::Entity_Common_m_pTemplate(light);
    if (lightTemplate) {
        int templateParams = game::Light_Template_params.Offset();
        float baseBrightness =
            dk2ml::At<float>(lightTemplate, templateParams + game::sLightParams_colorMultiplier.Offset());
        float baseRadius = dk2ml::At<float>(lightTemplate, templateParams + game::sLightParams_radiusMeters.Offset());
        dk2ml::At<float>(light, game::Light_Client_m_light.Offset() + game::sLightParams_colorMultiplier.Offset()) =
            baseBrightness * style.dotBrightness;
        for (const dk2ml::Field<uint8_t>* params : {&game::Light_Client_m_light, &game::Light_Client_m_lightCurrent}) {
            dk2ml::At<float>(light, params->Offset() + game::sLightParams_radiusMeters.Offset()) =
                baseRadius * style.dotSize;
        }
    }
    game::Light_Client_m_dataChanged(light) = true;
}

// The game zeroes the colors of a hidden ray.
bool RayShown(void* ray)
{
    return game::QuadColor(ray, 0) != 0;
}

// The beam was computed only for the light.
void HideLaser(void* ray, void* light)
{
    for (int i = 0; i < game::kQuadVertices; ++i) {
        game::QuadColor(ray, i) = 0;
    }
    game::Light_Client_m_isLightDisabled(light) = true;
    game::Light_Client_m_dataChanged(light) = true;
}

// Keeps the game's flickering alpha, scaled.
void StyleRay(void* ray, const LaserStyle& style)
{
    uint32_t rgb = StyleRgb(style);
    for (int i = 0; i < game::kQuadVertices; ++i) {
        uint32_t& color = game::QuadColor(ray, i);
        if (color == 0) {
            continue;
        }

        float alpha = static_cast<float>(color >> 24) * style.strength;
        uint32_t a = static_cast<uint32_t>(std::clamp(alpha, 1.0f, 255.0f));
        color = (a << 24) | rgb;
    }
}

// A beam shifted across itself hits the wall shifted the same, so no new raycast is needed.
void ShapeRay(void* ray, float thickness, const game::Vector3& shift, float back)
{
    if (thickness == 1.0f && shift.x == 0.0f && shift.y == 0.0f && shift.z == 0.0f && back == 0.0f) {
        return;
    }

    float& width = dk2ml::At<float>(ray, game::RenderObject2D_size.Offset() + game::Vector2_y.Offset());
    float& length = dk2ml::At<float>(ray, game::RenderObject2D_size.Offset() + game::Vector2_x.Offset());
    game::Vector3& origin = game::RenderObject2D_origin(ray);
    const game::Vector3& forward = game::RenderObject2D_forward(ray);
    width *= thickness;
    length += back;
    origin.x += shift.x - forward.x * back / 2;
    origin.y += shift.y - forward.y * back / 2;
    origin.z += shift.z - forward.z * back / 2;
    game::RenderObject2D_bNeedsUpdate(ray) = true;
    game::RenderObject2D_UpdateRenderData(ray);
}

void ShiftLight(void* light, const game::Vector3& shift)
{
    if (shift.x == 0.0f && shift.y == 0.0f && shift.z == 0.0f) {
        return;
    }

    game::Vector3 origin = game::Entity_Common_m_origin(light);
    game::Matrix3 rotation = game::Rotation(light);
    origin.x += shift.x;
    origin.y += shift.y;
    origin.z += shift.z;
    game::Entity_Common_SetTransform(light, &origin, &rotation, game::Entity_Common_m_scale(light));
}

// Level and square to the beam; the sign from the trooper's m_left (world handedness unknown).
game::Vector3 RightOfBeam(void* human, const game::Vector3& forward)
{
    float level = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (level < 1e-4f) {
        return {0.0f, 0.0f, 0.0f};
    }

    game::Vector3 side{forward.z / level, 0.0f, -forward.x / level};
    const game::Vector3& left = game::Entity_Common_m_left(human);
    if (side.x * left.x + side.z * left.z > 0.0f) {
        side = {-side.x, 0.0f, -side.z};
    }
    return side;
}

bool g_initFaked = false;

// Init's laser conditions besides night.
bool GetsLaser(void* human)
{
    void* tmpl = game::Entity_Common_m_pTemplate(human);
    return !game::Editing() && tmpl && game::Human_Template_type(tmpl) == game::HUMAN_GOODGUY.Get();
}

// Night for Init, so every trooper gets a laser. scratch[0]: the faked flag, or 0.
int InitPre(DK2ML_Regs* regs, void*)
{
    bool* night = game::NightFlag();
    regs->scratch[0] = 0;
    if (night && !*night && GetsLaser(dk2ml::Arg<void*>(regs, 0))) {
        *night = true;
        regs->scratch[0] = reinterpret_cast<uint64_t>(night);
        g_initFaked = true;
    }
    return DK2ML_CALL_ORIGINAL;
}

void InitPost(DK2ML_Regs* regs, void*)
{
    if (regs->scratch[0]) {
        g_initFaked = false;
        *FlagIn(regs->scratch[0]) = false;
    }
}

// Inside a faked Init: the real day flag for the gear. scratch[0]: the flag, or 0.
int SetInventoryPre(DK2ML_Regs* regs, void*)
{
    bool* night = game::NightFlag();
    regs->scratch[0] = 0;
    if (g_initFaked && night && *night) {
        *night = false;
        regs->scratch[0] = reinterpret_cast<uint64_t>(night);
    }
    return DK2ML_CALL_ORIGINAL;
}

void SetInventoryPost(DK2ML_Regs* regs, void*)
{
    if (regs->scratch[0]) {
        *FlagIn(regs->scratch[0]) = true;
    }
}

// The switches, overridden by the engagement behavior while engaging.
bool Shown(void* human, Kind kind, bool hasGear, bool engaging, const Behaviors& behaviors)
{
    if (!hasGear) {
        return false;
    }
    bool wanted = Settings_On(kind) && Units_On(human, kind);
    if (!engaging || !(IsLight(kind) ? behaviors.onLight : behaviors.onLaser)) {
        return wanted;
    }
    if (!wanted && behaviors.onlyIfOn) {
        return false;
    }
    return behaviors.mode == kBehaviorSteady || Engagement_StrobeOn(human, behaviors.strobeHz);
}

// Fakes night + NVG to show a visible laser or the light, clears NVG to hide the IR laser. The beam is computed only
// for a gun with a laser, so a wanted light sets hasLaser for the call.
int UpdatePre(DK2ML_Regs* regs, void*)
{
    regs->scratch[0] = reinterpret_cast<uint64_t>(dk2ml::Arg<void*>(regs, 0));
    regs->scratch[1] = 0;
    regs->scratch[2] = 0;

    void* human = reinterpret_cast<void*>(regs->scratch[0]);
    bool* night = game::NightFlag();
    bool* nvg = game::NvgFlag();
    if (!night || !nvg || game::Editing() || !game::Human_Client_m_pLaserLight(human)) {
        return DK2ML_CALL_ORIGINAL;
    }

    bool irView = game::NvgView();
    const Behaviors& behaviors = Settings_Behaviors(irView);
    bool engaging = (behaviors.onLaser || behaviors.onLight) && Settings_On(kBehaviors) &&
                    Units_On(human, kBehaviors) && Engagement_Active(human, behaviors);
    bool laserOn = Shown(human, MakeKind(false, irView), Guns_HasLaser(human), engaging, behaviors);
    bool lightOn = Shown(human, kLight, Guns_HasLight(human), engaging, behaviors);
    bool need = laserOn || lightOn;
    uint64_t state =
        (*night ? kWasNight : 0) | (*nvg ? kWasNvg : 0) | (laserOn ? kLaserOn : 0) | (lightOn ? kLightOn : 0);

    void* item = game::Human_Client_m_pEquippedItem(human);
    if (need && Guns_IsFirearm(human) && !game::EqpFirearm_Template_hasLaser(item)) {
        regs->scratch[1] = reinterpret_cast<uint64_t>(item);
        game::EqpFirearm_Template_hasLaser(item) = true;
    }

    regs->scratch[3] = reinterpret_cast<uint64_t>(night);
    if (!need && irView) {
        *nvg = false;
        state |= kRestore;
    } else if (need && !irView) {
        *night = true;
        *nvg = true;
        state |= kRestore | kFaked;
    }
    regs->scratch[2] = state;
    return DK2ML_CALL_ORIGINAL;
}

void UpdatePost(DK2ML_Regs* regs, void*)
{
    uint64_t state = regs->scratch[2];
    if (void* item = reinterpret_cast<void*>(regs->scratch[1])) {
        game::EqpFirearm_Template_hasLaser(item) = false;
    }

    if (state & kRestore) {
        *FlagIn(regs->scratch[3]) = (state & kWasNight) != 0;
        if (bool* nvg = game::NvgFlag()) {
            *nvg = (state & kWasNvg) != 0;
        }
    }

    void* human = reinterpret_cast<void*>(regs->scratch[0]);
    void* light = game::Human_Client_m_pLaserLight(human); // null once the trooper died
    void* ray = game::Human_Client_m_pLaserRay(human);
    if (!light || !ray) {
        Lights_Remove(human);
        return;
    }
    if (!(state & (kLaserOn | kLightOn)) || !RayShown(ray)) {
        Lights_Hide(human);
        return;
    }

    bool faked = (state & kFaked) != 0;
    GunOffset offset = Guns_Offset(human);
    float side = offset.sideCm / 100.0f;
    const game::Vector3& forward = game::RenderObject2D_forward(ray);
    game::Vector3 right = side != 0.0f ? RightOfBeam(human, forward) : game::Vector3{0.0f, 0.0f, 0.0f};
    game::Vector3 shift{right.x * side, offset.heightCm / 100.0f, right.z * side};

    // Before ShapeRay moves the beam.
    if (state & kLightOn) {
        GunOffset at = Guns_LightOffset(human);
        float lightSide = at.sideCm / 100.0f;
        float back = at.backCm / 100.0f;
        game::Vector3 lightRight = lightSide != 0.0f ? RightOfBeam(human, forward) : game::Vector3{0.0f, 0.0f, 0.0f};
        const game::Vector3& middle = game::RenderObject2D_origin(ray);
        float hit = dk2ml::At<float>(ray, game::RenderObject2D_size.Offset() + game::Vector2_x.Offset());
        float behind = hit / 2 + back; // from the beam's middle
        game::Vector3 from{middle.x - forward.x * behind + lightRight.x * lightSide,
                           middle.y - forward.y * behind + at.heightCm / 100.0f,
                           middle.z - forward.z * behind + lightRight.z * lightSide};
        Lights_Show(human, g_settings.light, from, forward, hit + back);
    } else {
        Lights_Hide(human);
    }

    if (!(state & kLaserOn)) {
        HideLaser(ray, light);
        return;
    }

    const LaserStyle& style = faked ? g_settings.visible : g_settings.nvg;
    StyleRay(ray, style);
    StyleLight(light, style);
    ShapeRay(ray, style.thickness, shift, offset.backCm / 100.0f);
    ShiftLight(light, shift);
}

} // namespace

bool Lasers_Init(const DK2ML_API* api)
{
    return dk2ml::Hook(api, game::Human_Client_Init, InitPre, InitPost) &&
           dk2ml::Hook(api, game::Human_Client_SetInventory, SetInventoryPre, SetInventoryPost) &&
           dk2ml::Hook(api, game::Human_Client_UpdateLaserLight, UpdatePre, UpdatePost);
}

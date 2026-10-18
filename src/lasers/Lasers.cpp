// The game creates and shows a trooper's laser only at night with NVG on. Both hooks fake those two flags for the
// length of the call, then the post styles and moves whatever laser the game showed. The game redoes it every frame.
#include <algorithm>
#include <cmath>

#include "Lasers.h"
#include "game/Game.h"

namespace {

constexpr int kRayVertices = 4;
constexpr int kLightColorFloats = 6; // Vector3[2]

// scratch[2], pre to post
constexpr uint64_t kRestore = 1;
constexpr uint64_t kWasNight = 2;
constexpr uint64_t kWasNvg = 4;
constexpr uint64_t kFaked = 8; // shown by faking the flags: the visible style
constexpr uint64_t kOn = 16;

float* LightColors(void* light, const dk2ml::Field<uint8_t>& params)
{
    return &dk2ml::At<float>(light, params.Offset() + game::sLightParams_colors.Offset());
}

// The laser light flickers, so the game never copies m_light into m_lightCurrent (what the renderer reads): colors and
// radius go into both. Brightness is animated from m_light, so it's set from the template's, not multiplied in place.
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

uint32_t& RayColor(void* ray, int vertex)
{
    int vertices = game::RenderObject2D_quad.Offset() + game::Render2D_Quad_v.Offset();
    int stride = static_cast<int>(game::Vertex3D_size.Get());
    return dk2ml::At<uint32_t>(ray, vertices + vertex * stride + game::Vertex3D_color.Offset());
}

// The game zeroes the colors of a hidden ray.
bool RayShown(void* ray)
{
    return RayColor(ray, 0) != 0;
}

// Keeps the game's flickering alpha, scaled.
void StyleRay(void* ray, const LaserStyle& style)
{
    uint32_t rgb = StyleRgb(style);
    for (int i = 0; i < kRayVertices; ++i) {
        uint32_t& color = RayColor(ray, i);
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
    game::Matrix3 rotation = dk2ml::At<game::Matrix3>(light, game::Entity_Common_m_left.Offset());
    origin.x += shift.x;
    origin.y += shift.y;
    origin.z += shift.z;
    game::Entity_Common_SetTransform(light, &origin, &rotation, game::Entity_Common_m_scale(light));
}

// Level and square to the beam; the sign comes from the trooper's own left, as the world's handedness is unknown.
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

// Night for Init: every trooper gets a (hidden) laser on every map.
int InitPre(DK2ML_Regs* regs, void*)
{
    bool* night = game::NightFlag();
    regs->scratch[0] = 0;
    if (night && !*night) {
        *night = true;
        regs->scratch[0] = kRestore;
    }
    return DK2ML_CALL_ORIGINAL;
}

void InitPost(DK2ML_Regs* regs, void*)
{
    bool* night = game::NightFlag();
    if ((regs->scratch[0] & kRestore) && night) {
        *night = false;
    }
}

// Each view shows only its own kind: fake night + NVG to show a visible laser, clear NVG to hide an IR one.
int UpdatePre(DK2ML_Regs* regs, void*)
{
    regs->scratch[0] = reinterpret_cast<uint64_t>(dk2ml::Arg<void*>(regs, 0));
    regs->scratch[2] = 0;

    bool* night = game::NightFlag();
    bool* nvg = game::NvgFlag();
    if (!night || !nvg) {
        return DK2ML_CALL_ORIGINAL;
    }

    void* human = reinterpret_cast<void*>(regs->scratch[0]);
    bool irView = game::NvgView();
    bool on = (irView ? g_settings.nvg.on : g_settings.visible.on) && Units_LaserOn(human, irView);
    uint64_t state = (*night ? kWasNight : 0) | (*nvg ? kWasNvg : 0) | (on ? kOn : 0);
    regs->scratch[2] = state;
    if (!on && irView) {
        *nvg = false;
        regs->scratch[2] = state | kRestore;
    } else if (on && !irView) {
        *night = true;
        *nvg = true;
        regs->scratch[2] = state | kRestore | kFaked;
    }
    return DK2ML_CALL_ORIGINAL;
}

void UpdatePost(DK2ML_Regs* regs, void*)
{
    uint64_t state = regs->scratch[2];
    bool* night = game::NightFlag();
    bool* nvg = game::NvgFlag();
    if ((state & kRestore) && night && nvg) {
        *night = (state & kWasNight) != 0;
        *nvg = (state & kWasNvg) != 0;
    }

    void* human = reinterpret_cast<void*>(regs->scratch[0]);
    void* light = game::Human_Client_m_pLaserLight(human); // null once the trooper died
    void* ray = game::Human_Client_m_pLaserRay(human);
    if (!(state & kOn) || !light || !ray || !RayShown(ray)) {
        return;
    }

    const LaserStyle& style = (state & kFaked) ? g_settings.visible : g_settings.nvg;
    StyleRay(ray, style);
    StyleLight(light, style);

    GunOffset offset = Guns_Offset(human);
    float side = offset.sideCm / 100.0f; // cm to the game's meters
    game::Vector3 right =
        side != 0.0f ? RightOfBeam(human, game::RenderObject2D_forward(ray)) : game::Vector3{0.0f, 0.0f, 0.0f};
    game::Vector3 shift{right.x * side, offset.heightCm / 100.0f, right.z * side};
    ShapeRay(ray, style.thickness, shift, offset.backCm / 100.0f);
    ShiftLight(light, shift);
}

} // namespace

bool Lasers_Init(const DK2ML_API* api)
{
    return dk2ml::Hook(api, game::Human_Client_Init, InitPre, InitPost) &&
           dk2ml::Hook(api, game::Human_Client_UpdateLaserLight, UpdatePre, UpdatePost);
}

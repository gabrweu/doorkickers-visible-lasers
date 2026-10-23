// Tactical lights: a stock "Light Spot" per trooper plus the mod's glow and cone sprites (mod/entities/dk2ml_vlasers.xml),
// made on first use and placed every frame from the laser beam (Lasers.cpp), with a hand sway and a brightness flicker
// on top. Client-side: the AI doesn't see them.
#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_map>

#include "Lasers.h"
#include "game/Game.h"

namespace {

enum Part { kSpot, kGlow, kCone, kPartCount };

// The last two need the mod's data loaded.
constexpr const char* kTemplates[kPartCount] = {"Light Spot", "dk2ml_vlasers_glow", "dk2ml_vlasers_cone"};
constexpr int kLightColorFloats = 6; // Vector3[2]
constexpr float kPi = 3.14159265f;
constexpr float kTiltDeg = 8.0f; // a level cone at chest height misses the nearby floor
constexpr float kGlowSizeM = 0.3f; // at glowSize 1
constexpr float kGlowFadeDeg = 20.0f; // past glowAngleDeg
constexpr float kConeAlpha = 128.0f; // at coneStrength 1
constexpr float kMinConeM = 0.1f;
constexpr float kConeOverscan = 1.3f; // the texture's soft edge past the cone width ($overscan, make_fx_texture.ps1)
// Hand sway (at sway 1) and flicker (at flicker 1), on game time: still while paused, slowed by slow-mo, replayed alike.
constexpr float kSwayIdleDeg = 0.5f; // standing
constexpr float kSwayMoveDeg = 1.5f; // added at kRunSpeedMps
constexpr double kSwayHz = 0.3;
constexpr float kBobDeg = 0.6f; // the steps' bob at kRunSpeedMps
constexpr double kBobHz = 1.8;
constexpr float kRunSpeedMps = 3.5f;
constexpr float kSpeedSmoothS = 0.3f;
constexpr int kMaxStepMs = 500; // a longer gap (a replay jump) isn't movement
constexpr float kFlickerAmount = 0.05f;
constexpr double kFlickerHz = 3.0;

// The game can delete local lights itself (TryFreeingUpSomeEntities), so the id is checked against the entity pool.
struct Piece {
    uint32_t id;
    void* entity;
};

struct Tracked {
    Piece parts[kPartCount];
    bool shown;
    bool seeded; // lastMs and lastOrigin are set
    int lastMs; // game time
    game::Vector3 lastOrigin;
    float speed; // m/s, smoothed
};

std::unordered_map<void*, Tracked> g_lights; // by Human_Client
bool g_createFailed[kPartCount] = {}; // until the next mission
bool g_loggedFailure[kPartCount] = {};

void* Live(const Piece& piece)
{
    void* client = *game::g_pGameClient;
    if (!client || piece.id == 0 || piece.id >= game::kEntityPoolSize) {
        return nullptr;
    }
    void* const* pool = &dk2ml::At<void*>(client, game::GameCommon_m_entityPool.Offset());
    return pool[piece.id] == piece.entity ? piece.entity : nullptr;
}

void SetSpriteColor(void* entity, uint32_t color)
{
    void* sprite = game::Entity_Client_m_pRenderObject2D(entity);
    if (!sprite) {
        return;
    }
    for (int i = 0; i < game::kQuadVertices; ++i) {
        game::QuadColor(sprite, i) = color;
    }
}

// The entity's update rebuilds the quad every frame for local ids.
void SetSpriteSize(void* entity, float length, float width)
{
    void* sprite = game::Entity_Client_m_pRenderObject2D(entity);
    if (!sprite) {
        return;
    }
    int size = game::RenderObject2D_size.Offset();
    dk2ml::At<float>(sprite, size + game::Vector2_x.Offset()) = length;
    dk2ml::At<float>(sprite, size + game::Vector2_y.Offset()) = width;
    game::RenderObject2D_bNeedsUpdate(sprite) = true;
}

// Drops the shadow too: a moving shadowed light rebuilds every shadowed light's rays.
void Disable(void* light)
{
    game::Light_Client_m_isLightDisabled(light) = true;
    for (const dk2ml::Field<uint8_t>* params : {&game::Light_Client_m_light, &game::Light_Client_m_lightCurrent}) {
        dk2ml::At<int>(light, params->Offset() + game::sLightParams_limitType.Offset()) =
            static_cast<int>(game::LIGHT_LIMIT_NONE.Get());
    }
    game::Light_Client_m_dataChanged(light) = true;
}

void HidePart(Part part, void* entity)
{
    if (part == kSpot) {
        Disable(entity);
    } else {
        SetSpriteColor(entity, 0); // additive
    }
}

// Client frame only: CreateRuntimeEntity takes its id from g_pGameCommon. Null for an unknown template.
void* Create(Part part)
{
    void* client = *game::g_pGameClient;
    void* entity = client ? game::GameClient_CreateRuntimeEntity(client, kTemplates[part], nullptr, true) : nullptr;
    if (!entity) {
        g_createFailed[part] = true;
        if (!g_loggedFailure[part]) {
            game::api->Log("couldn't create a \"%s\" for a tactical light; none until the next mission",
                           kTemplates[part]);
            g_loggedFailure[part] = true;
        }
        return nullptr;
    }

    game::Light_Client_Init(entity);
    if (part != kSpot) {
        Disable(entity); // sprite only
        HidePart(part, entity);
    }
    return entity;
}

void* Get(Tracked& tracked, Part part, bool create)
{
    Piece& piece = tracked.parts[part];
    if (void* entity = Live(piece)) {
        return entity;
    }
    piece = {0, nullptr};
    if (!create || g_createFailed[part]) {
        return nullptr;
    }

    void* entity = Create(part);
    if (entity) {
        piece = {game::Entity_Common_m_id(entity), entity};
    }
    return entity;
}

game::Vector3 Normalized(game::Vector3 v)
{
    float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return length < 1e-6f ? game::Vector3{0.0f, 0.0f, 0.0f} : game::Vector3{v.x / length, v.y / length, v.z / length};
}

game::Vector3 Cross(const game::Vector3& a, const game::Vector3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

bool IsZero(const game::Vector3& v)
{
    return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f;
}

game::Vector3 Tilted(const game::Vector3& forward)
{
    float level = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (level < 1e-4f) {
        return forward;
    }

    float pitch = std::atan2(forward.y, level) - kTiltDeg * kPi / 180.0f;
    float across = std::cos(pitch) / level;
    return {forward.x * across, std::sin(pitch), forward.z * across};
}

// About -1..1, smooth, with no repeat in sight. phase in cycles; different phases don't move together.
float Wobble(double t, double phase)
{
    constexpr double kTau = 2.0 * 3.14159265358979;
    double sum = std::sin(kTau * (t + phase)) + 0.5 * std::sin(kTau * (2.3 * t + 2.17 * phase + 0.31)) +
                 0.25 * std::sin(kTau * (4.1 * t + 3.41 * phase + 0.73));
    return static_cast<float>(sum / 1.75);
}

// Per trooper, from its id, so troopers don't move in step and a replay moves the same.
double Phase(void* human, double offset)
{
    double phase = game::Entity_Common_m_id(human) * 0.6180339887 + offset;
    return phase - std::floor(phase);
}

// From the trooper's moves in game time; a pause or a jump leaves it alone.
void UpdateSpeed(Tracked& tracked, void* human, int nowMs)
{
    const game::Vector3& origin = game::Entity_Common_m_origin(human);
    int stepMs = nowMs - tracked.lastMs;
    if (tracked.seeded && stepMs == 0) {
        return;
    }
    if (tracked.seeded && stepMs > 0 && stepMs < kMaxStepMs) {
        float dx = origin.x - tracked.lastOrigin.x;
        float dz = origin.z - tracked.lastOrigin.z;
        float seconds = static_cast<float>(stepMs) / 1000.0f;
        float speed = std::sqrt(dx * dx + dz * dz) / seconds;
        tracked.speed += (speed - tracked.speed) * (1.0f - std::exp(-seconds / kSpeedSmoothS));
    }
    tracked.seeded = true;
    tracked.lastMs = nowMs;
    tracked.lastOrigin = origin;
}

// Turned by a slow wander, larger while moving, plus the steps' bob: yaw around up, then pitch as Tilted does.
game::Vector3 Swayed(const game::Vector3& forward, void* human, float speed, float sway, double t)
{
    float level = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (sway <= 0.0f || level < 1e-4f) {
        return forward;
    }

    float move = std::clamp(speed / kRunSpeedMps, 0.0f, 1.0f);
    float wander = kSwayIdleDeg + kSwayMoveDeg * move;
    double step = kBobHz * t + Phase(human, 0.83);
    float bob = kBobDeg * move;
    float yawDeg = sway * (wander * Wobble(kSwayHz * t, Phase(human, 0.0)) +
                           0.4f * bob * static_cast<float>(std::sin(kPi * step))); // a sway per stride
    float pitchDeg = sway * (wander * Wobble(kSwayHz * t, Phase(human, 0.29)) +
                             bob * static_cast<float>(std::sin(2.0 * kPi * step)));

    float yaw = yawDeg * kPi / 180.0f;
    float x = forward.x * std::cos(yaw) + forward.z * std::sin(yaw);
    float z = forward.z * std::cos(yaw) - forward.x * std::sin(yaw);
    float pitch = std::atan2(forward.y, level) + pitchDeg * kPi / 180.0f;
    float across = std::cos(pitch) / level;
    return {x * across, std::sin(pitch), z * across};
}

// The spot shines along m_forward; UpdateLightParams needs the three axes square to each other.
game::Matrix3 Facing(const game::Vector3& forward)
{
    game::Vector3 f = Normalized(forward);
    game::Vector3 left = Normalized(Cross({0.0f, 1.0f, 0.0f}, f));
    if (IsZero(left)) {
        left = {1.0f, 0.0f, 0.0f}; // straight up or down
    }
    return game::FromAxes(left, Cross(f, left), f);
}

// A sprite's length runs along m_left.
game::Matrix3 Along(const game::Vector3& direction)
{
    game::Vector3 left = Normalized(direction);
    game::Vector3 forward = Normalized(Cross(left, {0.0f, 1.0f, 0.0f}));
    if (IsZero(forward)) {
        forward = {0.0f, 0.0f, 1.0f}; // straight up or down
    }
    return game::FromAxes(left, Cross(forward, left), forward);
}

void WriteParams(void* light, const LightStyle& style, bool shown, float level)
{
    uint32_t rgb = StyleRgb(style);
    float color[3] = {static_cast<float>(rgb & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                      static_cast<float>((rgb >> 16) & 0xFF) / 255.0f};
    int limit =
        static_cast<int>((shown && g_settings.lightShadows ? game::LIGHT_LIMIT_SHADOWS : game::LIGHT_LIMIT_NONE).Get());

    // Both, so the renderer has the values this frame.
    for (const dk2ml::Field<uint8_t>* params : {&game::Light_Client_m_light, &game::Light_Client_m_lightCurrent}) {
        int base = params->Offset();
        float* colors = &dk2ml::At<float>(light, base + game::sLightParams_colors.Offset());
        for (int i = 0; i < kLightColorFloats; ++i) {
            colors[i] = color[i % 3];
        }
        dk2ml::At<float>(light, base + game::sLightParams_colorMultiplier.Offset()) = style.brightness * level;
        dk2ml::At<float>(light, base + game::sLightParams_radiusMeters.Offset()) = style.rangeM;
        dk2ml::At<float>(light, base + game::sLightParams_spotAngleDegrees.Offset()) = style.widthDeg / 2.0f;
        dk2ml::At<int>(light, base + game::sLightParams_limitType.Offset()) = limit;
    }
}

void ShowSpot(void* light, const LightStyle& style, const game::Vector3& from, const game::Vector3& aim, float level)
{
    game::Matrix3 rotation = Facing(aim);
    game::Entity_Common_SetTransform(light, &from, &rotation, game::Entity_Common_m_scale(light));
    WriteParams(light, style, true, level);
    game::Light_Client_m_isLightDisabled(light) = false;
    game::Light_Client_m_dataChanged(light) = true;
}

float Smoothstep(float edge0, float edge1, float x)
{
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 1 within glowAngleDeg of the beam, fading to 0 over kGlowFadeDeg.
float GlowSeen(const LightStyle& style, const game::Vector3& from, const game::Vector3& aim)
{
    if (style.glowAngleDeg >= kGlowAngleRange.max) {
        return 1.0f;
    }
    const game::Vector3& camera = *game::RenderObject2D_s_billboardCameraOrigin;
    game::Vector3 toCamera = Normalized({camera.x - from.x, camera.y - from.y, camera.z - from.z});
    game::Vector3 direction = Normalized(aim);
    float cosine = toCamera.x * direction.x + toCamera.y * direction.y + toCamera.z * direction.z;
    float angle = std::acos(std::clamp(cosine, -1.0f, 1.0f)) * 180.0f / kPi;
    return 1.0f - Smoothstep(style.glowAngleDeg, style.glowAngleDeg + kGlowFadeDeg, angle);
}

// Billboarded. Fades both alpha and color, whichever the blend uses.
void ShowGlow(void* glow, const LightStyle& style, const game::Vector3& from, const game::Vector3& aim, float level)
{
    float seen = std::min(GlowSeen(style, from, aim) * level, 1.0f);
    if (seen <= 0.0f) {
        HidePart(kGlow, glow);
        return;
    }

    game::Matrix3 rotation = Facing({0.0f, 0.0f, 1.0f});
    game::Entity_Common_SetTransform(glow, &from, &rotation, game::Entity_Common_m_scale(glow));
    float size = kGlowSizeM * style.glowSize;
    SetSpriteSize(glow, size, size);
    uint32_t rgb = StyleRgb(style);
    uint32_t color = static_cast<uint32_t>(std::lround(255.0f * seen)) << 24;
    for (int shift = 0; shift < 24; shift += 8) {
        color |= static_cast<uint32_t>(std::lround(static_cast<float>((rgb >> shift) & 0xFF) * seen)) << shift;
    }
    SetSpriteColor(glow, color);
}

// The texture's apex is at u = 0, so the sprite's middle sits half its length out. The far end is kConeOverscan times
// the cone width: the texture's soft edge reaches past it.
void ShowCone(void* cone, const LightStyle& style, const game::Vector3& from, const game::Vector3& aim, float reachM,
              float level)
{
    float length = std::min(style.coneLengthM, reachM);
    if (length < kMinConeM) {
        HidePart(kCone, cone);
        return;
    }

    game::Vector3 direction = Normalized(aim);
    game::Vector3 middle{from.x + direction.x * length / 2, from.y + direction.y * length / 2,
                         from.z + direction.z * length / 2};
    game::Matrix3 rotation = Along(direction);
    game::Entity_Common_SetTransform(cone, &middle, &rotation, game::Entity_Common_m_scale(cone));
    float width = kConeOverscan * 2.0f * length * std::tan(style.widthDeg / 2.0f * kPi / 180.0f);
    SetSpriteSize(cone, length, width);
    uint32_t alpha = static_cast<uint32_t>(std::clamp(kConeAlpha * style.coneStrength * level, 1.0f, 255.0f));
    SetSpriteColor(cone, (alpha << 24) | StyleRgb(style));
}

int DestructorPre(DK2ML_Regs* regs, void*)
{
    void* human = dk2ml::Arg<void*>(regs, 0);
    Lights_Remove(human);
    Engagement_Forget(human);
    return DK2ML_CALL_ORIGINAL;
}

} // namespace

bool Lights_Init(const DK2ML_API* api)
{
    return dk2ml::Hook(api, game::Human_Client_DeletingDestructor, DestructorPre);
}

void Lights_Show(void* human, const LightStyle& style, const game::Vector3& from, const game::Vector3& forward,
                 float reachM)
{
    if (game::Editing()) {
        return;
    }

    Tracked& tracked = g_lights[human];
    void* client = *game::g_pGameClient;
    int nowMs = client ? game::GameCommon_m_gameTime(client) : 0;
    UpdateSpeed(tracked, human, nowMs);
    double t = nowMs / 1000.0;
    game::Vector3 aim = Tilted(Swayed(forward, human, tracked.speed, style.sway, t));
    float level = 1.0f + style.flicker * kFlickerAmount * Wobble(kFlickerHz * t, Phase(human, 0.61));

    if (void* spot = Get(tracked, kSpot, true)) {
        ShowSpot(spot, style, from, aim, level);
    }
    if (void* glow = Get(tracked, kGlow, style.glow)) {
        if (style.glow) {
            ShowGlow(glow, style, from, aim, level);
        } else {
            HidePart(kGlow, glow);
        }
    }
    if (void* cone = Get(tracked, kCone, style.cone)) {
        if (style.cone) {
            ShowCone(cone, style, from, aim, reachM, level);
        } else {
            HidePart(kCone, cone);
        }
    }
    tracked.shown = true;
}

void Lights_Hide(void* human)
{
    if (g_lights.empty()) {
        return;
    }

    auto found = g_lights.find(human);
    if (found == g_lights.end() || !found->second.shown) {
        return;
    }
    for (int part = 0; part < kPartCount; ++part) {
        if (void* entity = Get(found->second, static_cast<Part>(part), false)) {
            HidePart(static_cast<Part>(part), entity);
        }
    }
    found->second.shown = false;
}

// Flagged EF_DELETE_ME; the game deletes them.
void Lights_Remove(void* human)
{
    if (g_lights.empty()) {
        return;
    }

    auto found = g_lights.find(human);
    if (found == g_lights.end()) {
        return;
    }
    for (int part = 0; part < kPartCount; ++part) {
        if (void* entity = Live(found->second.parts[part])) {
            HidePart(static_cast<Part>(part), entity);
            game::Entity_Common_m_flags(entity) |= static_cast<uint32_t>(game::EF_DELETE_ME.Get());
        }
    }
    g_lights.erase(found);
}

void Lights_MissionStart()
{
    std::fill(std::begin(g_createFailed), std::end(g_createFailed), false);
}

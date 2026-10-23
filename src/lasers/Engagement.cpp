// Which troopers are engaging. The client has no target: a trooper engages on each shot (EV_HUMAN_FIREARM_SHOT) and,
// optionally, after aiming at an enemy it sees for aimSettleS; it stays engaged for holdS. Client game time: a pause
// freezes the strobe.
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "Lasers.h"
#include "game/Game.h"

namespace {

constexpr float kAimRangeM = 25.0f;
constexpr float kAimConeDeg = 10.0f; // either side of the aim
constexpr float kPi = 3.14159265f;
constexpr uint32_t kStrobeOffsetMs = 137; // per trooper id
constexpr float kEyeHeightM = 1.8f; // as the game's sight checks

struct Aim {
    uint32_t enemy;
    int since;
};

std::unordered_map<void*, int> g_lastEngaged; // by Human_Client, game time
std::unordered_map<void*, Aim> g_aims; // by Human_Client

bool Now(int* now)
{
    void* client = *game::g_pGameClient;
    if (!client) {
        return false;
    }
    *now = game::GameCommon_m_gameTime(client);
    return true;
}

bool IsType(void* human, const dk2ml::Enum& type)
{
    void* tmpl = game::Entity_Common_m_pTemplate(human);
    return tmpl && game::Human_Template_type(tmpl) == type.Get();
}

// The nearest live enemy near the trooper's level aim, seen by the team (m_bHiddenInFOW) and by the trooper
// (HasLineOfSight). m_isSpotted only means "known hostile". 0 if none.
uint32_t AimedEnemy(void* human)
{
    const game::Vector3& aim = game::Human_Client_m_aimDirection(human);
    float aimLength = std::sqrt(aim.x * aim.x + aim.z * aim.z);
    if (aimLength < 1e-4f) {
        return 0;
    }

    const game::Vector3& from = game::Entity_Common_m_origin(human);
    game::Vector3 eye{from.x, kEyeHeightM, from.z};
    game::Vector3 level{aim.x / aimLength, 0.0f, aim.z / aimLength};
    float minCos = std::cos(kAimConeDeg * kPi / 180.0f);
    uint32_t visionBlock = static_cast<uint32_t>(game::COL_VISION_BLOCK.Get());
    uint32_t nearest = 0;
    float nearestDistance = kAimRangeM;
    game::EachHuman([&](void* enemy) {
        if (!IsType(enemy, game::HUMAN_BADGUY) || game::Entity_Common_m_health(enemy) <= 0 ||
            game::Human_Client_m_isSurrendered(enemy) || game::Entity_Client_m_bHiddenInFOW(enemy)) {
            return true;
        }

        const game::Vector3& to = game::Entity_Common_m_origin(enemy);
        float dx = to.x - from.x;
        float dz = to.z - from.z;
        float distance = std::sqrt(dx * dx + dz * dz);
        if (distance < 1e-3f || distance > nearestDistance || (dx * level.x + dz * level.z) / distance < minCos) {
            return true;
        }
        if (game::GameCommon_HasLineOfSight(&eye, &level, 2.0f * kAimConeDeg, enemy, visionBlock, false)) {
            nearest = game::Entity_Common_m_id(enemy);
            nearestDistance = distance;
        }
        return true;
    });
    return nearest;
}

bool AimSettled(void* human, int now, float settleS)
{
    uint32_t enemy = AimedEnemy(human);
    if (enemy == 0) {
        g_aims.erase(human);
        return false;
    }

    auto found = g_aims.find(human);
    if (found == g_aims.end() || found->second.enemy != enemy || now < found->second.since) {
        g_aims[human] = {enemy, now};
        return settleS <= 0.0f;
    }
    return now - found->second.since >= static_cast<int>(settleS * 1000.0f);
}

// One event per round. Door breaching shots don't count.
int ReceiveEventPre(DK2ML_Regs* regs, void*)
{
    void* human = dk2ml::Arg<void*>(regs, 0);
    int now = 0;
    if (dk2ml::Arg<int>(regs, 2) == static_cast<int>(game::EV_HUMAN_FIREARM_SHOT.Get()) && !game::Editing() &&
        IsType(human, game::HUMAN_GOODGUY) && game::Entity_Common_m_health(human) > 0 &&
        game::Human_Client_m_currentCommand(human) != static_cast<int>(game::HUMAN_CMD_SHOTGUN_DOOR.Get()) &&
        Now(&now)) {
        g_lastEngaged[human] = now;
    }
    return DK2ML_CALL_ORIGINAL;
}

} // namespace

bool Engagement_Init(const DK2ML_API* api)
{
    return dk2ml::Hook(api, game::Human_Client_Net_ReceiveEvent, ReceiveEventPre);
}

bool Engagement_Active(void* human, const Behaviors& behaviors)
{
    int now = 0;
    if (!Now(&now)) {
        return false;
    }
    if (behaviors.engageOnAim && AimSettled(human, now, behaviors.aimSettleS)) {
        g_lastEngaged[human] = now;
        return true;
    }

    auto found = g_lastEngaged.find(human);
    if (found == g_lastEngaged.end()) {
        return false;
    }
    int age = now - found->second;
    if (age < 0) { // a replay jumped back
        g_lastEngaged.erase(found);
        return false;
    }
    return age < static_cast<int>(behaviors.holdS * 1000.0f);
}

bool Engagement_StrobeOn(void* human, float strobeHz)
{
    int now = 0;
    if (!Now(&now)) {
        return true;
    }
    uint32_t period = static_cast<uint32_t>(1000.0f / strobeHz);
    if (period < 2) {
        return true;
    }
    uint32_t phase = (static_cast<uint32_t>(now) + game::Entity_Common_m_id(human) * kStrobeOffsetMs) % period;
    return phase < period / 2;
}

void Engagement_Forget(void* human)
{
    g_lastEngaged.erase(human);
    g_aims.erase(human);
}

void Engagement_MissionStart()
{
    g_lastEngaged.clear();
    g_aims.clear();
}

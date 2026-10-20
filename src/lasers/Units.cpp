// Per-trooper lasers: each kind has a mission start state, and a set of the trooper ids switched away from it.
#include <unordered_set>

#include "Lasers.h"
#include "game/Game.h"

namespace {

constexpr int kMaxNodes = 4096; // a broken list must not hang the game
constexpr int kMaxClients = 32;
constexpr int kMaxTroopers = 64;

// Indexed by ir. Taken at mission start, so a settings change mid-mission flips no one.
bool g_startOn[2] = {false, true};
std::unordered_set<uint32_t> g_flipped[2];

void SetKind(uint32_t id, bool ir, bool on)
{
    if (on == g_startOn[ir]) {
        g_flipped[ir].erase(id);
    } else {
        g_flipped[ir].insert(id);
    }
}

bool Replaying(void* gameClient)
{
    return (game::GameClient_m_flags(gameClient) & static_cast<uint32_t>(game::CGAMEFLAG_REPLAYING.Get())) != 0;
}

// The game's status-key filter in play: the local player's live, controllable squad members. In a replay, any live
// trooper.
bool Commandable(void* gameClient, void* human)
{
    void* tmpl = game::Entity_Common_m_pTemplate(human);
    if (!tmpl || game::Human_Template_type(tmpl) != game::HUMAN_GOODGUY.Get() ||
        game::Entity_Common_m_health(human) <= 0) {
        return false;
    }
    if (Replaying(gameClient)) {
        return true;
    }

    int clientIndex =
        dk2ml::At<int>(gameClient, game::GameClient_m_server.Offset() + game::GameClient_Server_clientIndex.Offset());
    return clientIndex >= 0 && clientIndex < kMaxClients &&
           (game::Human_Client_m_ownerMask(human) & (1u << clientIndex)) != 0 &&
           game::Human_Client_m_isControlable(human);
}

// Human_Client::typeList is intrusive: the walk starts at the list's next and ends at null or the sentinel. Stops
// early when visit returns false.
template <typename Visit> void EachHuman(Visit visit)
{
    void* list = game::Human_Client_typeList.Address();
    void* sentinel = game::LinkedList_Human_head(list);
    void* node = game::LinkedList_Human_next(list);
    for (int i = 0; node && node != sentinel && i < kMaxNodes; ++i) {
        void* human = game::LinkedList_Human_owner(node);
        node = game::LinkedList_Human_next(node);
        if (human && !visit(human)) {
            return;
        }
    }
}

int OwnTroopers(void* gameClient, void** out, int max)
{
    int count = 0;
    EachHuman([&](void* human) {
        if (Commandable(gameClient, human)) {
            out[count++] = human;
        }
        return count < max;
    });
    return count;
}

bool AnyOn(void* const* troopers, int count, bool ir)
{
    for (int i = 0; i < count; ++i) {
        if (Units_LaserOnId(game::Entity_Common_m_id(troopers[i]), ir)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool Units_LaserOn(void* human, bool ir)
{
    return !g_settings.perUnitLasers || Units_LaserOnId(game::Entity_Common_m_id(human), ir);
}

bool Units_LaserOnId(uint32_t id, bool ir)
{
    return g_startOn[ir] != (g_flipped[ir].count(id) != 0);
}

void* Units_Human(uint32_t id)
{
    void* found = nullptr;
    EachHuman([&](void* human) {
        if (game::Entity_Common_m_id(human) == id) {
            found = human;
        }
        return !found;
    });
    return found;
}

void Units_Toggle(uint32_t id, bool ir)
{
    std::unordered_set<uint32_t>& flipped = g_flipped[ir];
    if (!flipped.erase(id)) {
        flipped.insert(id);
    }
}

void Units_ToggleSelected()
{
    void* gameClient = *game::g_pGameClient;
    void* input = *game::g_pGameInput;
    void* human = gameClient && input ? game::GameInput_GetSelectedHuman(input) : nullptr;
    if (human && Commandable(gameClient, human)) {
        Units_Toggle(game::Entity_Common_m_id(human), game::NvgView());
    }
}

// Any on turns them all off, else all on, so a mixed team ends up the same.
void Units_ToggleAll()
{
    void* gameClient = *game::g_pGameClient;
    if (!gameClient) {
        return;
    }

    bool ir = game::NvgView();
    void* troopers[kMaxTroopers];
    int count = OwnTroopers(gameClient, troopers, kMaxTroopers);
    bool anyOn = AnyOn(troopers, count, ir);
    for (int i = 0; i < count; ++i) {
        SetKind(game::Entity_Common_m_id(troopers[i]), ir, !anyOn);
    }
}

void Units_SetAll(bool ir, bool on)
{
    g_startOn[ir] = on;
    g_flipped[ir].clear();
}

bool Units_AnyOn(bool ir)
{
    void* gameClient = *game::g_pGameClient;
    if (!gameClient) {
        return g_startOn[ir];
    }

    void* troopers[kMaxTroopers];
    int count = OwnTroopers(gameClient, troopers, kMaxTroopers);
    return count > 0 ? AnyOn(troopers, count, ir) : g_startOn[ir];
}

// Per-trooper mode keeps both global switches on; the troopers take over the global state, or hand theirs back.
void Units_SetPerUnit(bool perUnit)
{
    if (g_settings.perUnitLasers == perUnit) {
        return;
    }

    for (bool ir : {false, true}) {
        LaserStyle& kind = ir ? g_settings.nvg : g_settings.visible;
        if (perUnit) {
            Units_SetAll(ir, kind.on);
            kind.on = true;
        } else {
            kind.on = Units_AnyOn(ir);
        }
    }
    g_settings.perUnitLasers = perUnit;
}

void Units_Clear()
{
    g_startOn[0] = g_settings.visibleAtStart;
    g_startOn[1] = true;
    g_flipped[0].clear();
    g_flipped[1].clear();
}

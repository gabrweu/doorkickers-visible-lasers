// Per-trooper switches: per kind, a start state and the trooper ids flipped from it. Changes in play are recorded with
// their game time and kept in the replay file through the loader's replay data (AddReplayData, API 3).
#include <cstddef>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Lasers.h"
#include "game/Game.h"

namespace {

constexpr int kMaxClients = 32;
constexpr int kMaxTroopers = 64;
constexpr uint32_t kEveryTrooper = 0; // no trooper has entity id 0

// Indexed by Kind.
bool g_startOn[kKindCount] = {false, true, false, false};
std::unordered_set<uint32_t> g_flipped[kKindCount];

struct Change {
    int timeMs;
    uint32_t id; // or kEveryTrooper (Units_SetAll)
    Kind kind;
    bool on;
};

struct Recording {
    bool startOn[kKindCount] = {};
    std::vector<Change> changes; // in game-time order
};

Recording g_recording; // this mission, in play
Recording g_replay; // the replay being watched, from its file
bool g_haveReplay = false;

// The player's own changes while watching a replay, on top of the recording. -1: none.
int g_replayAll[kKindCount] = {};
std::unordered_set<uint32_t> g_replayFlipped[kKindCount];

bool Replaying(void* gameClient)
{
    return (game::GameClient_m_flags(gameClient) & static_cast<uint32_t>(game::CGAMEFLAG_REPLAYING.Get())) != 0;
}

void* ReplayClient()
{
    void* gameClient = *game::g_pGameClient;
    return gameClient && Replaying(gameClient) ? gameClient : nullptr;
}

bool LiveOn(uint32_t id, Kind kind)
{
    return g_startOn[kind] != (g_flipped[kind].count(id) != 0);
}

void Record(uint32_t id, Kind kind, bool on)
{
    if (void* gameClient = *game::g_pGameClient) {
        g_recording.changes.push_back({game::GameCommon_m_gameTime(gameClient), id, kind, on});
    }
}

// Recomputed from the start on each call, so scrubbing back undoes changes.
bool RecordedOn(const Recording& recording, uint32_t id, Kind kind, int nowMs)
{
    bool on = recording.startOn[kind];
    for (const Change& change : recording.changes) {
        if (change.timeMs > nowMs) {
            break;
        }
        if (change.kind == kind && (change.id == id || change.id == kEveryTrooper)) {
            on = change.on;
        }
    }
    return on;
}

bool ReplayOn(void* gameClient, uint32_t id, Kind kind)
{
    bool on = g_startOn[kind];
    if (g_replayAll[kind] >= 0) {
        on = g_replayAll[kind] != 0;
    } else if (g_haveReplay) {
        on = RecordedOn(g_replay, id, kind, game::GameCommon_m_gameTime(gameClient));
    }
    return on != (g_replayFlipped[kind].count(id) != 0);
}

void SetTo(uint32_t id, Kind kind, bool on)
{
    if (Units_OnId(id, kind) != on) {
        Units_Toggle(id, kind);
    }
}

// The replay data, little-endian: u16 format version, u16 kind count, u8 start state per kind, u32 change count, then
// per change i32 time, u32 id, u8 kind, u8 on.
constexpr char kReplayKey[] = "era.vlasers";
constexpr uint16_t kReplayFormat = 1;

void SaveReplay(DK2ML_ReplayWriter* out, void*)
{
    dk2ml::ReplayOut w{game::api, out};
    w.Put<uint16_t>(kReplayFormat);
    w.Put<uint16_t>(static_cast<uint16_t>(kKindCount));
    for (bool on : g_recording.startOn) {
        w.Put<uint8_t>(on ? 1 : 0);
    }
    w.Put<uint32_t>(static_cast<uint32_t>(g_recording.changes.size()));
    for (const Change& change : g_recording.changes) {
        w.Put<int32_t>(change.timeMs);
        w.Put<uint32_t>(change.id);
        w.Put<uint8_t>(static_cast<uint8_t>(change.kind));
        w.Put<uint8_t>(change.on ? 1 : 0);
    }
    if (w.Ok()) {
        game::api->Log("replay: saving %zu per-trooper switch changes", g_recording.changes.size());
    }
}

// Kinds this version doesn't know (a newer one's) are skipped; ones the data lacks keep the start settings.
bool ReadRecording(dk2ml::ByteReader& in, Recording* recording)
{
    for (int i = 0; i < kKindCount; ++i) {
        recording->startOn[i] = Settings_AtStart(static_cast<Kind>(i));
    }
    uint16_t format = 0;
    uint16_t kindCount = 0;
    if (!in.Get(&format) || format != kReplayFormat || !in.Get(&kindCount)) {
        return false;
    }
    for (int i = 0; i < kindCount; ++i) {
        uint8_t on = 0;
        if (!in.Get(&on)) {
            return false;
        }
        if (i < kKindCount) {
            recording->startOn[i] = on != 0;
        }
    }
    uint32_t changeCount = 0;
    if (!in.Get(&changeCount)) {
        return false;
    }
    for (uint32_t i = 0; i < changeCount; ++i) {
        int32_t timeMs = 0;
        uint32_t id = 0;
        uint8_t kind = 0;
        uint8_t on = 0;
        if (!in.Get(&timeMs) || !in.Get(&id) || !in.Get(&kind) || !in.Get(&on)) {
            return false;
        }
        if (kind < kKindCount) {
            recording->changes.push_back({timeMs, id, static_cast<Kind>(kind), on != 0});
        }
    }
    return true;
}

// A replay starts (also restarted or skipped through). No data: the troopers start from the start settings.
void LoadReplay(const void* data, size_t size, void*)
{
    g_haveReplay = false;
    dk2ml::ByteReader in{data, size};
    Recording recording;
    if (!data || !ReadRecording(in, &recording)) {
        game::api->Log("replay: no per-trooper switches in this replay, troopers start from the start settings");
        return;
    }

    g_replay = std::move(recording);
    g_haveReplay = true;
    game::api->Log("replay: %zu per-trooper switch changes", g_replay.changes.size());
}

// The game's status-key filter. In a replay, any live trooper.
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

int OwnTroopers(void* gameClient, void** out, int max)
{
    int count = 0;
    game::EachHuman([&](void* human) {
        if (Commandable(gameClient, human)) {
            out[count++] = human;
        }
        return count < max;
    });
    return count;
}

bool AnyOn(void* const* troopers, int count, Kind kind)
{
    for (int i = 0; i < count; ++i) {
        if (Units_OnId(game::Entity_Common_m_id(troopers[i]), kind)) {
            return true;
        }
    }
    return false;
}

} // namespace

Kind Units_KindInView(bool light)
{
    return MakeKind(light, game::NvgView());
}

bool Units_On(void* human, Kind kind)
{
    return !g_settings.perUnitLasers || Units_OnId(game::Entity_Common_m_id(human), kind);
}

bool Units_OnId(uint32_t id, Kind kind)
{
    void* replay = ReplayClient();
    return replay ? ReplayOn(replay, id, kind) : LiveOn(id, kind);
}

void* Units_Human(uint32_t id)
{
    void* found = nullptr;
    game::EachHuman([&](void* human) {
        if (game::Entity_Common_m_id(human) == id) {
            found = human;
        }
        return !found;
    });
    return found;
}

void Units_Toggle(uint32_t id, Kind kind)
{
    bool replaying = ReplayClient() != nullptr;
    std::unordered_set<uint32_t>& flipped = replaying ? g_replayFlipped[kind] : g_flipped[kind];
    if (!flipped.erase(id)) {
        flipped.insert(id);
    }
    if (!replaying) {
        Record(id, kind, LiveOn(id, kind));
    }
}

void Units_ToggleSelected(bool light)
{
    void* gameClient = *game::g_pGameClient;
    void* input = *game::g_pGameInput;
    void* human = gameClient && input ? game::GameInput_GetSelectedHuman(input) : nullptr;
    if (human && Commandable(gameClient, human)) {
        Units_Toggle(game::Entity_Common_m_id(human), Units_KindInView(light));
    }
}

// Any on: all off, else all on.
void Units_ToggleAll(bool light)
{
    void* gameClient = *game::g_pGameClient;
    if (!gameClient) {
        return;
    }

    Kind kind = Units_KindInView(light);
    void* troopers[kMaxTroopers];
    int count = OwnTroopers(gameClient, troopers, kMaxTroopers);
    bool anyOn = AnyOn(troopers, count, kind);
    for (int i = 0; i < count; ++i) {
        SetTo(game::Entity_Common_m_id(troopers[i]), kind, !anyOn);
    }
}

void Units_SetAll(Kind kind, bool on)
{
    if (ReplayClient()) {
        g_replayAll[kind] = on ? 1 : 0;
        g_replayFlipped[kind].clear();
        return;
    }
    g_startOn[kind] = on;
    g_flipped[kind].clear();
    Record(kEveryTrooper, kind, on);
}

bool Units_AnyOn(Kind kind)
{
    void* gameClient = *game::g_pGameClient;
    if (!gameClient) {
        return g_startOn[kind];
    }

    void* troopers[kMaxTroopers];
    int count = OwnTroopers(gameClient, troopers, kMaxTroopers);
    return count > 0 ? AnyOn(troopers, count, kind) : g_startOn[kind];
}

// Per-trooper mode keeps every global switch on.
void Units_SetPerUnit(bool perUnit)
{
    if (g_settings.perUnitLasers == perUnit) {
        return;
    }

    for (int i = 0; i < kKindCount; ++i) {
        Kind kind = static_cast<Kind>(i);
        bool& on = Settings_On(kind);
        if (perUnit) {
            Units_SetAll(kind, on);
            on = true;
        } else {
            on = Units_AnyOn(kind);
        }
    }
    g_settings.perUnitLasers = perUnit;
}

// Also on a replay's map load. Leaves the replay's recording alone: LoadReplay may run before or after it.
void Units_Clear()
{
    g_recording = Recording{};
    for (int i = 0; i < kKindCount; ++i) {
        g_startOn[i] = Settings_AtStart(static_cast<Kind>(i));
        g_flipped[i].clear();
        g_recording.startOn[i] = g_startOn[i];
        g_replayAll[i] = -1;
        g_replayFlipped[i].clear();
    }
}

bool Units_AddReplayData(const DK2ML_API* api)
{
    return dk2ml::AddReplayData(api, kReplayKey, SaveReplay, LoadReplay);
}

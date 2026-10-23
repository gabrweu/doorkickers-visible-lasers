// The per-trooper toggles in the right-click menu (gui/vlasers_unit.xml). The menu rebuilds only its own entries, so
// ours stay once moved in; a GUI reload makes new ones, which are moved and bound again.
#include "Lasers.h"
#include "game/Game.h"

namespace {

constexpr const char* kMenu = "#waypoint_menu";

struct Toggle {
    const char* item;
    const char* offButton;
    const char* onButton;
    const char* noneButton; // no laser/light on the gun, or no Behaviors set
    float x, y;
    Kind kind;
    void* wiredItem;
};

// On the stock toggles' arc (36 degree steps, radius 111). VIS and IR share a slot. 105 35 belongs to No Flagging
// Allowed.
Toggle g_toggles[] = {
    {"#dk2ml_vlasers_unit", "#dk2ml_vlasers_unit_off", "#dk2ml_vlasers_unit_on", "#dk2ml_vlasers_unit_none", 105.0f,
     -35.0f, kVisibleLaser, nullptr},
    {"#dk2ml_vlasers_unit_ir", "#dk2ml_vlasers_unit_ir_off", "#dk2ml_vlasers_unit_ir_on", "#dk2ml_vlasers_unit_ir_none",
     105.0f, -35.0f, kIrLaser, nullptr},
    {"#dk2ml_vlasers_light", "#dk2ml_vlasers_light_off", "#dk2ml_vlasers_light_on", "#dk2ml_vlasers_light_none",
     -105.0f, -35.0f, kLight, nullptr},
    {"#dk2ml_vlasers_behave", "#dk2ml_vlasers_behave_off", "#dk2ml_vlasers_behave_on", "#dk2ml_vlasers_behave_none",
     -105.0f, 35.0f, kBehaviors, nullptr},
};

uint32_t g_trooperId = 0; // whose menu is open
bool g_loggedFailure = false;

void ShowIfChanged(void* item, bool show)
{
    if (item && dk2ml::gui::IsShown(game::api, item) != show) {
        dk2ml::gui::Show(game::api, item, show);
    }
}

bool AnyBehavior(const Behaviors& behaviors)
{
    return behaviors.onLaser || behaviors.onLight;
}

// Behaviors in either view count. A trooper that can't be found gets the normal toggles.
bool Has(Kind kind, void* human)
{
    if (kind == kBehaviors) {
        return AnyBehavior(g_settings.visibleBehaviors) || AnyBehavior(g_settings.nvgBehaviors);
    }
    return !human || (IsLight(kind) ? Guns_HasLight(human) : Guns_HasLaser(human));
}

void Sync(const Toggle& toggle, void* item)
{
    bool has = Has(toggle.kind, Units_Human(g_trooperId));
    bool on = Units_OnId(g_trooperId, toggle.kind);
    ShowIfChanged(dk2ml::gui::Find(game::api, toggle.noneButton, item), !has);
    ShowIfChanged(dk2ml::gui::Find(game::api, toggle.onButton, item), has && on);
    ShowIfChanged(dk2ml::gui::Find(game::api, toggle.offButton, item), has && !on);
}

void Clicked(void*, float, float, void* user)
{
    const Toggle& toggle = *static_cast<const Toggle*>(user);
    if (g_trooperId == 0) {
        return;
    }
    Units_Toggle(g_trooperId, toggle.kind);
    Sync(toggle, dk2ml::gui::Find(game::api, toggle.item));
}

bool Wire(Toggle& toggle, void* item)
{
    const DK2ML_API* api = game::api;
    void* off = dk2ml::gui::Find(api, toggle.offButton, item);
    void* on = dk2ml::gui::Find(api, toggle.onButton, item);
    return off && on && dk2ml::gui::OnAction(api, off, game::EVENT_CLICK.Get(), Clicked, &toggle) &&
           dk2ml::gui::OnAction(api, on, game::EVENT_CLICK.Get(), Clicked, &toggle);
}

// Null until the item is in the menu with its clicks bound.
void* Attach(Toggle& toggle, void* item)
{
    const DK2ML_API* api = game::api;
    void* menu = dk2ml::gui::Find(api, kMenu);
    if (!menu) {
        return nullptr;
    }
    if (dk2ml::gui::Parent(api, item) == menu && item == toggle.wiredItem) {
        return item;
    }

    if (!dk2ml::gui::AddChild(api, menu, item) || !Wire(toggle, item)) {
        if (!g_loggedFailure) {
            api->Log("failed to add the per-trooper button %s to %s", toggle.item, kMenu);
            g_loggedFailure = true;
        }
        return nullptr;
    }
    toggle.wiredItem = item;
    dk2ml::gui::SetOrigin(api, item, toggle.x, toggle.y);
    return item;
}

int OpenPre(DK2ML_Regs* regs, void*)
{
    const void* waypoint = dk2ml::Arg<const void*>(regs, 1);
    g_trooperId =
        waypoint
            ? dk2ml::At<uint32_t>(waypoint, game::ActionWP_Client_m_action.Offset() + game::sActionWP_ownerId.Offset())
            : 0;
    return DK2ML_CALL_ORIGINAL;
}

// Lasers: only the kind in view.
bool InView(Kind kind)
{
    bool laser = kind == kVisibleLaser || kind == kIrLaser;
    return !laser || kind == Units_KindInView(false);
}

void OpenPost(DK2ML_Regs*, void*)
{
    for (Toggle& toggle : g_toggles) {
        void* item = dk2ml::gui::Find(game::api, toggle.item);
        if (!item) {
            continue;
        }
        bool wanted = g_settings.perUnitLasers && g_trooperId != 0 && !game::Editing() && InView(toggle.kind);
        if (!wanted) {
            ShowIfChanged(item, false);
            continue;
        }

        if (Attach(toggle, item)) {
            ShowIfChanged(item, true);
            Sync(toggle, item);
        }
    }
}

} // namespace

bool UnitButton_Init(const DK2ML_API* api)
{
    if (!game::GameGUI_OpenWaypointMenu.Resolved() || !game::ActionWP_Client_m_action.Resolved() ||
        !game::sActionWP_ownerId.Resolved() || !game::EVENT_CLICK.Resolved()) {
        return false;
    }
    return dk2ml::Hook(api, game::GameGUI_OpenWaypointMenu, OpenPre, OpenPost);
}

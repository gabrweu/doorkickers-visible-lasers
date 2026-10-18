// The VIS/IR toggles in the right-click menu (gui/vlasers_unit.xml). The menu rebuilds only its own entries, so our items
// stay once moved in; a GUI reload makes new ones, which get moved and bound again.
#include "Lasers.h"
#include "game/Game.h"

namespace {

constexpr const char* kMenu = "#waypoint_menu";

struct Toggle {
    const char* item;
    const char* offButton;
    const char* onButton;
    float x, y;
    bool ir;
    void* wiredItem; // the item whose Buttons are bound
};

// The next step on the stock toggles' arc, each side.
Toggle g_toggles[] = {
    {"#dk2ml_vlasers_unit", "#dk2ml_vlasers_unit_off", "#dk2ml_vlasers_unit_on", 105.0f, -35.0f, false, nullptr},
    {"#dk2ml_vlasers_unit_ir", "#dk2ml_vlasers_unit_ir_off", "#dk2ml_vlasers_unit_ir_on", -105.0f, -35.0f, true,
     nullptr},
};

uint32_t g_trooperId = 0; // whose menu is open
bool g_loggedFailure = false;

void ShowIfChanged(void* item, bool show)
{
    if (item && dk2ml::gui::IsShown(game::api, item) != show) {
        dk2ml::gui::Show(game::api, item, show);
    }
}

void Sync(const Toggle& toggle, void* item)
{
    bool on = Units_LaserOnId(g_trooperId, toggle.ir);
    ShowIfChanged(dk2ml::gui::Find(game::api, toggle.onButton, item), on);
    ShowIfChanged(dk2ml::gui::Find(game::api, toggle.offButton, item), !on);
}

void Clicked(void*, float, float, void* user)
{
    const Toggle& toggle = *static_cast<const Toggle*>(user);
    if (g_trooperId == 0) {
        return;
    }
    Units_Toggle(g_trooperId, toggle.ir);
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
            api->Log("failed to add the per-trooper laser button %s to %s", toggle.item, kMenu);
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

void OpenPost(DK2ML_Regs*, void*)
{
    bool* night = game::NightFlag();
    for (Toggle& toggle : g_toggles) {
        void* item = dk2ml::gui::Find(game::api, toggle.item);
        if (!item) {
            continue;
        }
        bool irShown = night && *night;
        bool wanted = g_settings.perUnitLasers && g_trooperId != 0 && (!toggle.ir || irShown);
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
    return dk2ml::Hook(api, game::GameGUI_OpenWaypointMenu, OpenPre, OpenPost);
}

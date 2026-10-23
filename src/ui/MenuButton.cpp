// The Esc-menu button that opens the settings window (loader API 2; none on an older loader).
#include "Lasers.h"
#include "game/Game.h"

namespace {

void Clicked(void*, float, float, void*)
{
    if (!game::Editing()) {
        Window_Toggle();
    }
}

} // namespace

void MenuButton_Register(const DK2ML_API* api)
{
    DK2ML_MenuButton b = {sizeof(b)};
    b.icon = "data/textures/gui/dk2ml_vlasers_normal.dds";
    b.iconHover = "data/textures/gui/dk2ml_vlasers_hover.dds";
    b.tooltip = "era's Lights and Lasers settings";
    b.onClick = Clicked;
    dk2ml::gui::AddMenuButton(api, b);
}

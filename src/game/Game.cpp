#include "game/Game.h"

namespace game {

const DK2ML_API* api = nullptr;

// hooked
dk2ml::Fn<void(void*)> Human_Client_Init{"Human_Client::Init"};
dk2ml::Fn<void(void*)> Human_Client_UpdateLaserLight{"Human_Client::UpdateLaserLight"};
dk2ml::Fn<void(void*, const void*, int)> GameGUI_OpenWaypointMenu{"GameGUI::OpenWaypointMenu", dk2ml::Optional};
dk2ml::Fn<void(void*, const void*)> Human_Client_SetInventory{"Human_Client::SetInventory"};
dk2ml::Fn<void*(void*, unsigned)> Human_Client_DeletingDestructor{"??_EHuman_Client@@UEAAPEAXI@Z"};
dk2ml::Fn<void(void*, int, int, unsigned, const void*)> Human_Client_Net_ReceiveEvent{
    "?Net_ReceiveEvent@Human_Client@@UEAAXW4eNetworkMask@@W4eEntityEvent@Entity_Common@@IAEBVBitPacker@@@Z"};

// called
dk2ml::Fn<void*(const void*)> GameInput_GetSelectedHuman{"GameInput::GetSelectedHuman"};
dk2ml::Fn<void(void*)> RenderObject2D_UpdateRenderData{"RenderObject2D::UpdateRenderData"};
// overloaded: (const Vector3&, const Matrix3&, float)
dk2ml::Fn<void(void*, const Vector3*, const Matrix3*, float)> Entity_Common_SetTransform{
    "?SetTransform@Entity_Common@@QEAAXAEBVVector3@@AEBVMatrix3@@M@Z"};
dk2ml::Fn<void*(void*, const char*, const void*, bool)> GameClient_CreateRuntimeEntity{
    "GameClient::CreateRuntimeEntity"};
dk2ml::Fn<void(void*)> Light_Client_Init{"Light_Client::Init"};
dk2ml::Fn<bool(const Vector3*, const Vector3*, float, const void*, uint32_t, bool)> GameCommon_HasLineOfSight{
    "?HasLineOfSight@GameCommon@@SA_NAEBVVector3@@0MPEBVEntity_Common@@I_N@Z"};

// globals
dk2ml::Global<void*> g_pGameCommon{"g_pGameCommon"};
dk2ml::Global<void*> g_pRenderer{"g_pRenderer"};
dk2ml::Global<void*> g_pGameGUI{"g_pGameGUI"};
dk2ml::Global<void*> g_pEditor{"g_pEditor"};
dk2ml::Global<void*> g_pGameInput{"g_pGameInput"};
dk2ml::Global<void*> g_pGameClient{"g_pGameClient"};
dk2ml::Global<uint8_t> Human_Client_typeList{"?typeList@Human_Client@@2V?$LinkedList@VHuman_Client@@@@A"};
dk2ml::Global<Vector3> RenderObject2D_s_billboardCameraOrigin{"?s_billboardCameraOrigin@RenderObject2D@@2VVector3@@A"};

// fields
dk2ml::Field<bool> GameCommon_m_isNightTime{"GameCommon", "m_isNightTime"};
dk2ml::Field<int> GameCommon_m_gameTime{"GameCommon", "m_gameTime"};
dk2ml::Field<uint8_t> GameRenderer_m_renderOptions{"GameRenderer", "m_renderOptions"};
dk2ml::Field<bool> sRenderOptions_nvg{"GameRenderer::sRenderOptions", "nvg"};

dk2ml::Field<void*> Human_Client_m_pLaserLight{"Human_Client", "m_pLaserLight"};
dk2ml::Field<void*> Human_Client_m_pLaserRay{"Human_Client", "m_pLaserRay"};

dk2ml::Field<uint8_t> RenderObject2D_quad{"RenderObject2D", "quad"};
dk2ml::Field<Vector3> RenderObject2D_origin{"RenderObject2D", "origin"};
dk2ml::Field<Vector3> RenderObject2D_forward{"RenderObject2D", "forward"};
dk2ml::Field<uint8_t> RenderObject2D_size{"RenderObject2D", "size"};
dk2ml::Field<bool> RenderObject2D_bNeedsUpdate{"RenderObject2D", "bNeedsUpdate"};
dk2ml::Field<float> Vector2_x{"Vector2", "x"};
dk2ml::Field<float> Vector2_y{"Vector2", "y"};
dk2ml::Field<uint8_t> Render2D_Quad_v{"Render2D::Quad", "v"};
dk2ml::Field<uint32_t> Vertex3D_color{"Render::Vertex3D", "color"};
dk2ml::TypeSize Vertex3D_size{"Render::Vertex3D"};

dk2ml::Field<uint8_t> Light_Client_m_light{"Light_Client", "m_light"};
dk2ml::Field<uint8_t> Light_Client_m_lightCurrent{"Light_Client", "m_lightCurrent"};
dk2ml::Field<bool> Light_Client_m_dataChanged{"Light_Client", "m_dataChanged"};
dk2ml::Field<bool> Light_Client_m_isLightDisabled{"Light_Client", "m_isLightDisabled"};
dk2ml::Field<uint8_t> sLightParams_colors{"sLightParams", "colors"};
dk2ml::Field<float> sLightParams_colorMultiplier{"sLightParams", "colorMultiplier"};
dk2ml::Field<float> sLightParams_radiusMeters{"sLightParams", "radiusMeters"};
dk2ml::Field<float> sLightParams_spotAngleDegrees{"sLightParams", "spotAngleDegrees"};
dk2ml::Field<int> sLightParams_limitType{"sLightParams", "limitType"};
dk2ml::Field<uint8_t> Light_Template_params{"Light_Template", "params"};
dk2ml::Field<void*> Entity_Common_m_pTemplate{"Entity_Common", "m_pTemplate"};
dk2ml::Field<uint32_t> Entity_Common_m_id{"Entity_Common", "m_id"};
dk2ml::Field<float> Entity_Common_m_health{"Entity_Common", "m_health"};
dk2ml::Field<Vector3> Entity_Common_m_origin{"Entity_Common", "m_origin"};
dk2ml::Field<Vector3> Entity_Common_m_left{"Entity_Common", "m_left"};
dk2ml::Field<Vector3> Entity_Common_m_up{"Entity_Common", "m_up"};
dk2ml::Field<Vector3> Entity_Common_m_forward{"Entity_Common", "m_forward"};
dk2ml::Field<float> Entity_Common_m_scale{"Entity_Common", "m_scale"};
dk2ml::Field<uint32_t> Entity_Common_m_flags{"Entity_Common", "m_flags"};
dk2ml::Field<uint8_t> GameCommon_m_entityPool{"GameCommon", "m_entityPool"};
dk2ml::Field<uint8_t> ActionWP_Client_m_action{"ActionWP_Client", "m_action", dk2ml::Optional};
dk2ml::Field<uint32_t> sActionWP_ownerId{"sActionWP", "ownerId", dk2ml::Optional};

dk2ml::Field<uint8_t> GameClient_m_server{"GameClient", "m_server"};
dk2ml::Field<int> GameClient_Server_clientIndex{"GameClient::Server", "clientIndex"};
dk2ml::Field<uint32_t> GameClient_m_flags{"GameClient", "m_flags"};
dk2ml::Field<void*> LinkedList_Human_head{"LinkedList<Human_Client>", "head"};
dk2ml::Field<void*> LinkedList_Human_next{"LinkedList<Human_Client>", "next"};
dk2ml::Field<void*> LinkedList_Human_owner{"LinkedList<Human_Client>", "owner"};
dk2ml::Field<uint32_t> Human_Client_m_ownerMask{"Human_Client", "m_ownerMask"};
dk2ml::Field<bool> Human_Client_m_isControlable{"Human_Client", "m_isControlable"};
dk2ml::Field<int> Human_Template_type{"Human_Template", "type"};
dk2ml::Field<Vector3> Human_Client_m_aimDirection{"Human_Client", "m_aimDirection"};
dk2ml::Field<bool> Human_Client_m_isSurrendered{"Human_Client", "m_isSurrendered"};
dk2ml::Field<int> Human_Client_m_currentCommand{"Human_Client", "m_currentCommand"};
dk2ml::Field<bool> Entity_Client_m_bHiddenInFOW{"Entity_Client", "m_bHiddenInFOW"};
dk2ml::Field<void*> Entity_Client_m_pRenderObject2D{"Entity_Client", "m_pRenderObject2D"};

dk2ml::Field<void*> Human_Client_m_pEquippedItem{"Human_Client", "m_pEquippedItem"};
dk2ml::Field<uint8_t> Equipment_Template_name{"Equipment_Template", "name"};
dk2ml::Field<int> Equipment_Template_type{"Equipment_Template", "type"};
dk2ml::Field<bool> EqpFirearm_Template_hasLaser{"EqpFirearm_Template", "hasLaser"};
dk2ml::Field<const char*> HashedString_m_pString{"HashedString", "m_pString"};

dk2ml::Field<void*> GameGUI_m_pChatBox{"GameGUI", "m_pChatBox"};
dk2ml::Field<bool> GUI_Item_m_hidden{"GUI::Item", "m_hidden"};

// enum values
dk2ml::Enum CGAMESTATE_RUNNING{"GameClient::eCGameState", "CGAMESTATE_RUNNING"};
dk2ml::Enum CGAMEFLAG_REPLAYING{"GameClient::eCGameFlags", "CGAMEFLAG_REPLAYING"};
dk2ml::Enum HUMAN_GOODGUY{"eHumanType", "HUMAN_GOODGUY"};
dk2ml::Enum HUMAN_BADGUY{"eHumanType", "HUMAN_BADGUY"};
dk2ml::Enum HUMAN_CMD_SHOTGUN_DOOR{"eHumanCommand", "HUMAN_CMD_SHOTGUN_DOOR"};
dk2ml::Enum EV_HUMAN_FIREARM_SHOT{"Entity_Common::eEntityEvent", "EV_HUMAN_FIREARM_SHOT"};
dk2ml::Enum COL_VISION_BLOCK{"eCollisionFlags", "COL_VISION_BLOCK"};
dk2ml::Enum EVENT_CLICK{"GUI::Item::eItemEventType", "EVENT_CLICK", dk2ml::Optional};
dk2ml::Enum EQUIPMENT_FIREARM{"EquipmentType::eType", "FIREARM"};
dk2ml::Enum LIGHT_LIMIT_NONE{"eLightLimitType", "LIGHT_LIMIT_NONE"};
dk2ml::Enum LIGHT_LIMIT_SHADOWS{"eLightLimitType", "LIGHT_LIMIT_SHADOWS"};
dk2ml::Enum EF_DELETE_ME{"Entity_Common::eEntityFlags", "EF_DELETE_ME"};

namespace imgui {

dk2ml::Fn<bool(const char*, bool*, int)> Begin{"ImGui::Begin"};
dk2ml::Fn<void()> End{"ImGui::End"};
dk2ml::Fn<bool(const char*, bool*)> Checkbox{"ImGui::Checkbox"};
dk2ml::Fn<bool(const char*, int, void*, const void*, const void*, const char*, int)> SliderScalar{
    "ImGui::SliderScalar"};
dk2ml::Fn<bool(const char*, const ImVec2&, int)> ButtonEx{"ImGui::ButtonEx"};
dk2ml::Fn<void(const char*, ...)> Text{"ImGui::Text"};
dk2ml::Fn<void(const char*, ...)> TextDisabled{"ImGui::TextDisabled"};
dk2ml::Fn<void(int)> SeparatorEx{"ImGui::SeparatorEx"};
dk2ml::Fn<void(float, float)> SameLine{"ImGui::SameLine"};
// the getter overload, the only one left
dk2ml::Fn<bool(const char*, int*, bool (*)(void*, int, const char**), void*, int, int)> Combo{
    "?Combo@ImGui@@YA_NPEBDPEAHP6A_NPEAXHPEAPEBD@Z2HH@Z"};
dk2ml::Fn<bool(const char*, float*, int)> ColorEdit4{"ImGui::ColorEdit4"};
dk2ml::Fn<void()> Spacing{"ImGui::Spacing"};
dk2ml::Fn<void(float)> Indent{"ImGui::Indent"};
dk2ml::Fn<void(float)> Unindent{"ImGui::Unindent"};
dk2ml::Fn<void()> AlignTextToFramePadding{"ImGui::AlignTextToFramePadding"};
dk2ml::Fn<void(const ImVec4&, const char*, ...)> TextColored{"ImGui::TextColored"};
dk2ml::Fn<void(int, float)> PushStyleVarFloat{"?PushStyleVar@ImGui@@YAXHM@Z"};
dk2ml::Fn<void(int, const ImVec2&)> PushStyleVarVec2{"?PushStyleVar@ImGui@@YAXHAEBUImVec2@@@Z"};
dk2ml::Fn<void(int)> PopStyleVar{"ImGui::PopStyleVar"};
dk2ml::Fn<bool(const char*, int, int)> BeginTabBar{"ImGui::BeginTabBar"};
dk2ml::Fn<void()> EndTabBar{"ImGui::EndTabBar"};
dk2ml::Fn<bool(const char*, bool*, int)> BeginTabItem{"ImGui::BeginTabItem"};

dk2ml::Global<void*> GImGui{"GImGui"};
dk2ml::Field<uint8_t> ImGuiContext_IO{"ImGuiContext", "IO"};
dk2ml::Field<bool> ImGuiIO_WantCaptureMouse{"ImGuiIO", "WantCaptureMouse"};
dk2ml::Field<bool> ImGuiIO_WantCaptureKeyboard{"ImGuiIO", "WantCaptureKeyboard"};

dk2ml::Enum ImGuiDataType_Float{"ImGuiDataType_", "ImGuiDataType_Float"};
dk2ml::Enum ImGuiWindowFlags_NoCollapse{"ImGuiWindowFlags_", "ImGuiWindowFlags_NoCollapse"};
dk2ml::Enum ImGuiWindowFlags_AlwaysAutoResize{"ImGuiWindowFlags_", "ImGuiWindowFlags_AlwaysAutoResize"};
dk2ml::Enum ImGuiSeparatorFlags_Horizontal{"ImGuiSeparatorFlags_", "ImGuiSeparatorFlags_Horizontal"};
dk2ml::Enum ImGuiColorEditFlags_NoAlpha{"ImGuiColorEditFlags_", "ImGuiColorEditFlags_NoAlpha"};
dk2ml::Enum ImGuiStyleVar_WindowPadding{"ImGuiStyleVar_", "ImGuiStyleVar_WindowPadding"};
dk2ml::Enum ImGuiStyleVar_WindowRounding{"ImGuiStyleVar_", "ImGuiStyleVar_WindowRounding"};
dk2ml::Enum ImGuiStyleVar_FramePadding{"ImGuiStyleVar_", "ImGuiStyleVar_FramePadding"};
dk2ml::Enum ImGuiStyleVar_FrameRounding{"ImGuiStyleVar_", "ImGuiStyleVar_FrameRounding"};
dk2ml::Enum ImGuiStyleVar_ItemSpacing{"ImGuiStyleVar_", "ImGuiStyleVar_ItemSpacing"};
dk2ml::Enum ImGuiStyleVar_ItemInnerSpacing{"ImGuiStyleVar_", "ImGuiStyleVar_ItemInnerSpacing"};
dk2ml::Enum ImGuiStyleVar_GrabRounding{"ImGuiStyleVar_", "ImGuiStyleVar_GrabRounding"};
dk2ml::Enum ImGuiButtonFlags_AlignTextBaseLine{"ImGuiButtonFlagsPrivate_", "ImGuiButtonFlags_AlignTextBaseLine"};
dk2ml::Enum ImGuiTabBarFlags_IsFocused{"ImGuiTabBarFlagsPrivate_", "ImGuiTabBarFlags_IsFocused"};
dk2ml::Enum ImGuiTabItemFlags_NoPushId{"ImGuiTabItemFlags_", "ImGuiTabItemFlags_NoPushId"};

} // namespace imgui

} // namespace game

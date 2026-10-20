// Game names, resolved by name from the PDB (dk2ml::ResolveAll in init). Optional (missing: no right-click buttons,
// the rest works): GameGUI_OpenWaypointMenu, ActionWP_Client_m_action, sActionWP_ownerId, EVENT_CLICK.
#pragma once

#include <cstdint>

#include "dk2ml.hpp"

namespace game {

extern const DK2ML_API* api;

struct Vector3 {
    float x, y, z; // Y is up
};

struct Matrix3 {
    Vector3 m[3]; // Entity_Common: m_left, m_up, m_forward
};

// --- hooked ---
extern dk2ml::Fn<void(void* human)> Human_Client_Init; // creates the laser, only on night maps
extern dk2ml::Fn<void(void* human)> Human_Client_UpdateLaserLight; // shows it, only on night maps with NVG on
extern dk2ml::Fn<void(void* gui, const void* waypoint, int type)> GameGUI_OpenWaypointMenu;
// picks the day-only / night-only gear models (NVG up or down) from the client's night flag; Init calls it
extern dk2ml::Fn<void(void* human, const void* inventory)> Human_Client_SetInventory;

// --- called ---
extern dk2ml::Fn<void*(const void* input)> GameInput_GetSelectedHuman;
extern dk2ml::Fn<void(void* object)> RenderObject2D_UpdateRenderData; // rebuilds the quad while bNeedsUpdate
extern dk2ml::Fn<void(void* entity, const Vector3* origin, const Matrix3* rotation, float scale)>
    Entity_Common_SetTransform;

// --- globals (dereference: null while the object doesn't exist) ---
extern dk2ml::Global<void*> g_pGameCommon;
extern dk2ml::Global<void*> g_pRenderer;
extern dk2ml::Global<void*> g_pGameGUI;
extern dk2ml::Global<void*> g_pEditor;
extern dk2ml::Global<void*> g_pGameInput;
extern dk2ml::Global<void*> g_pGameClient;
extern dk2ml::Global<uint8_t> Human_Client_typeList; // LinkedList<Human_Client>

// --- fields (uint8_t: an embedded struct, used for its offset) ---
extern dk2ml::Field<bool> GameCommon_m_isNightTime;
extern dk2ml::Field<uint8_t> GameRenderer_m_renderOptions;
extern dk2ml::Field<bool> sRenderOptions_nvg;

extern dk2ml::Field<void*> Human_Client_m_pLaserLight; // Light_Client
extern dk2ml::Field<void*> Human_Client_m_pLaserRay;   // RenderObject2D

extern dk2ml::Field<uint8_t> RenderObject2D_quad;
extern dk2ml::Field<Vector3> RenderObject2D_origin;  // the ray's middle
extern dk2ml::Field<Vector3> RenderObject2D_forward; // normalized
extern dk2ml::Field<uint8_t> RenderObject2D_size;    // Vector2: length, width
extern dk2ml::Field<bool> RenderObject2D_bNeedsUpdate;
extern dk2ml::Field<float> Vector2_x;
extern dk2ml::Field<float> Vector2_y;
extern dk2ml::Field<uint8_t> Render2D_Quad_v; // Vertex3D[4]
extern dk2ml::Field<uint32_t> Vertex3D_color; // 0xAABBGGRR
extern dk2ml::TypeSize Vertex3D_size;

extern dk2ml::Field<uint8_t> Light_Client_m_light;        // sLightParams
extern dk2ml::Field<uint8_t> Light_Client_m_lightCurrent; // sLightParams the renderer reads
extern dk2ml::Field<bool> Light_Client_m_dataChanged;
extern dk2ml::Field<uint8_t> sLightParams_colors; // Vector3[2]
extern dk2ml::Field<float> sLightParams_colorMultiplier;
extern dk2ml::Field<float> sLightParams_radiusMeters;
extern dk2ml::Field<uint8_t> Light_Template_params; // sLightParams
extern dk2ml::Field<void*> Entity_Common_m_pTemplate;
extern dk2ml::Field<uint32_t> Entity_Common_m_id;
extern dk2ml::Field<float> Entity_Common_m_health;
extern dk2ml::Field<Vector3> Entity_Common_m_origin;
extern dk2ml::Field<Vector3> Entity_Common_m_left; // first row of the Matrix3
extern dk2ml::Field<float> Entity_Common_m_scale;
extern dk2ml::Field<uint8_t> ActionWP_Client_m_action; // sActionWP
extern dk2ml::Field<uint32_t> sActionWP_ownerId;

extern dk2ml::Field<uint8_t> GameClient_m_server; // GameClient::Server
extern dk2ml::Field<int> GameClient_Server_clientIndex;
extern dk2ml::Field<uint32_t> GameClient_m_flags;
extern dk2ml::Field<void*> LinkedList_Human_head; // the sentinel
extern dk2ml::Field<void*> LinkedList_Human_next;
extern dk2ml::Field<void*> LinkedList_Human_owner;
extern dk2ml::Field<uint32_t> Human_Client_m_ownerMask; // bit per client
extern dk2ml::Field<bool> Human_Client_m_isControlable;
extern dk2ml::Field<int> Human_Template_type;

extern dk2ml::Field<void*> Human_Client_m_pEquippedItem; // Equipment_Template, shared by every copy of the gun
extern dk2ml::Field<uint8_t> Equipment_Template_name;    // HashedString
extern dk2ml::Field<int> Equipment_Template_type;
extern dk2ml::Field<bool> EqpFirearm_Template_hasLaser;
extern dk2ml::Field<const char*> HashedString_m_pString;

extern dk2ml::Field<void*> GameGUI_m_pChatBox;
extern dk2ml::Field<bool> GUI_Item_m_hidden;

// --- enum values ---
extern dk2ml::Enum CGAMESTATE_RUNNING;
extern dk2ml::Enum CGAMEFLAG_REPLAYING;
extern dk2ml::Enum HUMAN_GOODGUY;
extern dk2ml::Enum EVENT_CLICK;
extern dk2ml::Enum EQUIPMENT_FIREARM;

inline bool MissionRunning()
{
    return api->GetGameState() == CGAMESTATE_RUNNING.Get();
}

// The game takes no hotkeys while the chat box is shown.
inline bool ChatOpen()
{
    void* gui = *g_pGameGUI;
    void* chat = gui ? GameGUI_m_pChatBox(gui) : nullptr;
    return chat && !GUI_Item_m_hidden(chat);
}

inline bool* NvgFlag()
{
    void* renderer = *g_pRenderer;
    return renderer ? &dk2ml::At<bool>(renderer, GameRenderer_m_renderOptions.Offset() + sRenderOptions_nvg.Offset())
                    : nullptr;
}

inline bool* NightFlag()
{
    void* common = *g_pGameCommon;
    return common ? &GameCommon_m_isNightTime(common) : nullptr;
}

// The stock IR view: night map with NVG on. Anything else is the visible lasers' view.
inline bool NvgView()
{
    bool* night = NightFlag();
    bool* nvg = NvgFlag();
    return night && nvg && *night && *nvg;
}

inline bool Editing()
{
    return *g_pEditor != nullptr;
}

// --- ImGui; some functions were inlined away, so the underlying ones are used ---
namespace imgui {

struct ImVec2 {
    float x, y;
};

struct ImVec4 {
    float x, y, z, w;
};

extern dk2ml::Fn<bool(const char* name, bool* open, int flags)> Begin;
extern dk2ml::Fn<void()> End;
extern dk2ml::Fn<bool(const char* label, bool* value)> Checkbox;
extern dk2ml::Fn<bool(const char* label, int dataType, void* value, const void* min, const void* max,
                      const char* format, int flags)>
    SliderScalar;
extern dk2ml::Fn<bool(const char* label, const ImVec2& size, int flags)> ButtonEx;
extern dk2ml::Fn<void(const char* format, ...)> Text;
extern dk2ml::Fn<void(const char* format, ...)> TextDisabled;
extern dk2ml::Fn<void(int flags)> SeparatorEx;
extern dk2ml::Fn<void(float offsetFromStartX, float spacing)> SameLine;
extern dk2ml::Fn<bool(const char* label, int* current, bool (*getter)(void* data, int index, const char** text),
                      void* data, int count, int popupMaxHeightInItems)>
    Combo;
extern dk2ml::Fn<bool(const char* label, float* rgba, int flags)> ColorEdit4;
extern dk2ml::Fn<void()> Spacing;
extern dk2ml::Fn<void(float width)> Indent; // 0: the style's IndentSpacing
extern dk2ml::Fn<void(float width)> Unindent;
extern dk2ml::Fn<void()> AlignTextToFramePadding;
extern dk2ml::Fn<void(const ImVec4& color, const char* format, ...)> TextColored;
// Only style vars can be popped (no PopStyleColor/PopItemWidth/EndDisabled).
extern dk2ml::Fn<void(int var, float value)> PushStyleVarFloat;
extern dk2ml::Fn<void(int var, const ImVec2& value)> PushStyleVarVec2;
extern dk2ml::Fn<void(int count)> PopStyleVar;

// No SetNextWindowPos, GetWindowPos/Size or GetIO: read through the context.
extern dk2ml::Global<void*> GImGui;
extern dk2ml::Field<void*> ImGuiContext_CurrentWindow;
extern dk2ml::Field<ImVec2> ImGuiWindow_Pos; // Begin reads it
extern dk2ml::Field<ImVec2> ImGuiWindow_Size;
extern dk2ml::Field<uint8_t> ImGuiContext_IO; // ImGuiIO
extern dk2ml::Field<bool> ImGuiIO_WantCaptureMouse;
extern dk2ml::Field<bool> ImGuiIO_WantCaptureKeyboard;

extern dk2ml::Enum ImGuiDataType_Float;
extern dk2ml::Enum ImGuiWindowFlags_NoCollapse;
extern dk2ml::Enum ImGuiWindowFlags_AlwaysAutoResize;
extern dk2ml::Enum ImGuiWindowFlags_NoMove;
extern dk2ml::Enum ImGuiWindowFlags_NoSavedSettings;
extern dk2ml::Enum ImGuiSeparatorFlags_Horizontal;
extern dk2ml::Enum ImGuiColorEditFlags_NoAlpha;
extern dk2ml::Enum ImGuiStyleVar_WindowPadding;
extern dk2ml::Enum ImGuiStyleVar_WindowRounding;
extern dk2ml::Enum ImGuiStyleVar_FramePadding;
extern dk2ml::Enum ImGuiStyleVar_FrameRounding;
extern dk2ml::Enum ImGuiStyleVar_ItemSpacing;
extern dk2ml::Enum ImGuiStyleVar_ItemInnerSpacing;
extern dk2ml::Enum ImGuiStyleVar_GrabRounding;
extern dk2ml::Enum ImGuiButtonFlags_AlignTextBaseLine;

} // namespace imgui

} // namespace game

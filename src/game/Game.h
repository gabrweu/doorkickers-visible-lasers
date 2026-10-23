// Game names, resolved from the PDB by dk2ml::ResolveAll. Optional: GameGUI_OpenWaypointMenu, ActionWP_Client_m_action,
// sActionWP_ownerId, EVENT_CLICK (right-click buttons).
#pragma once

#include <cstdint>

#include "dk2ml.hpp"

namespace game {

extern const DK2ML_API* api;

struct Vector3 {
    float x, y, z; // Y is up
};

// SetTransform takes the axes as columns: m[0] = (left.x, up.x, forward.x), ... (FromAxes, Rotation below).
struct Matrix3 {
    Vector3 m[3];
};

// --- hooked ---
extern dk2ml::Fn<void(void* human)> Human_Client_Init; // creates the laser, only on night maps
extern dk2ml::Fn<void(void* human)> Human_Client_UpdateLaserLight; // shows it, only on night maps with NVG on
extern dk2ml::Fn<void(void* gui, const void* waypoint, int type)> GameGUI_OpenWaypointMenu;
// picks day/night gear models from the client's night flag; called by Init
extern dk2ml::Fn<void(void* human, const void* inventory)> Human_Client_SetInventory;
// every Human_Client's delete (vtable slot 0; ~Human_Client is inlined into it)
extern dk2ml::Fn<void*(void* human, unsigned flags)> Human_Client_DeletingDestructor;
// EV_HUMAN_FIREARM_SHOT: one per round, also in replays
extern dk2ml::Fn<void(void* human, int mask, int event, unsigned value, const void* packer)>
    Human_Client_Net_ReceiveEvent;

// --- called ---
extern dk2ml::Fn<void*(const void* input)> GameInput_GetSelectedHuman;
extern dk2ml::Fn<void(void* object)> RenderObject2D_UpdateRenderData; // rebuilds the quad while bNeedsUpdate
extern dk2ml::Fn<void(void* entity, const Vector3* origin, const Matrix3* rotation, float scale)>
    Entity_Common_SetTransform;
// local ids are 8000 and up; doesn't call Init
extern dk2ml::Fn<void*(void* gameClient, const char* templateName, const void* xml, bool local)>
    GameClient_CreateRuntimeEntity;
// static; fovDeg is the full angle. Uses g_pGameCommon's collision world: client frame only.
extern dk2ml::Fn<bool(const Vector3* eye, const Vector3* dir, float fovDeg, const void* target, uint32_t flags,
                      bool corners)>
    GameCommon_HasLineOfSight;
extern dk2ml::Fn<void(void* light)> Light_Client_Init;

// --- globals (dereference: null while the object doesn't exist) ---
extern dk2ml::Global<void*> g_pGameCommon;
extern dk2ml::Global<void*> g_pRenderer;
extern dk2ml::Global<void*> g_pGameGUI;
extern dk2ml::Global<void*> g_pEditor;
extern dk2ml::Global<void*> g_pGameInput;
extern dk2ml::Global<void*> g_pGameClient;
extern dk2ml::Global<uint8_t> Human_Client_typeList; // LinkedList<Human_Client>
// camera position, written every frame by GameClient::UpdateCamera
extern dk2ml::Global<Vector3> RenderObject2D_s_billboardCameraOrigin;

// --- fields (uint8_t: an embedded struct, used for its offset) ---
extern dk2ml::Field<bool> GameCommon_m_isNightTime;
extern dk2ml::Field<int> GameCommon_m_gameTime; // ms, slowed by slow-mo, stopped while paused
extern dk2ml::Field<uint8_t> GameRenderer_m_renderOptions;
extern dk2ml::Field<bool> sRenderOptions_nvg;

extern dk2ml::Field<void*> Human_Client_m_pLaserLight; // Light_Client
extern dk2ml::Field<void*> Human_Client_m_pLaserRay; // RenderObject2D

extern dk2ml::Field<uint8_t> RenderObject2D_quad;
extern dk2ml::Field<Vector3> RenderObject2D_origin; // the ray's middle
extern dk2ml::Field<Vector3> RenderObject2D_forward; // normalized
extern dk2ml::Field<uint8_t> RenderObject2D_size; // Vector2: length, width
extern dk2ml::Field<bool> RenderObject2D_bNeedsUpdate;
extern dk2ml::Field<float> Vector2_x;
extern dk2ml::Field<float> Vector2_y;
extern dk2ml::Field<uint8_t> Render2D_Quad_v; // Vertex3D[4]
extern dk2ml::Field<uint32_t> Vertex3D_color; // 0xAABBGGRR
extern dk2ml::TypeSize Vertex3D_size;

extern dk2ml::Field<uint8_t> Light_Client_m_light; // sLightParams
extern dk2ml::Field<uint8_t> Light_Client_m_lightCurrent; // sLightParams the renderer reads
extern dk2ml::Field<bool> Light_Client_m_dataChanged; // the renderer repacks lights only when one has it set
extern dk2ml::Field<bool> Light_Client_m_isLightDisabled;
extern dk2ml::Field<uint8_t> sLightParams_colors; // Vector3[2]
extern dk2ml::Field<float> sLightParams_colorMultiplier;
extern dk2ml::Field<float> sLightParams_radiusMeters;
extern dk2ml::Field<float> sLightParams_spotAngleDegrees; // half the cone
extern dk2ml::Field<int> sLightParams_limitType; // eLightLimitType
extern dk2ml::Field<uint8_t> Light_Template_params; // sLightParams
extern dk2ml::Field<void*> Entity_Common_m_pTemplate;
extern dk2ml::Field<uint32_t> Entity_Common_m_id;
extern dk2ml::Field<float> Entity_Common_m_health;
extern dk2ml::Field<Vector3> Entity_Common_m_origin;
extern dk2ml::Field<Vector3> Entity_Common_m_left;
extern dk2ml::Field<Vector3> Entity_Common_m_up;
extern dk2ml::Field<Vector3> Entity_Common_m_forward; // a spot light shines along it
extern dk2ml::Field<float> Entity_Common_m_scale;
extern dk2ml::Field<uint32_t> Entity_Common_m_flags; // EF_DELETE_ME: GameClient::UpdateEntitites deletes it
extern dk2ml::Field<uint8_t> GameCommon_m_entityPool; // Entity_Common*[kEntityPoolSize], by id
constexpr uint32_t kEntityPoolSize = 12000;
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
extern dk2ml::Field<Vector3> Human_Client_m_aimDirection;
extern dk2ml::Field<bool> Human_Client_m_isSurrendered;
extern dk2ml::Field<int> Human_Client_m_currentCommand; // eHumanCommand
extern dk2ml::Field<bool> Entity_Client_m_bHiddenInFOW;
// placed every frame for local ids: origin = m_origin, forward = m_left, right = m_forward (unless billboarded)
extern dk2ml::Field<void*> Entity_Client_m_pRenderObject2D;

extern dk2ml::Field<void*> Human_Client_m_pEquippedItem; // Equipment_Template, shared by every copy of the gun
extern dk2ml::Field<uint8_t> Equipment_Template_name; // HashedString
extern dk2ml::Field<int> Equipment_Template_type;
extern dk2ml::Field<bool> EqpFirearm_Template_hasLaser;
extern dk2ml::Field<const char*> HashedString_m_pString;

extern dk2ml::Field<void*> GameGUI_m_pChatBox;
extern dk2ml::Field<bool> GUI_Item_m_hidden;

// --- enum values ---
extern dk2ml::Enum CGAMESTATE_RUNNING;
extern dk2ml::Enum CGAMEFLAG_REPLAYING;
extern dk2ml::Enum HUMAN_GOODGUY;
extern dk2ml::Enum HUMAN_BADGUY;
extern dk2ml::Enum HUMAN_CMD_SHOTGUN_DOOR;
extern dk2ml::Enum EV_HUMAN_FIREARM_SHOT;
extern dk2ml::Enum COL_VISION_BLOCK;
extern dk2ml::Enum EVENT_CLICK;
extern dk2ml::Enum EQUIPMENT_FIREARM;
extern dk2ml::Enum LIGHT_LIMIT_NONE;
extern dk2ml::Enum LIGHT_LIMIT_SHADOWS;
extern dk2ml::Enum EF_DELETE_ME;

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

// Night map with NVG on.
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

inline Matrix3 FromAxes(const Vector3& left, const Vector3& up, const Vector3& forward)
{
    return {{{left.x, up.x, forward.x}, {left.y, up.y, forward.y}, {left.z, up.z, forward.z}}};
}

constexpr int kQuadVertices = 4;

// 0xAABBGGRR. UpdateRenderData doesn't write it.
inline uint32_t& QuadColor(void* object, int vertex)
{
    int vertices = RenderObject2D_quad.Offset() + Render2D_Quad_v.Offset();
    int stride = static_cast<int>(Vertex3D_size.Get());
    return dk2ml::At<uint32_t>(object, vertices + vertex * stride + Vertex3D_color.Offset());
}

inline Matrix3 Rotation(void* entity)
{
    return FromAxes(Entity_Common_m_left(entity), Entity_Common_m_up(entity), Entity_Common_m_forward(entity));
}

// Intrusive list: from the list's next to null or the sentinel. Stops when visit returns false.
template <typename Visit> void EachHuman(Visit visit)
{
    constexpr int kMaxNodes = 4096; // a broken list must not hang the game
    void* list = Human_Client_typeList.Address();
    void* sentinel = LinkedList_Human_head(list);
    void* node = LinkedList_Human_next(list);
    for (int i = 0; node && node != sentinel && i < kMaxNodes; ++i) {
        void* human = LinkedList_Human_owner(node);
        node = LinkedList_Human_next(node);
        if (human && !visit(human)) {
            return;
        }
    }
}

// --- ImGui (some functions are inlined away; their underlying ones are bound) ---
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
// Specialized for its one caller: the id is baked in (one tab bar per window) and BeginTabBarEx takes its flags from
// r8, so they go in the third argument (ImGuiTabBarFlags_IsFocused, as stock adds).
extern dk2ml::Fn<bool(const char* ignoredId, int ignoredFlags, int flags)> BeginTabBar;
extern dk2ml::Fn<void()> EndTabBar; // only after a BeginTabBar that returned true
// open is ignored. No EndTabItem in this build: always pass ImGuiTabItemFlags_NoPushId.
extern dk2ml::Fn<bool(const char* label, bool* open, int flags)> BeginTabItem;

// No GetIO: read through the context.
extern dk2ml::Global<void*> GImGui;
extern dk2ml::Field<uint8_t> ImGuiContext_IO; // ImGuiIO
extern dk2ml::Field<bool> ImGuiIO_WantCaptureMouse;
extern dk2ml::Field<bool> ImGuiIO_WantCaptureKeyboard;

extern dk2ml::Enum ImGuiDataType_Float;
extern dk2ml::Enum ImGuiWindowFlags_NoCollapse;
extern dk2ml::Enum ImGuiWindowFlags_AlwaysAutoResize;
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
extern dk2ml::Enum ImGuiTabBarFlags_IsFocused;
extern dk2ml::Enum ImGuiTabItemFlags_NoPushId;

} // namespace imgui

} // namespace game

/*
 * dk2ml.hpp - optional C++17 layer over dk2ml.h. Header-only; no ABI of its own.
 *
 * Declare the game functions, fields, globals and enum values a plugin uses as namespace-scope objects; one ResolveAll
 * call in DK2ML_PluginInit resolves them and logs every missing name. No game objects exist during init, so use the
 * bindings in hooks and events:
 *
 *   #include "dk2ml.hpp"
 *
 *   dk2ml::Fn<void(void* camera)> UpdateViewMatrix{"Camera::UpdateViewMatrix"};
 *   dk2ml::Global<void*> g_pGameClient{"g_pGameClient"};        // the variable: *g_pGameClient is the GameClient
 *   dk2ml::Field<float> Camera_m_fov{"Camera", "m_fov"};
 *   dk2ml::TypeSize CameraSize{"Camera"};
 *   dk2ml::Enum StateRunning{"GameClient::eCGameState", "CGAMESTATE_RUNNING"};
 *   dk2ml::Fn<void(void*)> NiceToHave{"Some::NewFunction", dk2ml::Optional}; // missing: logged, init still succeeds
 *
 *   int UpdateViewMatrixPre(DK2ML_Regs* regs, void*)
 *   {
 *       void* camera = dk2ml::Arg<void*>(regs, 0); // `this`
 *       float& fov = Camera_m_fov(camera);         // a reference into the game object
 *       if (fov > 90.0f)
 *           fov = 90.0f;
 *       return DK2ML_CALL_ORIGINAL;
 *   }
 *
 *   DK2ML_EXPORT int DK2ML_PluginInit(const DK2ML_API* api, const DK2ML_PluginInfo* info)
 *   {
 *       if (!dk2ml::ResolveAll(api))
 *           return 1;
 *       return dk2ml::Hook(api, UpdateViewMatrix, UpdateViewMatrixPre) ? 0 : 2;
 *   }
 *
 * Call a bound function like any other, e.g. UpdateViewMatrix(camera) on the main thread. In callbacks,
 * Arg<T>(regs, n), Result<T>(regs) and SetResult(regs, v) access arguments and results by type.
 *
 * Bindings register in their constructors: declare them at namespace scope or as static members, never as locals or
 * temporaries.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "dk2ml.h"

namespace dk2ml {

// Optional: a missing name is logged but doesn't fail ResolveAll.
enum Requirement { Required, Optional };

class Binding {
  public:
    Binding(const Binding&) = delete;
    Binding& operator=(const Binding&) = delete;

    bool Resolved() const { return resolved_; }

    bool IsRequired() const { return requirement_ == Required; }

  protected:
    explicit Binding(Requirement requirement) : requirement_(requirement), next_(Head()) { Head() = this; }

    ~Binding() = default;

    // Logs and returns false if the name is missing.
    virtual bool Lookup(const DK2ML_API* api) = 0;

  private:
    static Binding*& Head()
    {
        static Binding* head = nullptr; // function-local: safe from static initialization order
        return head;
    }

    Requirement requirement_;
    bool resolved_ = false;
    Binding* next_;

    friend bool ResolveAll(const DK2ML_API* api);
};

// Resolves every binding and logs each missing name. False if a required one is missing.
inline bool ResolveAll(const DK2ML_API* api)
{
    int missingRequired = 0;
    int missingOptional = 0;
    for (Binding* b = Binding::Head(); b; b = b->next_) {
        b->resolved_ = b->Lookup(api);
        if (!b->resolved_) {
            ++(b->IsRequired() ? missingRequired : missingOptional);
        }
    }

    if (missingRequired || missingOptional) {
        api->Log("%d required and %d optional name(s) not found in this game build (listed above)", missingRequired,
                 missingOptional);
    }

    return missingRequired == 0;
}

// A game function by name, callable: Fn<int(void* self, float x)> f{"Class::Method"}; f(obj, 1.0f).
// The signature isn't checked; `this` is the first argument. A class returned by value: hidden result pointer after
// `this`, returned as well (see "Calling convention" in docs\for-agents\symbols.md).
template <typename Signature> class Fn;

template <typename R, typename... Args> class Fn<R(Args...)> : public Binding {
  public:
    using Pointer = R (*)(Args...);

    explicit Fn(const char* name, Requirement requirement = Required) : Binding(requirement), name_(name) {}

    R operator()(Args... args) const { return pointer_(args...); }

    Pointer Get() const { return pointer_; }

    void* Address() const { return reinterpret_cast<void*>(pointer_); }

    const char* Name() const { return name_; }

    explicit operator bool() const { return pointer_ != nullptr; }

  protected:
    bool Lookup(const DK2ML_API* api) override
    {
        pointer_ = reinterpret_cast<Pointer>(api->ResolveSymbol(name_));
        if (!pointer_) {
            api->Log("missing function %s", name_);
        }
        return pointer_ != nullptr;
    }

  private:
    const char* name_;
    Pointer pointer_ = nullptr;
};

// A C variadic game function: Fn<void(const char* fmt, ...)> f{"ImGui::Text"}; f("%d troopers", n).
// The rest after the typed arguments pass as given (floats as double).
template <typename R, typename... Args> class Fn<R(Args..., ...)> : public Binding {
  public:
    using Pointer = R (*)(Args..., ...);

    explicit Fn(const char* name, Requirement requirement = Required) : Binding(requirement), name_(name) {}

    template <typename... Extra> R operator()(Args... args, Extra... extra) const
    {
        return pointer_(args..., extra...);
    }

    Pointer Get() const { return pointer_; }

    void* Address() const { return reinterpret_cast<void*>(pointer_); }

    const char* Name() const { return name_; }

    explicit operator bool() const { return pointer_ != nullptr; }

  protected:
    bool Lookup(const DK2ML_API* api) override
    {
        pointer_ = reinterpret_cast<Pointer>(api->ResolveSymbol(name_));
        if (!pointer_) {
            api->Log("missing function %s", name_);
        }
        return pointer_ != nullptr;
    }

  private:
    const char* name_;
    Pointer pointer_ = nullptr;
};

// A game global by name; holds the variable's address. `GameClient* g_pGameClient` -> Global<void*>, and
// *g_pGameClient is the GameClient (or null).
template <typename T> class Global : public Binding {
  public:
    explicit Global(const char* name, Requirement requirement = Required) : Binding(requirement), name_(name) {}

    T& operator*() const { return *address_; }

    T* Address() const { return address_; }

    explicit operator bool() const { return address_ != nullptr; }

  protected:
    bool Lookup(const DK2ML_API* api) override
    {
        address_ = static_cast<T*>(api->ResolveSymbol(name_));
        if (!address_) {
            api->Log("missing global %s", name_);
        }
        return address_ != nullptr;
    }

  private:
    const char* name_;
    T* address_ = nullptr;
};

// A data member by type and field name: Field<float> fov{"Camera", "m_fov"}; fov(camera) = 60.0f.
// T must match the field's size and layout; only the offset comes from the PDB.
template <typename T> class Field : public Binding {
  public:
    Field(const char* type, const char* field, Requirement requirement = Required)
        : Binding(requirement), type_(type), field_(field)
    {}

    T& operator()(void* object) const { return *reinterpret_cast<T*>(static_cast<char*>(object) + offset_); }

    const T& operator()(const void* object) const
    {
        return *reinterpret_cast<const T*>(static_cast<const char*>(object) + offset_);
    }

    int32_t Offset() const { return offset_; }

  protected:
    bool Lookup(const DK2ML_API* api) override
    {
        offset_ = api->GetFieldOffset(type_, field_);
        if (offset_ < 0) {
            api->Log("missing field %s::%s", type_, field_);
        }
        return offset_ >= 0;
    }

  private:
    const char* type_;
    const char* field_;
    int32_t offset_ = -1;
};

// sizeof a game type: TypeSize cameraSize{"Camera"}; cameraSize.Get().
class TypeSize : public Binding {
  public:
    explicit TypeSize(const char* type, Requirement requirement = Required) : Binding(requirement), type_(type) {}

    uint32_t Get() const { return size_; }

  protected:
    bool Lookup(const DK2ML_API* api) override
    {
        size_ = api->GetTypeSize(type_);
        if (!size_) {
            api->Log("missing type %s", type_);
        }
        return size_ != 0;
    }

  private:
    const char* type_;
    uint32_t size_ = 0;
};

// An enumerator by its C++ name (not the XML name): Enum addChild{"GUI::eAction", "ACTION_ADD_CHILD"}; addChild.Get().
// Never hardcode the values: they change when the game adds enumerators.
class Enum : public Binding {
  public:
    Enum(const char* type, const char* name, Requirement requirement = Required)
        : Binding(requirement), type_(type), name_(name)
    {}

    int64_t Get() const { return value_; }

  protected:
    bool Lookup(const DK2ML_API* api) override
    {
        if (api->GetEnumValue(type_, name_, &value_) != DK2ML_OK) {
            api->Log("missing enum %s::%s", type_, name_);
            return false;
        }
        return true;
    }

  private:
    const char* type_;
    const char* name_;
    int64_t value_ = 0;
};

// --- events, interfaces, tasks ---

// Subscribes to a loader event (see "events" in dk2ml.h); init only. Captureless lambdas work:
//   dk2ml::On(api, DK2ML_EVENT_FRAME, [](const DK2ML_Event* e, void*) { ... });
inline bool On(const DK2ML_API* api, DK2ML_EventType type, DK2ML_EventFn fn, void* user = nullptr)
{
    return api->Subscribe(type, fn, user) == DK2ML_OK;
}

// Publishes a function table for other plugins (see "interfaces" in dk2ml.h); init only. First field: structSize.
template <typename Table> bool Publish(const DK2ML_API* api, const char* name, uint32_t version, const Table* table)
{
    return api->PublishInterface(name, version, table) == DK2ML_OK;
}

// Another plugin's table, from PLUGINS_LOADED on. NULL during init, or if missing, older than minVersion, or its
// publisher was switched off.
template <typename Table>
const Table* Get(const DK2ML_API* api, const char* name, uint32_t minVersion = 0, uint32_t* version = nullptr)
{
    return static_cast<const Table*>(api->GetInterface(name, minVersion, version));
}

// Runs fn(user) on the main thread at the next frame start (see "tasks" in dk2ml.h). Any thread; captureless lambdas.
inline bool Post(const DK2ML_API* api, DK2ML_TaskFn fn, void* user = nullptr)
{
    return api->AddTask(fn, user) == DK2ML_OK;
}

// --- GUI kit, input capture, GUI events ---

namespace gui {

// First item named name under `under`, recursive; NULL = the whole GUI.
inline void* Find(const DK2ML_API* api, const char* name, void* under = nullptr)
{
    return api->GuiFind(under, name);
}

inline void* Parent(const DK2ML_API* api, void* item)
{
    return api->GuiParent(item);
}

inline const char* Name(const DK2ML_API* api, void* item)
{
    return api->GuiName(item);
}

// Up to max children into out, in drawing order (last on top). Returns the child count.
inline int Children(const DK2ML_API* api, void* item, void** out, int max)
{
    return api->GuiChildren(item, out, max);
}

inline bool IsShown(const DK2ML_API* api, void* item)
{
    return api->GuiIsShown(item) != 0;
}

inline void Show(const DK2ML_API* api, void* item, bool show = true)
{
    api->GuiShow(item, show ? 1 : 0);
}

inline bool SetText(const DK2ML_API* api, void* item, const char* utf8)
{
    return api->GuiSetText(item, utf8) == DK2ML_OK;
}

inline bool AddChild(const DK2ML_API* api, void* parent, void* item)
{
    return api->GuiAddChild(parent, item) == DK2ML_OK;
}

inline bool SetOrigin(const DK2ML_API* api, void* item, float x, float y)
{
    return api->GuiSetOrigin(item, x, y) == DK2ML_OK;
}

inline bool Click(const DK2ML_API* api, void* item)
{
    return api->GuiClick(item) == DK2ML_OK;
}

// Binds item's <Action type="Callback"> actions for itemEvent (GUI::Item::eItemEventType) to fn. Captureless lambdas:
//   dk2ml::Enum EVENT_CLICK{"GUI::Item::eItemEventType", "EVENT_CLICK"}; // at namespace scope
//   dk2ml::gui::OnAction(api, button, EVENT_CLICK.Get(), [](void* item, float, float, void*) { ... });
inline bool OnAction(const DK2ML_API* api, void* item, int64_t itemEvent, DK2ML_GuiCallbackFn fn, void* user = nullptr)
{
    return api->GuiSetCallback(item, static_cast<int>(itemEvent), fn, user) == DK2ML_OK;
}

// A button in the Esc menus (see "Esc-menu buttons" in dk2ml.h); init only. False (logged) on an API 1 loader:
//   DK2ML_MenuButton b = {sizeof(b)};
//   b.icon = "data/textures/gui/my_mod_normal.dds";
//   b.iconHover = "data/textures/gui/my_mod_hover.dds";
//   b.tooltip = "My Mod settings";
//   b.onClick = [](void*, float, float, void*) { g_windowOpen = !g_windowOpen; };
//   dk2ml::gui::AddMenuButton(api, b);
inline bool AddMenuButton(const DK2ML_API* api, const DK2ML_MenuButton& button)
{
    if (api->structSize <= offsetof(DK2ML_API, AddMenuButton)) {
        api->Log("this loader has no Esc-menu buttons (plugin API %u, they need 2)", api->apiVersion);
        return false;
    }
    return api->AddMenuButton(&button) == DK2ML_OK;
}

} // namespace gui

// While captured, the game acts as if one of its menus is open (no clicks or keys on the map).
inline bool CaptureInput(const DK2ML_API* api, bool capture)
{
    return api->CaptureGameInput(capture ? 1 : 0) == DK2ML_OK;
}

// One of the game's own menus is open, ignoring captures.
inline bool GameMenuOpen(const DK2ML_API* api)
{
    return api->IsGameMenuOpen() != 0;
}

// Subscribes to a game GUI event (GUI::Events::eEventType); init only. False for an id <= 0 (e.g. an unresolved Enum).
inline bool OnGuiEvent(const DK2ML_API* api, int64_t guiEventId, DK2ML_EventFn fn, void* user = nullptr)
{
    return guiEventId > 0 && api->SubscribeGuiEvent(static_cast<uint32_t>(guiEventId), fn, user) == DK2ML_OK;
}

// --- hooks ---

// Creates and enables a safe hook. False if target is null (not logged) or CreateSafeHook/EnableHook fails (logged).
inline bool Hook(const DK2ML_API* api, void* target, DK2ML_PreFn pre, DK2ML_PostFn post = nullptr, void* user = nullptr)
{
    return target && api->CreateSafeHook(target, pre, post, user) == DK2ML_OK && api->EnableHook(target) == DK2ML_OK;
}

template <typename Signature>
bool Hook(const DK2ML_API* api, const Fn<Signature>& function, DK2ML_PreFn pre, DK2ML_PostFn post = nullptr,
          void* user = nullptr)
{
    return Hook(api, function.Address(), pre, post, user);
}

namespace detail {

// T from a 64-bit slot's low bytes; the upper bytes of smaller values are undefined
template <typename T> T FromBits(uint64_t bits)
{
    static_assert(sizeof(T) <= sizeof(uint64_t) && std::is_trivially_copyable<T>::value,
                  "only scalars, pointers and small trivially copyable types travel in one register");

    T value;
    std::memcpy(&value, &bits, sizeof(T));
    return value;
}

template <typename T> uint64_t ToBits(T value, uint64_t keep)
{
    static_assert(sizeof(T) <= sizeof(uint64_t) && std::is_trivially_copyable<T>::value,
                  "only scalars, pointers and small trivially copyable types travel in one register");

    std::memcpy(&keep, &value, sizeof(T)); // low bytes only; the rest is kept
    return keep;
}

template <typename T> constexpr bool IsFloating = std::is_floating_point<T>::value;

} // namespace detail

// Argument n (0 = first; `this` is 0) in a pre callback, as a T. Floats/doubles 0-3: xmm0-3; else rcx/rdx/r8/r9,
// then the stack.
template <typename T> T Arg(const DK2ML_Regs* regs, int n)
{
    if constexpr (detail::IsFloating<T>) {
        return detail::FromBits<T>(n < 4 ? regs->xmm[n].lo : regs->stack[n + 1]);
    } else {
        return detail::FromBits<T>(DK2ML_Arg(regs, n));
    }
}

// Sets argument n for the original (pre).
template <typename T> void SetArg(DK2ML_Regs* regs, int n, T value)
{
    if constexpr (detail::IsFloating<T>) {
        if (n < 4) {
            regs->xmm[n].lo = detail::ToBits(value, regs->xmm[n].lo);
        } else {
            regs->stack[n + 1] = detail::ToBits(value, 0);
        }
    } else {
        DK2ML_SetArg(regs, n, detail::ToBits(value, DK2ML_Arg(regs, n)));
    }
}

// The result as a T, in post. Floats/doubles: xmm0; else rax.
template <typename T> T Result(const DK2ML_Regs* regs)
{
    return detail::FromBits<T>(detail::IsFloating<T> ? regs->xmm[0].lo : regs->rax);
}

// Sets the result: in post, or in a pre that returns DK2ML_SKIP_ORIGINAL.
template <typename T> void SetResult(DK2ML_Regs* regs, T value)
{
    if constexpr (detail::IsFloating<T>) {
        regs->xmm[0].lo = detail::ToBits(value, regs->xmm[0].lo);
    } else {
        regs->rax = detail::ToBits(value, 0);
    }
}

// A member at a byte offset: At<int>(object, offset).
template <typename T> T& At(void* base, int32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename T> const T& At(const void* base, int32_t offset)
{
    return *reinterpret_cast<const T*>(static_cast<const char*>(base) + offset);
}

} // namespace dk2ml

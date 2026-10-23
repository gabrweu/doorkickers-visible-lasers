// Per-gun laser/light positions and laser/light overrides, by template name. Outside Settings, so Reset keeps them.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <vector>

#include "Lasers.h"
#include "game/Game.h"

namespace {

constexpr const wchar_t* kSection = L"gun_offsets";
constexpr const wchar_t* kLightOffsetSection = L"gun_light_offsets";
constexpr const wchar_t* kLaserSection = L"gun_lasers";
constexpr const wchar_t* kLightSection = L"gun_lights";
constexpr DWORD kSectionChars = 32768; // the ini API's limit

GunTable g_guns;
GunTable g_lightOffsets;
GunLaserTable g_lasers;
GunLaserTable g_lights;

// Usable as an ini key (the ini trims spaces).
bool Storable(std::string_view name)
{
    return !name.empty() && name.find('=') == std::string_view::npos && name.front() != '[' && name.front() != ';' &&
           name.front() != ' ' && name.back() != ' ';
}

std::wstring Wide(std::string_view text)
{
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
    return wide;
}

GunOffset Clamped(GunOffset offset)
{
    return {std::clamp(offset.heightCm, kHeightRange.min, kHeightRange.max),
            std::clamp(offset.backCm, kBackRange.min, kBackRange.max),
            std::clamp(offset.sideCm, kSideRange.min, kSideRange.max)};
}

const char* ItemName(void* item)
{
    const char* name =
        dk2ml::At<const char*>(item, game::Equipment_Template_name.Offset() + game::HashedString_m_pString.Offset());
    return name && name[0] ? name : nullptr;
}

bool IsFirearm(void* item)
{
    return item && game::Equipment_Template_type(item) == game::EQUIPMENT_FIREARM.Get();
}

void SaveSwitches(const std::wstring& file, const wchar_t* sectionName, const GunLaserTable& table)
{
    if (table.empty()) {
        WritePrivateProfileStringW(sectionName, nullptr, nullptr, file.c_str());
        return;
    }

    std::wstring section;
    for (const auto& [name, on] : table) {
        section += Wide(name) + (on ? L"=on" : L"=off");
        section += L'\0';
    }
    section += L'\0';
    WritePrivateProfileSectionW(sectionName, section.c_str(), file.c_str());
}

// <name>=on|off
void LoadSwitches(const std::wstring& file, const wchar_t* sectionName, GunLaserTable& table)
{
    table.clear();
    std::vector<wchar_t> section(kSectionChars);
    GetPrivateProfileSectionW(sectionName, section.data(), kSectionChars, file.c_str());
    for (const wchar_t* line = section.data(); *line; line += wcslen(line) + 1) {
        const wchar_t* equals = wcschr(line, L'=');
        if (!equals) {
            continue;
        }

        const wchar_t* value = equals + 1;
        std::string name = Utf8(std::wstring_view(line, equals - line));
        if ((_wcsicmp(value, L"on") == 0 || _wcsicmp(value, L"off") == 0) && Storable(name)) {
            table.insert_or_assign(name, _wcsicmp(value, L"on") == 0);
        }
    }
}

const bool* Override(const GunLaserTable& table, void* item)
{
    if (!IsFirearm(item)) {
        return nullptr;
    }
    const char* name = ItemName(item);
    auto found = name ? table.find(std::string_view(name)) : table.end();
    return found == table.end() ? nullptr : &found->second;
}

void Set(GunLaserTable& table, std::string_view name, bool on)
{
    if (Storable(name)) {
        table.insert_or_assign(std::string(name), on);
    }
}

template <typename Table> void Remove(Table& table, std::string_view name)
{
    auto found = table.find(name);
    if (found != table.end()) {
        table.erase(found);
    }
}

GunOffset* FindOffset(GunTable& table, std::string_view name)
{
    auto found = table.find(name);
    return found == table.end() ? nullptr : &found->second;
}

void SetOffset(GunTable& table, std::string_view name, GunOffset offset)
{
    if (Storable(name)) {
        table.insert_or_assign(std::string(name), Clamped(offset));
    }
}

// <name>=<height>,<back>[,<side>]
void LoadOffsets(const std::wstring& file, const wchar_t* sectionName, GunTable& table)
{
    table.clear();
    std::vector<wchar_t> section(kSectionChars);
    GetPrivateProfileSectionW(sectionName, section.data(), kSectionChars, file.c_str());
    for (const wchar_t* line = section.data(); *line; line += wcslen(line) + 1) {
        const wchar_t* equals = wcschr(line, L'=');
        if (!equals) {
            continue;
        }

        wchar_t* end = nullptr;
        float height = std::wcstof(equals + 1, &end);
        if (end == equals + 1 || *end != L',') {
            continue;
        }
        const wchar_t* backText = end + 1;
        float back = std::wcstof(backText, &end);
        if (end == backText) {
            continue;
        }
        float side = 0.0f;
        if (*end == L',') {
            const wchar_t* sideText = end + 1;
            side = std::wcstof(sideText, &end);
            if (end == sideText) {
                side = 0.0f;
            }
        }
        SetOffset(table, Utf8(std::wstring_view(line, equals - line)), {height, back, side});
    }
}

void SaveOffsets(const std::wstring& file, const wchar_t* sectionName, const GunTable& table)
{
    if (table.empty()) {
        WritePrivateProfileStringW(sectionName, nullptr, nullptr, file.c_str()); // deletes the section
        return;
    }

    std::wstring section;
    for (const auto& [name, offset] : table) {
        wchar_t value[64];
        std::swprintf(value, 64, L"%.2f,%.2f,%.2f", offset.heightCm, offset.backCm, offset.sideCm);
        section += Wide(name) + L"=" + value;
        section += L'\0';
    }
    section += L'\0';
    WritePrivateProfileSectionW(sectionName, section.c_str(), file.c_str());
}

} // namespace

const char* Guns_Name(void* human)
{
    void* item = game::Human_Client_m_pEquippedItem(human);
    return item ? ItemName(item) : nullptr;
}

bool Guns_IsFirearm(void* human)
{
    return IsFirearm(game::Human_Client_m_pEquippedItem(human));
}

bool Guns_StockLaser(void* human)
{
    void* item = game::Human_Client_m_pEquippedItem(human);
    return IsFirearm(item) && game::EqpFirearm_Template_hasLaser(item);
}

bool Guns_HasLaser(void* human)
{
    const bool* own = Override(g_lasers, game::Human_Client_m_pEquippedItem(human));
    return own ? *own : Guns_StockLaser(human);
}

void Guns_SetLaser(std::string_view name, bool on)
{
    Set(g_lasers, name, on);
}

void Guns_RemoveLaser(std::string_view name)
{
    Remove(g_lasers, name);
}

const GunLaserTable& Guns_Lasers()
{
    return g_lasers;
}

bool Guns_HasLight(void* human)
{
    const bool* own = Override(g_lights, game::Human_Client_m_pEquippedItem(human));
    return own ? *own : Guns_HasLaser(human);
}

void Guns_SetLight(std::string_view name, bool on)
{
    Set(g_lights, name, on);
}

void Guns_RemoveLight(std::string_view name)
{
    Remove(g_lights, name);
}

const GunLaserTable& Guns_Lights()
{
    return g_lights;
}

GunOffset Guns_Offset(void* human)
{
    const char* name = Guns_Name(human);
    const GunOffset* own = name ? Guns_Find(name) : nullptr;
    return own ? *own : GunOffset{g_settings.heightCm, g_settings.backCm, g_settings.sideCm};
}

GunOffset* Guns_Find(std::string_view name)
{
    return FindOffset(g_guns, name);
}

void Guns_Set(std::string_view name, GunOffset offset)
{
    SetOffset(g_guns, name, offset);
}

void Guns_Remove(std::string_view name)
{
    Remove(g_guns, name);
}

const GunTable& Guns_All()
{
    return g_guns;
}

GunOffset Guns_LightOffset(void* human)
{
    const char* name = Guns_Name(human);
    const GunOffset* own = name ? Guns_FindLightOffset(name) : nullptr;
    return own ? *own : GunOffset{g_settings.lightHeightCm, g_settings.lightBackCm, g_settings.lightSideCm};
}

GunOffset* Guns_FindLightOffset(std::string_view name)
{
    return FindOffset(g_lightOffsets, name);
}

void Guns_SetLightOffset(std::string_view name, GunOffset offset)
{
    SetOffset(g_lightOffsets, name, offset);
}

void Guns_RemoveLightOffset(std::string_view name)
{
    Remove(g_lightOffsets, name);
}

const GunTable& Guns_LightOffsets()
{
    return g_lightOffsets;
}

void Guns_Load(const std::wstring& file)
{
    LoadSwitches(file, kLaserSection, g_lasers);
    LoadSwitches(file, kLightSection, g_lights);
    LoadOffsets(file, kSection, g_guns);
    LoadOffsets(file, kLightOffsetSection, g_lightOffsets);
}

void Guns_Save(const std::wstring& file)
{
    SaveSwitches(file, kLaserSection, g_lasers);
    SaveSwitches(file, kLightSection, g_lights);
    SaveOffsets(file, kSection, g_guns);
    SaveOffsets(file, kLightOffsetSection, g_lightOffsets);
}

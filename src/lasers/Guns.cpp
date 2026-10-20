// Laser position per gun, by template name (the game has none), and per-gun hasLaser overrides. Outside Settings, so
// "Reset all settings" keeps them.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <vector>

#include "Lasers.h"
#include "game/Game.h"

namespace {

constexpr const wchar_t* kSection = L"gun_offsets";
constexpr const wchar_t* kLaserSection = L"gun_lasers";
constexpr DWORD kSectionChars = 32768; // the ini API's limit

GunTable g_guns;
GunLaserTable g_lasers;

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

void SaveLasers(const std::wstring& file)
{
    if (g_lasers.empty()) {
        WritePrivateProfileStringW(kLaserSection, nullptr, nullptr, file.c_str());
        return;
    }

    std::wstring section;
    for (const auto& [name, on] : g_lasers) {
        section += Wide(name) + (on ? L"=on" : L"=off");
        section += L'\0';
    }
    section += L'\0';
    WritePrivateProfileSectionW(kLaserSection, section.c_str(), file.c_str());
}

// <name>=on|off
void LoadLasers(const std::wstring& file)
{
    g_lasers.clear();
    std::vector<wchar_t> section(kSectionChars);
    GetPrivateProfileSectionW(kLaserSection, section.data(), kSectionChars, file.c_str());
    for (const wchar_t* line = section.data(); *line; line += wcslen(line) + 1) {
        const wchar_t* equals = wcschr(line, L'=');
        if (!equals) {
            continue;
        }

        const wchar_t* value = equals + 1;
        if (_wcsicmp(value, L"on") == 0 || _wcsicmp(value, L"off") == 0) {
            Guns_SetLaser(Utf8(std::wstring_view(line, equals - line)), _wcsicmp(value, L"on") == 0);
        }
    }
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
    void* item = game::Human_Client_m_pEquippedItem(human);
    const bool* own = Guns_LaserOverride(item);
    return own ? *own : Guns_StockLaser(human);
}

const bool* Guns_LaserOverride(void* item)
{
    if (!IsFirearm(item)) {
        return nullptr;
    }
    const char* name = ItemName(item);
    auto found = name ? g_lasers.find(std::string_view(name)) : g_lasers.end();
    return found == g_lasers.end() ? nullptr : &found->second;
}

void Guns_SetLaser(std::string_view name, bool on)
{
    if (Storable(name)) {
        g_lasers.insert_or_assign(std::string(name), on);
    }
}

void Guns_RemoveLaser(std::string_view name)
{
    auto found = g_lasers.find(name);
    if (found != g_lasers.end()) {
        g_lasers.erase(found);
    }
}

const GunLaserTable& Guns_Lasers()
{
    return g_lasers;
}

GunOffset Guns_Offset(void* human)
{
    const char* name = Guns_Name(human);
    const GunOffset* own = name ? Guns_Find(name) : nullptr;
    return own ? *own : GunOffset{g_settings.heightCm, g_settings.backCm, g_settings.sideCm};
}

GunOffset* Guns_Find(std::string_view name)
{
    auto found = g_guns.find(name);
    return found == g_guns.end() ? nullptr : &found->second;
}

void Guns_Set(std::string_view name, GunOffset offset)
{
    if (Storable(name)) {
        g_guns.insert_or_assign(std::string(name), Clamped(offset));
    }
}

void Guns_Remove(std::string_view name)
{
    auto found = g_guns.find(name);
    if (found != g_guns.end()) {
        g_guns.erase(found);
    }
}

const GunTable& Guns_All()
{
    return g_guns;
}

// <name>=<height>,<back>[,<side>]
void Guns_Load(const std::wstring& file)
{
    LoadLasers(file);
    g_guns.clear();
    std::vector<wchar_t> section(kSectionChars);
    GetPrivateProfileSectionW(kSection, section.data(), kSectionChars, file.c_str());
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
        Guns_Set(Utf8(std::wstring_view(line, equals - line)), {height, back, side});
    }
}

void Guns_Save(const std::wstring& file)
{
    SaveLasers(file);
    if (g_guns.empty()) {
        WritePrivateProfileStringW(kSection, nullptr, nullptr, file.c_str()); // deletes the section
        return;
    }

    std::wstring section;
    for (const auto& [name, offset] : g_guns) {
        wchar_t value[64];
        std::swprintf(value, 64, L"%.2f,%.2f,%.2f", offset.heightCm, offset.backCm, offset.sideCm);
        section += Wide(name) + L"=" + value;
        section += L'\0';
    }
    section += L'\0';
    WritePrivateProfileSectionW(kSection, section.c_str(), file.c_str());
}

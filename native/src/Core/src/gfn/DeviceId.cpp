// OpenNOW native client — stable device identity.

#include "opennow/core/gfn/DeviceId.hpp"

#include <mutex>

#include "opennow/core/Hash.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace opennow::core::gfn {
namespace {

std::mutex g_mutex;
std::string g_override;
std::string g_cachedSource;
std::string g_cachedId;

std::string HostName() {
#ifdef _WIN32
    DWORD size = 0;
    ::GetComputerNameExW(ComputerNameDnsHostname, nullptr, &size);
    std::wstring buffer(static_cast<std::size_t>(size), L'\0');
    if (size > 0 && ::GetComputerNameExW(ComputerNameDnsHostname, buffer.data(), &size)) {
        buffer.resize(static_cast<std::size_t>(size));
        const int needed = WideCharToMultiByte(CP_UTF8, 0, buffer.data(),
                                               static_cast<int>(buffer.size()), nullptr, 0,
                                               nullptr, nullptr);
        std::string out(static_cast<std::size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()),
                            out.data(), needed, nullptr, nullptr);
        return out;
    }
    return "opennow-host";
#else
    char buffer[256] = {0};
    if (::gethostname(buffer, sizeof(buffer)) == 0) return std::string(buffer);
    return "opennow-host";
#endif
}

std::string UserName() {
#ifdef _WIN32
    DWORD size = 0;
    ::GetUserNameExW(NameSamCompatible, nullptr, &size);
    std::wstring buffer(static_cast<std::size_t>(size), L'\0');
    if (size > 0 && ::GetUserNameExW(NameSamCompatible, buffer.data(), &size)) {
        buffer.resize(static_cast<std::size_t>(size));
        const int needed = WideCharToMultiByte(CP_UTF8, 0, buffer.data(),
                                               static_cast<int>(buffer.size()), nullptr, 0,
                                               nullptr, nullptr);
        std::string out(static_cast<std::size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()),
                            out.data(), needed, nullptr, nullptr);
        return out;
    }
    return "opennow-user";
#else
    const char* user = std::getenv("USER");
    return user != nullptr ? std::string(user) : "opennow-user";
#endif
}

#ifdef _WIN32
std::string MachineGuid() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ,
                        &key) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buffer[128] = {0};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LONG status = ::RegQueryValueExW(key, L"MachineGuid", nullptr, &type,
                                           reinterpret_cast<LPBYTE>(buffer), &size);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return {};
    std::wstring wide(buffer);
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed,
                        nullptr, nullptr);
    return out;
}
#endif

}  // namespace

std::string DeviceId::ComputeSource() {
#ifdef _WIN32
    const std::string machineGuid = MachineGuid();
    if (!machineGuid.empty()) return machineGuid;
#endif
    return HostName() + "/" + UserName();
}

std::string DeviceId::Source() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_override.empty()) return g_override;
    if (g_cachedSource.empty()) g_cachedSource = ComputeSource();
    return g_cachedSource;
}

std::string DeviceId::Get() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_override.empty()) return Sha256(g_override).Hex().substr(0, 32);
    if (g_cachedId.empty()) {
        g_cachedSource = ComputeSource();
        g_cachedId = Sha256(g_cachedSource).Hex().substr(0, 32);
    }
    return g_cachedId;
}

void DeviceId::SetOverride(std::string value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_override = std::move(value);
    g_cachedId.clear();
    g_cachedSource.clear();
}

void DeviceId::ClearOverride() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_override.clear();
    g_cachedId.clear();
    g_cachedSource.clear();
}

}  // namespace opennow::core::gfn

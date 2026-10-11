// OpenNOW native client — stable device identity.
//
// GFN ties sessions and, in some regions, entitlement checks to a device hash.
// It must be stable across launches but not identical across machines, so we
// hash the Windows machine GUID and fall back to host+user when it is missing
// (exactly the derivation the web server uses).

#pragma once

#include <string>

namespace opennow::core::gfn {

class DeviceId {
public:
    // Cached after the first call; the value never changes for a process.
    static std::string Get();

    // Raw input before hashing — exposed for diagnostics in Settings → About.
    static std::string Source();

    // Test seam.
    static void SetOverride(std::string value);
    static void ClearOverride();

private:
    static std::string ComputeSource();
};

}  // namespace opennow::core::gfn

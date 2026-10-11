// OpenNOW native client — precompiled header.

#pragma once

// The Windows headers have to come first: the XAML compiler's XamlUserType
// implements the classic COM IXamlUserType, and C++/WinRT only supports that
// when unknwn.h is already in scope.
#include <windows.h>
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>

// Windows App SDK (WinUI 3)
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Xaml.Interop.h>  // xaml_typename

// C++/WinRT helpers
#include <wil/cppwinrt.h>

// OpenNOW Core
#include <opennow/core/Json.hpp>
#include <opennow/core/Logging.hpp>
#include <opennow/core/Paths.hpp>
#include <opennow/core/Result.hpp>
#include <opennow/core/Settings.hpp>
#include <opennow/core/Types.hpp>
#include <opennow/core/gfn/Auth.hpp>
#include <opennow/core/gfn/Catalog.hpp>
#include <opennow/core/gfn/CloudMatch.hpp>
#include <opennow/core/gfn/DeviceId.hpp>
#include <opennow/core/gfn/Signaling.hpp>
#include <opennow/core/gfn/Subscription.hpp>

#include <cctype>
#include <cwctype>

namespace winrt {

// C++/WinRT only ships resume_foreground for the system dispatcher queue; the
// WinUI 3 one is a different type, so provide the equivalent awaiter.
inline auto resume_foreground(Microsoft::UI::Dispatching::DispatcherQueue const& queue) {
    struct awaitable {
        Microsoft::UI::Dispatching::DispatcherQueue queue;
        bool await_ready() const noexcept { return false; }
        void await_resume() const noexcept {}
        void await_suspend(std::experimental::coroutine_handle<> handle) const {
            queue.TryEnqueue([handle] { handle(); });
        }
    };
    return awaitable{queue};
}

}  // namespace winrt

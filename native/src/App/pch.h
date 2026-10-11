// OpenNOW native client — precompiled header.

#pragma once

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
#include <windows.h>
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>

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

// OpenNOW native client — entry point.
//
// The app is unpackaged and self-contained: the Windows App SDK travels next to
// this executable, so there is no package identity to resolve it through and no
// bootstrapper call to make — the DLLs load like any other app-local binary.
//
// Everything here is also the app's first diagnostic surface. A GUI app that
// fails before its first window shows nothing at all, so every failure path
// writes %LOCALAPPDATA%\OpenNOW\startup.log and puts a message on screen.

#include "pch.h"
#include "StartupLog.h"

#include "App.h"

#include <cstring>
#include <chrono>
#include <fstream>
#include <string>

namespace {

std::wstring StartupLogPath() {
    wchar_t localAppData[MAX_PATH]{};
    std::wstring directory = L"OpenNOW";
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) != 0) {
        directory = std::wstring(localAppData) + L"\\OpenNOW";
    }
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory + L"\\startup.log";
}

std::wstring Timestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);

    wchar_t stamp[64]{};
    wcsftime(stamp, std::size(stamp), L"%Y-%m-%d %H:%M:%S", &local);
    return stamp;
}

void Append(std::wstring const& message) {
    std::wofstream log(StartupLogPath(), std::ios::app);
    if (!log) {
        return;
    }
    log << Timestamp() << L"  " << message << L"\n";
}

void Report(std::wstring const& title, std::wstring const& detail) {
    Append(title + L": " + detail);
    MessageBoxW(nullptr, detail.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

}  // namespace

// Declared in StartupLog.h: the app and the main window trace their own
// construction here, before any logger or window exists.
void WriteStartupLog(std::wstring const& message) {
    Append(message);
}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        Append(L"entry point reached");

        // WinUI needs a COM apartment on this thread before any XAML object can
        // be created, and the XAML thread must be a single-threaded one.
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        Append(L"apartment initialised");

        Append(L"calling Application::Start");

        // Qualified: at global scope "Microsoft" is also the classic COM WRL
        // namespace that windows.h declares.
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
            Append(L"start callback entered");
            try {
                winrt::make<winrt::OpenNOW::implementation::App>();
                Append(L"application instance created");
            }
            catch (winrt::hresult_error const& error) {
                Append(L"creating the application failed: " + std::wstring(error.message().c_str()));
                throw;
            }
        });

        Append(L"application exited cleanly");
        winrt::uninit_apartment();
        return 0;
    }
    catch (winrt::hresult_error const& error) {
        wchar_t code[16]{};
        swprintf_s(code, L"0x%08X", static_cast<unsigned>(error.code()));
        Report(L"OpenNOW failed to start",
               L"Windows reported an error while starting OpenNOW.\n\n" +
                   std::wstring(error.message().c_str()) + L"\n\nError code: " + code +
                   L"\n\nDetails are in %LOCALAPPDATA%\\OpenNOW\\startup.log.");
        return static_cast<int>(error.code());
    }
    catch (std::exception const& error) {
        const auto* const text = error.what();
        std::wstring wide(text ? text : "", text ? std::strlen(text) : 0);
        Report(L"OpenNOW failed to start",
               L"OpenNOW could not start:\n\n" + wide +
                   L"\n\nDetails are in %LOCALAPPDATA%\\OpenNOW\\startup.log.");
        return 1;
    }
    catch (...) {
        Report(L"OpenNOW failed to start",
               L"OpenNOW could not start because of an unknown error.\n\nDetails are in "
               L"%LOCALAPPDATA%\\OpenNOW\\startup.log.");
        return 1;
    }
}

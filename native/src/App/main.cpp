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

#include "App.h"

#include <chrono>
#include <fstream>
#include <string>

namespace
{
    std::wstring StartupLogPath()
    {
        wchar_t localAppData[MAX_PATH]{};
        std::wstring directory = L"OpenNOW";
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) != 0) {
            directory = std::wstring(localAppData) + L"\\OpenNOW";
        }
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory + L"\\startup.log";
    }

    void WriteLog(std::wstring const& message)
    {
        std::ofstream log(StartupLogPath(), std::ios::app);
        if (!log) {
            return;
        }
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
        localtime_s(&local, &time);
        wchar_t stamp[32]{};
        wcsftime(stamp, std::size(stamp), L"%Y-%m-%d %H:%M:%S", &local);
        log << stamp << L"  " << message << L"\n";
    }

    int Report(std::wstring const& message)
    {
        WriteLog(message);
        MessageBoxW(nullptr, message.c_str(), L"OpenNOW", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::wstring Describe(winrt::hresult_error const& error)
    {
        return L"OpenNOW could not start.\n\n" + std::wstring(error.message().c_str()) +
               L"\n\nHRESULT: 0x" + std::to_wstring(static_cast<uint32_t>(error.code())) +
               L"\nDetails are in " + StartupLogPath();
    }

    std::wstring Describe(std::exception const& error)
    {
        std::wstring text(error.what(), error.what() + std::strlen(error.what()));
        return L"OpenNOW could not start.\n\n" + text + L"\n\nDetails are in " + StartupLogPath();
    }
}  // namespace

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    WriteLog(L"entry point reached");

    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        WriteLog(L"apartment initialised");

        Microsoft::UI::Xaml::Application::Start([](auto&&) {
            winrt::make<winrt::OpenNOW::implementation::App>();
        });
        WriteLog(L"application exited cleanly");

        winrt::uninit_apartment();
        return 0;
    }
    catch (winrt::hresult_error const& error) {
        return Report(Describe(error));
    }
    catch (std::exception const& error) {
        return Report(Describe(error));
    }
    catch (...) {
        return Report(L"OpenNOW could not start: an unknown exception was thrown during startup.");
    }
}

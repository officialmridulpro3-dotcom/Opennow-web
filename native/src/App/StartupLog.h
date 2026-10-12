#pragma once
#include <string>

// Appends one line to %LOCALAPPDATA%\OpenNOW\startup.log.
//
// While the process is starting up there is no window and no logger yet, so a
// failure here would otherwise be completely silent. Keep these calls cheap:
// each one opens, writes and closes the file.
void WriteStartupLog(std::wstring const& message);

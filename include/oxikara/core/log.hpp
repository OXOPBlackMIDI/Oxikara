#pragma once

#include <string_view>

namespace oxikara::core {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

void log(LogLevel level, std::string_view message);

} // namespace oxikara::core

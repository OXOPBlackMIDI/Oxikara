#include "oxikara/core/log.hpp"

#include <iostream>

namespace oxikara::core {

void log(const LogLevel level, const std::string_view message)
{
    const char* label = "INFO";
    switch (level) {
    case LogLevel::Debug:
        label = "DEBUG";
        break;
    case LogLevel::Info:
        label = "INFO";
        break;
    case LogLevel::Warn:
        label = "WARN";
        break;
    case LogLevel::Error:
        label = "ERROR";
        break;
    }

    std::ostream& out = (level == LogLevel::Error) ? std::cerr : std::cout;
    out << "[" << label << "] " << message << '\n';
}

} // namespace oxikara::core

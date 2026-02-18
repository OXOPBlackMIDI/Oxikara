#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace oxikara::core {

struct AppConfig {
    std::optional<std::string> midi_path;
    std::size_t max_midi_events_per_frame = 4096;
};

AppConfig parse_args(int argc, char** argv);

} // namespace oxikara::core

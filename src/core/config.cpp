#include "oxikara/core/config.hpp"

#include <cstdlib>
#include <string_view>

namespace oxikara::core {

AppConfig parse_args(const int argc, char** argv)
{
    AppConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--midi" && (i + 1) < argc) {
            config.midi_path = std::string(argv[++i]);
            continue;
        }
        if (arg == "--max-midi-events-per-frame" && (i + 1) < argc) {
            config.max_midi_events_per_frame = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
            continue;
        }
    }

    return config;
}

} // namespace oxikara::core

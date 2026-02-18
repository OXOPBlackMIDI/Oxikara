#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace oxikara::midi {

struct MidiHeader {
    std::uint16_t format = 0;
    std::uint16_t track_count = 0;
    std::uint16_t division = 0;
};

class MidiEventHandler {
public:
    virtual ~MidiEventHandler() = default;

    virtual void on_header(const MidiHeader& header) = 0;
    virtual void on_track_start(std::uint16_t track_index, std::uint32_t track_length) = 0;
    virtual void on_channel_event(
        std::uint32_t tick,
        std::uint8_t status,
        std::uint8_t data1,
        bool has_data2,
        std::uint8_t data2) = 0;
    virtual void on_meta_event(std::uint32_t tick, std::uint8_t type, std::uint32_t data_length) = 0;
    virtual void on_meta_data(std::uint32_t tick, std::uint8_t type, const std::uint8_t* bytes, std::size_t size) = 0;
    virtual void on_sysex_event(std::uint32_t tick, std::uint8_t status, std::uint32_t data_length) = 0;
    virtual void on_sysex_data(std::uint32_t tick, std::uint8_t status, const std::uint8_t* bytes, std::size_t size) = 0;
    virtual void on_track_end(std::uint16_t track_index, std::uint32_t tick) = 0;
};

class MidiStreamParser {
public:
    bool parse_file(const std::filesystem::path& path, MidiEventHandler& handler) const;
};

} // namespace oxikara::midi

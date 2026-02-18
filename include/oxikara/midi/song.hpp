#pragma once

#include "oxikara/midi/stream_parser.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace oxikara::midi {

struct NoteEvent {
    std::uint8_t channel = 0;
    std::uint8_t note = 0;
    std::uint8_t velocity = 100;
    std::uint32_t start_tick = 0;
    std::uint32_t end_tick = 0;
};

struct TempoChange {
    std::uint32_t tick = 0;
    std::uint32_t microseconds_per_quarter = 500000;
};

struct MidiSong {
    std::uint16_t track_count = 0;
    std::uint16_t format = 0;
    std::uint16_t division = 480;
    std::vector<NoteEvent> notes;
    std::vector<TempoChange> tempo;
    std::uint32_t end_tick = 0;
    std::uint64_t total_note_count = 0;
    std::uint64_t channel_event_count = 0;
    std::uint64_t meta_event_count = 0;
    std::uint64_t sysex_event_count = 0;
    std::uint64_t ignored_overlap_note_ons = 0;
};

bool load_song_streaming(const std::filesystem::path& path, MidiSong& out_song);
double tick_to_seconds(const MidiSong& song, std::uint32_t tick);
double duration_seconds(const MidiSong& song);

} // namespace oxikara::midi



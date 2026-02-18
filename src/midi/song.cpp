#include "oxikara/midi/song.hpp"

#include <algorithm>
#include <array>

namespace {

constexpr std::size_t kChannels = 16;
constexpr std::size_t kNotes = 128;
constexpr std::size_t kSlotsPerTrack = kChannels * kNotes;

struct ActiveState {
    bool on = false;
    std::uint32_t tick = 0;
    std::uint8_t velocity = 100;
};

class SongBuilder final : public oxikara::midi::MidiEventHandler {
public:
    void on_header(const oxikara::midi::MidiHeader& header) override
    {
        song_.format = header.format;
        song_.track_count = header.track_count;
        song_.division = header.division == 0 ? 480 : header.division;
        active_.assign(static_cast<std::size_t>(header.track_count) * kSlotsPerTrack, ActiveState{});
    }

    void on_track_start(const std::uint16_t track_index, std::uint32_t) override
    {
        current_track_ = track_index;
    }

    void on_channel_event(
        const std::uint32_t tick,
        const std::uint8_t status,
        const std::uint8_t data1,
        const bool has_data2,
        const std::uint8_t data2) override
    {
        ++song_.channel_event_count;
        const std::uint8_t type = static_cast<std::uint8_t>(status & 0xF0U);
        const std::uint8_t channel = static_cast<std::uint8_t>(status & 0x0FU);

        if (!has_data2) {
            song_.end_tick = std::max(song_.end_tick, tick);
            return;
        }

        const std::uint8_t note = data1;
        const std::uint8_t velocity = data2;
        ActiveState& st = slot(current_track_, channel, note);

        if (type == 0x90U && velocity > 0) {
            if (!st.on) {
                st.on = true;
                st.tick = tick;
                st.velocity = velocity;
            } else {
                ++song_.ignored_overlap_note_ons;
            }
        } else if (type == 0x80U || (type == 0x90U && velocity == 0)) {
            if (st.on && tick >= st.tick) {
                song_.notes.push_back({channel, note, st.velocity, st.tick, tick});
                ++song_.total_note_count;
            }
            st.on = false;
        }

        song_.end_tick = std::max(song_.end_tick, tick);
    }

    void on_meta_event(const std::uint32_t tick, const std::uint8_t type, const std::uint32_t data_length) override
    {
        ++song_.meta_event_count;
        pending_tempo_tick_ = 0;
        pending_tempo_bytes_ = 0;
        pending_tempo_valid_ = false;

        if (type == 0x51U && data_length == 3) {
            pending_tempo_tick_ = tick;
            pending_tempo_valid_ = true;
        }

        song_.end_tick = std::max(song_.end_tick, tick);
    }

    void on_meta_data(std::uint32_t, std::uint8_t, const std::uint8_t* bytes, const std::size_t size) override
    {
        if (!pending_tempo_valid_) {
            return;
        }

        for (std::size_t i = 0; i < size && pending_tempo_bytes_ < 3; ++i) {
            pending_tempo_[pending_tempo_bytes_++] = bytes[i];
        }

        if (pending_tempo_bytes_ == 3) {
            const std::uint32_t mpqn = (static_cast<std::uint32_t>(pending_tempo_[0]) << 16U)
                | (static_cast<std::uint32_t>(pending_tempo_[1]) << 8U) | static_cast<std::uint32_t>(pending_tempo_[2]);
            song_.tempo.push_back({pending_tempo_tick_, mpqn == 0 ? 500000U : mpqn});
            pending_tempo_valid_ = false;
        }
    }

    void on_sysex_event(std::uint32_t, std::uint8_t, std::uint32_t) override { ++song_.sysex_event_count; }
    void on_sysex_data(std::uint32_t, std::uint8_t, const std::uint8_t*, std::size_t) override {}

    void on_track_end(const std::uint16_t, const std::uint32_t tick) override
    {
        song_.end_tick = std::max(song_.end_tick, tick);
    }

    void finalize()
    {
        for (std::size_t idx = 0; idx < active_.size(); ++idx) {
            const std::size_t slot_id = idx % kSlotsPerTrack;
            const std::uint8_t channel = static_cast<std::uint8_t>(slot_id / kNotes);
            const std::uint8_t note = static_cast<std::uint8_t>(slot_id % kNotes);
            const ActiveState& st = active_[idx];
            if (st.on && song_.end_tick >= st.tick) {
                song_.notes.push_back({channel, note, st.velocity, st.tick, song_.end_tick});
                ++song_.total_note_count;
            }
        }

        if (song_.tempo.empty()) {
            song_.tempo.push_back({0U, 500000U});
        }

        std::sort(song_.tempo.begin(), song_.tempo.end(), [](const auto& a, const auto& b) {
            return a.tick < b.tick;
        });

        std::vector<oxikara::midi::TempoChange> dedup;
        dedup.reserve(song_.tempo.size());
        for (const auto& t : song_.tempo) {
            if (!dedup.empty() && dedup.back().tick == t.tick) {
                dedup.back().microseconds_per_quarter = t.microseconds_per_quarter;
            } else {
                dedup.push_back(t);
            }
        }
        song_.tempo = std::move(dedup);

        std::sort(song_.notes.begin(), song_.notes.end(), [](const auto& a, const auto& b) {
            if (a.start_tick != b.start_tick) {
                return a.start_tick < b.start_tick;
            }
            if (a.channel != b.channel) {
                return a.channel < b.channel;
            }
            return a.note < b.note;
        });
    }

    const oxikara::midi::MidiSong& song() const { return song_; }

private:
    ActiveState& slot(const std::uint16_t track, const std::uint8_t channel, const std::uint8_t note)
    {
        const std::size_t t = static_cast<std::size_t>(track);
        const std::size_t c = static_cast<std::size_t>(channel);
        const std::size_t n = static_cast<std::size_t>(note);
        return active_[t * kSlotsPerTrack + c * kNotes + n];
    }

    oxikara::midi::MidiSong song_{};
    std::uint16_t current_track_ = 0;
    std::vector<ActiveState> active_;

    bool pending_tempo_valid_ = false;
    std::uint32_t pending_tempo_tick_ = 0;
    std::array<std::uint8_t, 3> pending_tempo_{};
    std::size_t pending_tempo_bytes_ = 0;
};

} // namespace

namespace oxikara::midi {

bool load_song_streaming(const std::filesystem::path& path, MidiSong& out_song)
{
    SongBuilder builder;
    MidiStreamParser parser;
    if (!parser.parse_file(path, builder)) {
        return false;
    }
    builder.finalize();
    out_song = builder.song();
    return true;
}

double tick_to_seconds(const MidiSong& song, const std::uint32_t tick)
{
    if (song.tempo.empty()) {
        return static_cast<double>(tick) * (500000.0 / 1000000.0) / static_cast<double>(song.division);
    }

    const double tpq = static_cast<double>(song.division);
    double seconds = 0.0;
    std::uint32_t prev_tick = 0;
    std::uint32_t current_mpqn = 500000;

    std::size_t i = 0;
    if (song.tempo[0].tick == 0) {
        current_mpqn = song.tempo[0].microseconds_per_quarter;
        i = 1;
    }

    while (i < song.tempo.size() && song.tempo[i].tick <= tick) {
        const std::uint32_t segment_ticks = song.tempo[i].tick - prev_tick;
        seconds += static_cast<double>(segment_ticks) * (static_cast<double>(current_mpqn) / 1000000.0) / tpq;
        prev_tick = song.tempo[i].tick;
        current_mpqn = song.tempo[i].microseconds_per_quarter;
        ++i;
    }

    const std::uint32_t remaining = tick - prev_tick;
    seconds += static_cast<double>(remaining) * (static_cast<double>(current_mpqn) / 1000000.0) / tpq;
    return seconds;
}

double duration_seconds(const MidiSong& song)
{
    return tick_to_seconds(song, song.end_tick);
}

} // namespace oxikara::midi



#include "oxikara/app.hpp"

#include "oxikara/audio/midi_output.hpp"
#include "oxikara/core/log.hpp"
#include "oxikara/midi/song.hpp"
#include "oxikara/render/vulkan_renderer.hpp"
#include "oxikara/version.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <future>
#include <iterator>
#include <new>
#include <queue>
#include <sstream>
#include <utility>
#include <vector>

namespace {

struct PendingOffEvent {
    double end_sec = 0.0;
    std::uint32_t msg = 0;
};

struct PendingOffEarlier {
    bool operator()(const PendingOffEvent& a, const PendingOffEvent& b) const { return a.end_sec > b.end_sec; }
};

bool is_black_key(const int midi_note)
{
    const int semitone = midi_note % 12;
    return semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
}

std::array<float, 3> note_color(const std::uint8_t note)
{
    static constexpr std::array<std::array<float, 3>, 12> palette = {{
        {0.98f, 0.42f, 0.42f},
        {0.98f, 0.55f, 0.36f},
        {0.98f, 0.72f, 0.34f},
        {0.95f, 0.84f, 0.32f},
        {0.76f, 0.88f, 0.34f},
        {0.54f, 0.90f, 0.40f},
        {0.38f, 0.90f, 0.56f},
        {0.34f, 0.85f, 0.78f},
        {0.36f, 0.73f, 0.94f},
        {0.50f, 0.62f, 0.97f},
        {0.70f, 0.52f, 0.96f},
        {0.90f, 0.48f, 0.90f},
    }};
    return palette[note % 12U];
}

std::uint32_t make_short_msg(const std::uint8_t status, const std::uint8_t data1, const std::uint8_t data2)
{
    return static_cast<std::uint32_t>(status) | (static_cast<std::uint32_t>(data1) << 8U)
        | (static_cast<std::uint32_t>(data2) << 16U);
}

struct TempoTimeline {
    std::vector<std::uint32_t> ticks;
    std::vector<double> sec_at_tick;
    std::vector<std::uint32_t> mpqn;
    double tick_sec = 1.0 / 960.0;

    double to_seconds(const std::uint32_t tick) const
    {
        if (ticks.empty()) {
            return static_cast<double>(tick) * tick_sec;
        }
        const auto it = std::upper_bound(ticks.begin(), ticks.end(), tick);
        const std::size_t i = (it == ticks.begin()) ? 0 : static_cast<std::size_t>(std::distance(ticks.begin(), it) - 1);
        return sec_at_tick[i] + static_cast<double>(tick - ticks[i]) * static_cast<double>(mpqn[i]) / 1000000.0 * tick_sec;
    }
};

TempoTimeline build_tempo_timeline(const oxikara::midi::MidiSong& song)
{
    TempoTimeline t;
    t.tick_sec = 1.0 / static_cast<double>((song.division == 0) ? 480 : song.division);

    if (song.tempo.empty()) {
        t.ticks.push_back(0);
        t.sec_at_tick.push_back(0.0);
        t.mpqn.push_back(500000);
        return t;
    }

    t.ticks.reserve(song.tempo.size());
    t.sec_at_tick.resize(song.tempo.size(), 0.0);
    t.mpqn.reserve(song.tempo.size());

    for (const auto& change : song.tempo) {
        t.ticks.push_back(change.tick);
        t.mpqn.push_back((change.microseconds_per_quarter == 0) ? 500000U : change.microseconds_per_quarter);
    }

    for (std::size_t i = 1; i < t.ticks.size(); ++i) {
        const std::uint32_t prev_tick = t.ticks[i - 1];
        const std::uint32_t curr_tick = t.ticks[i];
        const std::uint32_t dt = curr_tick - prev_tick;
        t.sec_at_tick[i] = t.sec_at_tick[i - 1] + static_cast<double>(dt) * static_cast<double>(t.mpqn[i - 1]) / 1000000.0 * t.tick_sec;
    }
    return t;
}

} // namespace

namespace oxikara {

int App::run(const int argc, char** argv) const
{
    const core::AppConfig cfg = core::parse_args(argc, argv);
    core::log(core::LogLevel::Info, std::string("Oxikara ") + kVersion + " (C++/CMake)");

    if (!cfg.midi_path.has_value()) {
        core::log(core::LogLevel::Error, "Missing MIDI file. Use --midi <path.mid>.");
        return 1;
    }

    midi::MidiSong song;
    core::log(core::LogLevel::Info, "Parsing MIDI on worker thread...");

    auto parse_job = std::async(std::launch::async, [&song, &cfg]() {
        return midi::load_song_streaming(*cfg.midi_path, song);
    });

    std::size_t dots = 0;
    while (parse_job.wait_for(std::chrono::milliseconds(250)) != std::future_status::ready) {
        ++dots;
        if (dots % 4 == 0) {
            core::log(core::LogLevel::Info, "Parsing still running...");
        }
    }

    if (!parse_job.get()) {
        core::log(core::LogLevel::Error, "Failed to parse MIDI stream.");
        return 1;
    }

    {
        std::ostringstream info;
        info << "Parse info: tracks=" << song.track_count << " channel_events=" << song.channel_event_count
             << " meta_events=" << song.meta_event_count << " sysex_events=" << song.sysex_event_count
             << " notes=" << song.total_note_count << " overlap_noteons_ignored=" << song.ignored_overlap_note_ons
             << " tempo_changes=" << song.tempo.size();
        core::log(core::LogLevel::Info, info.str());
    }

    render::RenderSong render_song;
    render_song.duration_sec = midi::duration_seconds(song);
    const TempoTimeline timeline = build_tempo_timeline(song);

    std::uint8_t min_note = 127;
    std::uint8_t max_note = 0;

    try {
        render_song.notes.reserve(song.notes.size());
        for (const auto& n : song.notes) {
            if (n.end_tick <= n.start_tick) {
                continue;
            }

            min_note = std::min(min_note, n.note);
            max_note = std::max(max_note, n.note);

            const double start_sec = timeline.to_seconds(n.start_tick);
            const double end_sec = timeline.to_seconds(n.end_tick);

            const auto color = note_color(n.note);
            const std::uint8_t on_velocity = (n.velocity == 0) ? 100 : n.velocity;
            render_song.notes.push_back(render::RenderNote{
                static_cast<std::uint8_t>(n.channel & 0x0FU),
                n.note,
                on_velocity,
                start_sec,
                end_sec,
                color[0],
                color[1],
                color[2]});
        }
    } catch (const std::bad_alloc&) {
        core::log(core::LogLevel::Error, "Out of memory while building render notes.");
        return 1;
    }

    if (render_song.notes.empty()) {
        core::log(core::LogLevel::Warn, "No note events found in MIDI file.");
        return 1;
    }

    render_song.note_count = static_cast<std::size_t>(song.total_note_count);

    render_song.min_note = 0;
    render_song.max_note = 127;

    std::ostringstream summary;
    summary << "Loaded MIDI notes(total=" << song.total_note_count << ", view=" << render_song.notes.size()
            << ") duration=" << render_song.duration_sec << "s range=[" << static_cast<int>(render_song.min_note)
            << "," << static_cast<int>(render_song.max_note) << "]";
    core::log(core::LogLevel::Info, summary.str());

    audio::MidiOutput midi_out;
    if (!midi_out.initialize()) {
        core::log(core::LogLevel::Warn, "No MIDI output backend available (KDMAPI/WinMM). Visualizer only.");
    } else {
        core::log(core::LogLevel::Info, std::string("MIDI backend: ") + midi_out.backend_name());
    }

    render::VulkanRenderer renderer;
    if (!renderer.initialize(1400, 820, "Oxikara Piano Roll")) {
        core::log(core::LogLevel::Error, "Renderer initialization failed.");
        return 1;
    }

    renderer.set_song(std::move(render_song));
    renderer.start_playback();

    std::size_t next_note = 0;
    std::priority_queue<PendingOffEvent, std::vector<PendingOffEvent>, PendingOffEarlier> pending_off;
    double last_play_time = 0.0;

    auto reset_midi_schedule = [&](const double target_time) {
        while (!pending_off.empty()) {
            pending_off.pop();
        }

        for (int ch = 0; ch < 16; ++ch) {
            midi_out.send_short(make_short_msg(static_cast<std::uint8_t>(0xB0u | static_cast<std::uint8_t>(ch)), 123, 0));
        }

        const auto it = std::lower_bound(song.notes.begin(), song.notes.end(), target_time, [&](const midi::NoteEvent& n, const double t) {
            return timeline.to_seconds(n.start_tick) < t;
        });
        next_note = static_cast<std::size_t>(std::distance(song.notes.begin(), it));
    };

    while (!renderer.should_close()) {
        if (!renderer.draw_frame()) {
            core::log(core::LogLevel::Warn, "Render loop stopped after renderer error.");
            break;
        }

        const double play_time = renderer.playback_time();
        if (play_time + 0.001 < last_play_time || std::abs(play_time - last_play_time) > 4.5) {
            reset_midi_schedule(play_time);
        }
        last_play_time = play_time;

        std::size_t sent_this_frame = 0;
        while (next_note < song.notes.size() && sent_this_frame < cfg.max_midi_events_per_frame) {
            const auto& n = song.notes[next_note];
            if (n.end_tick <= n.start_tick) {
                ++next_note;
                continue;
            }

            const std::uint8_t channel = static_cast<std::uint8_t>(n.channel & 0x0FU);
            const std::uint8_t velocity = (n.velocity == 0) ? 100 : n.velocity;
            const double start_sec = timeline.to_seconds(n.start_tick);
            if (start_sec > play_time + 0.001) {
                break;
            }
            const double end_sec = timeline.to_seconds(n.end_tick);

            midi_out.send_short(make_short_msg(static_cast<std::uint8_t>(0x90U | channel), n.note, velocity));
            pending_off.push(PendingOffEvent{
                end_sec,
                make_short_msg(static_cast<std::uint8_t>(0x80U | channel), n.note, 0)});
            ++next_note;
            ++sent_this_frame;
        }

        while (!pending_off.empty() && pending_off.top().end_sec <= play_time + 0.001
               && sent_this_frame < cfg.max_midi_events_per_frame) {
            midi_out.send_short(pending_off.top().msg);
            pending_off.pop();
            ++sent_this_frame;
        }
    }

    while (!pending_off.empty()) {
        midi_out.send_short(pending_off.top().msg);
        pending_off.pop();
    }

    renderer.shutdown();
    midi_out.shutdown();

    core::log(core::LogLevel::Info, "Exiting.");
    return 0;
}

} // namespace oxikara


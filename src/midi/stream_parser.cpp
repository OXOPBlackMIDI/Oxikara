#include "oxikara/midi/stream_parser.hpp"

#include <array>
#include <fstream>

namespace {

using oxikara::midi::MidiEventHandler;
using oxikara::midi::MidiHeader;

bool read_exact(std::istream& in, void* data, const std::size_t size)
{
    in.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return in.good();
}

std::uint16_t read_be16(std::istream& in, bool& ok)
{
    std::array<std::uint8_t, 2> b{};
    ok = read_exact(in, b.data(), b.size());
    if (!ok) {
        return 0;
    }
    return static_cast<std::uint16_t>((b[0] << 8U) | b[1]);
}

std::uint32_t read_be32(std::istream& in, bool& ok)
{
    std::array<std::uint8_t, 4> b{};
    ok = read_exact(in, b.data(), b.size());
    if (!ok) {
        return 0;
    }
    return (static_cast<std::uint32_t>(b[0]) << 24U) | (static_cast<std::uint32_t>(b[1]) << 16U)
        | (static_cast<std::uint32_t>(b[2]) << 8U) | static_cast<std::uint32_t>(b[3]);
}

bool read_var_len(std::istream& in, std::uint32_t& out, std::uint32_t& used)
{
    out = 0;
    used = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint8_t byte = 0;
        if (!read_exact(in, &byte, 1)) {
            return false;
        }
        ++used;
        out = (out << 7U) | static_cast<std::uint32_t>(byte & 0x7FU);
        if ((byte & 0x80U) == 0) {
            return true;
        }
    }
    return false;
}

int channel_data_bytes(const std::uint8_t status)
{
    switch (status & 0xF0U) {
    case 0xC0:
    case 0xD0:
        return 1;
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
        return 2;
    default:
        return -1;
    }
}

bool skip_bytes(std::istream& in, std::uint32_t amount)
{
    constexpr std::size_t kChunk = 4096;
    std::array<std::uint8_t, kChunk> scratch{};
    std::uint32_t left = amount;
    while (left > 0) {
        const std::size_t now = static_cast<std::size_t>(left < kChunk ? left : kChunk);
        if (!read_exact(in, scratch.data(), now)) {
            return false;
        }
        left -= static_cast<std::uint32_t>(now);
    }
    return true;
}

bool stream_blob(
    std::istream& in,
    std::uint32_t tick,
    std::uint8_t status_or_type,
    std::uint32_t len,
    const bool meta,
    MidiEventHandler& handler)
{
    constexpr std::size_t kChunk = 1024;
    std::array<std::uint8_t, kChunk> buffer{};
    std::uint32_t left = len;
    while (left > 0) {
        const std::size_t now = static_cast<std::size_t>(left < kChunk ? left : kChunk);
        if (!read_exact(in, buffer.data(), now)) {
            return false;
        }
        if (meta) {
            handler.on_meta_data(tick, status_or_type, buffer.data(), now);
        } else {
            handler.on_sysex_data(tick, status_or_type, buffer.data(), now);
        }
        left -= static_cast<std::uint32_t>(now);
    }
    return true;
}

bool parse_track(std::istream& in, const std::uint16_t track_index, MidiEventHandler& handler)
{
    std::array<char, 4> id{};
    if (!read_exact(in, id.data(), id.size())) {
        return false;
    }
    if (!(id[0] == 'M' && id[1] == 'T' && id[2] == 'r' && id[3] == 'k')) {
        return false;
    }

    bool ok = false;
    const std::uint32_t track_length = read_be32(in, ok);
    if (!ok) {
        return false;
    }

    handler.on_track_start(track_index, track_length);

    std::uint32_t bytes_left = track_length;
    std::uint32_t tick = 0;
    std::uint8_t running_status = 0;

    while (bytes_left > 0) {
        std::uint32_t consumed = 0;

        std::uint32_t delta = 0;
        std::uint32_t used = 0;
        if (!read_var_len(in, delta, used)) {
            return false;
        }
        consumed += used;
        tick += delta;

        std::uint8_t first = 0;
        if (!read_exact(in, &first, 1)) {
            return false;
        }
        ++consumed;

        std::uint8_t status = first;
        bool had_status_byte = true;

        if ((first & 0x80U) == 0) {
            if (running_status == 0) {
                return false;
            }
            status = running_status;
            had_status_byte = false;
        }

        if ((status & 0xF0U) >= 0x80U && (status & 0xF0U) <= 0xE0U) {
            const int needed = channel_data_bytes(status);
            if (needed < 0) {
                return false;
            }

            std::uint8_t data1 = 0;
            std::uint8_t data2 = 0;
            if (had_status_byte) {
                if (!read_exact(in, &data1, 1)) {
                    return false;
                }
                ++consumed;
            } else {
                data1 = first;
            }

            if (needed == 2) {
                if (!read_exact(in, &data2, 1)) {
                    return false;
                }
                ++consumed;
            }

            handler.on_channel_event(tick, status, data1, needed == 2, data2);
            running_status = status;
        } else if (status == 0xFFU) {
            std::uint8_t meta_type = 0;
            if (!read_exact(in, &meta_type, 1)) {
                return false;
            }
            ++consumed;

            std::uint32_t len = 0;
            used = 0;
            if (!read_var_len(in, len, used)) {
                return false;
            }
            consumed += used;

            handler.on_meta_event(tick, meta_type, len);
            if (!stream_blob(in, tick, meta_type, len, true, handler)) {
                return false;
            }
            consumed += len;
            running_status = 0;
        } else if (status == 0xF0U || status == 0xF7U) {
            std::uint32_t len = 0;
            used = 0;
            if (!read_var_len(in, len, used)) {
                return false;
            }
            consumed += used;

            handler.on_sysex_event(tick, status, len);
            if (!stream_blob(in, tick, status, len, false, handler)) {
                return false;
            }
            consumed += len;
            running_status = 0;
        } else {
            return false;
        }

        if (consumed > bytes_left) {
            return false;
        }
        bytes_left -= consumed;
    }

    handler.on_track_end(track_index, tick);
    return true;
}

} // namespace

namespace oxikara::midi {

bool MidiStreamParser::parse_file(const std::filesystem::path& path, MidiEventHandler& handler) const
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    std::array<char, 4> chunk{};
    if (!read_exact(in, chunk.data(), chunk.size())) {
        return false;
    }
    if (!(chunk[0] == 'M' && chunk[1] == 'T' && chunk[2] == 'h' && chunk[3] == 'd')) {
        return false;
    }

    bool ok = false;
    const std::uint32_t header_len = read_be32(in, ok);
    if (!ok || header_len < 6) {
        return false;
    }

    MidiHeader header;
    header.format = read_be16(in, ok);
    if (!ok) {
        return false;
    }
    header.track_count = read_be16(in, ok);
    if (!ok) {
        return false;
    }
    header.division = read_be16(in, ok);
    if (!ok) {
        return false;
    }

    if (header_len > 6 && !skip_bytes(in, header_len - 6)) {
        return false;
    }

    handler.on_header(header);

    for (std::uint16_t i = 0; i < header.track_count; ++i) {
        if (!parse_track(in, i, handler)) {
            return false;
        }
    }

    return true;
}

} // namespace oxikara::midi

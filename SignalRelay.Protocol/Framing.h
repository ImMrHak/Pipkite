#pragma once
// Wire framing: 4-byte little-endian payload length + UTF-8 JSON payload, max 64 KiB.
// The decoder validates the length before allocating and handles partial reads.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sr::ipc {

inline constexpr std::uint32_t kMaxFrame = 64 * 1024;

std::string encodeFrame(std::string_view payload);  // empty result if payload too large

class FrameDecoder {
public:
    enum class State { NeedMore, Frame, Malformed };
    // Appends bytes; call next() repeatedly to extract complete frames.
    void feed(const char* data, std::size_t len);
    State next(std::string& payloadOut);
    std::size_t buffered() const { return buf_.size() - pos_; }
    void reset() {
        buf_.clear();
        pos_ = 0;
    }

private:
    std::vector<char> buf_;
    std::size_t pos_ = 0;
};

}  // namespace sr::ipc

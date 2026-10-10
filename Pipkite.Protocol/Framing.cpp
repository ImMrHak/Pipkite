#include "Framing.h"

#include <cstring>

namespace sr::ipc {

std::string encodeFrame(std::string_view payload) {
    if (payload.empty() || payload.size() > kMaxFrame) return {};
    std::string out;
    out.resize(4 + payload.size());
    const auto n = static_cast<std::uint32_t>(payload.size());
    out[0] = static_cast<char>(n & 0xFF);
    out[1] = static_cast<char>((n >> 8) & 0xFF);
    out[2] = static_cast<char>((n >> 16) & 0xFF);
    out[3] = static_cast<char>((n >> 24) & 0xFF);
    std::memcpy(out.data() + 4, payload.data(), payload.size());
    return out;
}

void FrameDecoder::feed(const char* data, std::size_t len) {
    // Compact consumed bytes occasionally to keep the buffer bounded.
    if (pos_ > 0 && pos_ >= buf_.size() / 2) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos_));
        pos_ = 0;
    }
    buf_.insert(buf_.end(), data, data + len);
}

FrameDecoder::State FrameDecoder::next(std::string& out) {
    const std::size_t avail = buf_.size() - pos_;
    if (avail < 4) return State::NeedMore;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(buf_.data() + pos_);
    const std::uint32_t n = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
                            (static_cast<std::uint32_t>(p[3]) << 24);
    if (n == 0 || n > kMaxFrame) return State::Malformed;  // validated before any allocation
    if (avail < 4 + static_cast<std::size_t>(n)) return State::NeedMore;
    out.assign(buf_.data() + pos_ + 4, n);
    pos_ += 4 + n;
    return State::Frame;
}

}  // namespace sr::ipc

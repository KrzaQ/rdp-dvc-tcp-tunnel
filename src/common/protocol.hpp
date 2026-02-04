#pragma once

#include <cstdint>

namespace kq {

inline constexpr char const* channelName = "KQTUNNEL";
inline constexpr char const* pipeName = R"(\\.\pipe\kq-tunnel)";
inline constexpr uint16_t defaultLocalPort = 2222;

} // namespace kq

#pragma once

#include <cstdint>

namespace kq {

inline constexpr char const* channelName = "KQTUNNEL";
inline constexpr char const* pipeName = R"(\\.\pipe\kq-tunnel)";
inline constexpr uint16_t defaultLocalPort = 2222;
inline constexpr char const* defaultTargetHost = "localhost";
inline constexpr uint16_t defaultTargetPort = 22;
inline constexpr size_t bufferSize = 8192;

} // namespace kq

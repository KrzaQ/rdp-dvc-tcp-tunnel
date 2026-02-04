#include <spdlog/spdlog.h>

#include "protocol.hpp"

int main(int argc, char* argv[])
{
    // TODO: parse target host:port from args (default localhost:22)
    spdlog::info("kq-tunnel-server starting, channel: {}", kq::channelName);

    // TODO: open DVC with WTSVirtualChannelOpenEx
    // TODO: connect to target TCP endpoint
    // TODO: bridge DVC <-> TCP

    return 0;
}

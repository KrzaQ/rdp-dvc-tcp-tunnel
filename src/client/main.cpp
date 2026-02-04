#include <spdlog/spdlog.h>

#include "protocol.hpp"

int main()
{
    spdlog::info("kq-tunnel-client starting on port {}", kq::defaultLocalPort);

    // TODO: open named pipe to plugin
    // TODO: listen on localhost:defaultLocalPort
    // TODO: bridge TCP <-> named pipe

    return 0;
}

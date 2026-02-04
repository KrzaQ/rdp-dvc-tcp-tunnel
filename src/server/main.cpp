#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include <windows.h>
#include <wtsapi32.h>

#include "protocol.hpp"

namespace {

std::atomic<bool> running{true};

void dvcToTcp(HANDLE dvc, asio::ip::tcp::socket& socket)
{
    std::vector<char> buf(kq::bufferSize);
    while (running) {
        ULONG bytesRead = 0;
        if (!WTSVirtualChannelRead(dvc, INFINITE, buf.data(),
                static_cast<ULONG>(buf.size()), &bytesRead)) {
            spdlog::info("DVC read ended ({})", GetLastError());
            break;
        }
        if (bytesRead == 0)
            continue;

        asio::error_code ec;
        asio::write(socket, asio::buffer(buf.data(), bytesRead), ec);
        if (ec) {
            spdlog::info("TCP write failed: {}", ec.message());
            break;
        }
    }
    running = false;
    asio::error_code ec;
    socket.close(ec);
}

void tcpToDvc(asio::ip::tcp::socket& socket, HANDLE dvc)
{
    std::vector<char> buf(kq::bufferSize);
    while (running) {
        asio::error_code ec;
        auto n = socket.read_some(asio::buffer(buf), ec);
        if (ec) {
            spdlog::info("TCP read ended: {}", ec.message());
            break;
        }

        ULONG bytesWritten = 0;
        if (!WTSVirtualChannelWrite(dvc, buf.data(),
                static_cast<ULONG>(n), &bytesWritten)) {
            spdlog::info("DVC write failed ({})", GetLastError());
            break;
        }
    }
    running = false;
    WTSVirtualChannelClose(dvc);
}

} // namespace

int main(int argc, char* argv[])
{
    std::string host = kq::defaultTargetHost;
    std::string port = std::to_string(kq::defaultTargetPort);

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = argv[2];

    spdlog::info("kq-tunnel-server starting");
    spdlog::info("  channel: {}", kq::channelName);
    spdlog::info("  target:  {}:{}", host, port);

    // Open DVC
    HANDLE dvc = WTSVirtualChannelOpenEx(
        WTS_CURRENT_SESSION,
        const_cast<LPSTR>(kq::channelName),
        WTS_CHANNEL_OPTION_DYNAMIC);

    if (dvc == nullptr) {
        spdlog::error("Failed to open DVC '{}' (error {})",
            kq::channelName, GetLastError());
        return 1;
    }
    spdlog::info("DVC opened");

    // Connect to target TCP endpoint
    asio::io_context io;
    asio::ip::tcp::resolver resolver(io);
    asio::ip::tcp::socket socket(io);

    try {
        auto endpoints = resolver.resolve(host, port);
        asio::connect(socket, endpoints);
    } catch (std::exception const& e) {
        spdlog::error("TCP connect to {}:{} failed: {}", host, port, e.what());
        WTSVirtualChannelClose(dvc);
        return 1;
    }
    spdlog::info("Connected to {}:{}", host, port);

    // Bridge DVC <-> TCP with two threads
    std::thread t1(dvcToTcp, dvc, std::ref(socket));
    std::thread t2(tcpToDvc, std::ref(socket), dvc);

    t1.join();
    t2.join();

    spdlog::info("Shutting down");
    return 0;
}

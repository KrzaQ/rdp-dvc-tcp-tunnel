#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include <windows.h>
#include <wtsapi32.h>

#include "protocol.hpp"

namespace {

// ReadFile on a DVC file handle returns CHANNEL_PDU_HEADER (8 bytes) + payload.
// WriteFile takes raw payload (no header needed).
constexpr DWORD channelPduHeaderSize = 8;

bool waitForIo(HANDLE file, OVERLAPPED& ov, DWORD& bytes, HANDLE cancelEvent)
{
    HANDLE handles[] = {ov.hEvent, cancelEvent};
    DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (wait == WAIT_OBJECT_0) {
        return GetOverlappedResult(file, &ov, &bytes, FALSE);
    }
    CancelIoEx(file, &ov);
    GetOverlappedResult(file, &ov, &bytes, TRUE);
    return false;
}

void dvcToTcp(HANDLE fileHandle, asio::ip::tcp::socket& socket, HANDLE cancelEvent)
{
    std::vector<char> buf(kq::bufferSize);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    for (;;) {
        DWORD bytesRead = 0;
        ResetEvent(ov.hEvent);
        BOOL ok = ReadFile(fileHandle, buf.data(),
            static_cast<DWORD>(buf.size()), &bytesRead, &ov);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                if (!waitForIo(fileHandle, ov, bytesRead, cancelEvent)) {
                    spdlog::info("DVC read ended ({})", GetLastError());
                    break;
                }
            } else {
                spdlog::info("DVC read ended ({})", err);
                break;
            }
        }

        if (bytesRead <= channelPduHeaderSize)
            continue;

        auto* payload = buf.data() + channelPduHeaderSize;
        auto payloadLen = bytesRead - channelPduHeaderSize;

        asio::error_code ec;
        asio::write(socket, asio::buffer(payload, payloadLen), ec);
        if (ec) {
            spdlog::info("TCP write failed: {}", ec.message());
            break;
        }
    }
    CloseHandle(ov.hEvent);
    SetEvent(cancelEvent);
    asio::error_code ec;
    socket.close(ec);
}

void tcpToDvc(asio::ip::tcp::socket& socket, HANDLE fileHandle, HANDLE cancelEvent)
{
    std::vector<char> buf(kq::bufferSize);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    for (;;) {
        asio::error_code ec;
        auto n = socket.read_some(asio::buffer(buf), ec);
        if (ec) {
            spdlog::info("TCP read ended: {}", ec.message());
            break;
        }

        DWORD bytesWritten = 0;
        ResetEvent(ov.hEvent);
        BOOL ok = WriteFile(fileHandle, buf.data(),
            static_cast<DWORD>(n), &bytesWritten, &ov);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                if (!waitForIo(fileHandle, ov, bytesWritten, cancelEvent)) {
                    spdlog::info("DVC write failed ({})", GetLastError());
                    break;
                }
            } else {
                spdlog::info("DVC write failed ({})", err);
                break;
            }
        }
    }
    CloseHandle(ov.hEvent);
    SetEvent(cancelEvent);
}

} // namespace

struct DvcHandles {
    HANDLE channel;
    HANDLE file;
};

DvcHandles openDvc()
{
    HANDLE dvc = WTSVirtualChannelOpenEx(
        WTS_CURRENT_SESSION,
        const_cast<LPSTR>(kq::channelName),
        WTS_CHANNEL_OPTION_DYNAMIC);

    if (dvc == nullptr) {
        spdlog::error("Failed to open DVC '{}' (error {})",
            kq::channelName, GetLastError());
        return {nullptr, nullptr};
    }
    spdlog::info("DVC opened");

    PVOID buffer = nullptr;
    DWORD len = 0;
    if (!WTSVirtualChannelQuery(dvc, WTSVirtualFileHandle, &buffer, &len)) {
        spdlog::error("Failed to query DVC file handle (error {})", GetLastError());
        WTSVirtualChannelClose(dvc);
        return {nullptr, nullptr};
    }

    HANDLE fileHandle = *reinterpret_cast<HANDLE*>(buffer);

    HANDLE dupHandle = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), fileHandle,
            GetCurrentProcess(), &dupHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        spdlog::error("DuplicateHandle failed (error {})", GetLastError());
        WTSFreeMemory(buffer);
        WTSVirtualChannelClose(dvc);
        return {nullptr, nullptr};
    }
    WTSFreeMemory(buffer);

    spdlog::info("DVC file handle acquired");
    return {dvc, dupHandle};
}

int main(int argc, char* argv[])
{
    enum class Mode { connect, listen };
    Mode mode = Mode::connect;
    int argOffset = 1;

    if (argc >= 2) {
        std::string_view cmd = argv[1];
        if (cmd == "connect") {
            mode = Mode::connect;
            argOffset = 2;
        } else if (cmd == "listen") {
            mode = Mode::listen;
            argOffset = 2;
        }
    }

    spdlog::info("kq-tunnel-server starting");
    spdlog::info("  channel: {}", kq::channelName);

    auto [dvc, fileHandle] = openDvc();
    if (dvc == nullptr)
        return 1;

    asio::io_context io;
    HANDLE cancelEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    if (mode == Mode::connect) {
        std::string host = kq::defaultTargetHost;
        std::string port = std::to_string(kq::defaultTargetPort);
        if (argOffset < argc)
            host = argv[argOffset];
        if (argOffset + 1 < argc)
            port = argv[argOffset + 1];

        spdlog::info("  mode: connect");
        spdlog::info("  target: {}:{}", host, port);

        asio::ip::tcp::resolver resolver(io);
        asio::ip::tcp::socket socket(io);

        try {
            auto endpoints = resolver.resolve(host, port);
            asio::connect(socket, endpoints);
        } catch (std::exception const& e) {
            spdlog::error("TCP connect to {}:{} failed: {}", host, port, e.what());
            CloseHandle(cancelEvent);
            CloseHandle(fileHandle);
            WTSVirtualChannelClose(dvc);
            return 1;
        }
        spdlog::info("Connected to {}:{}", host, port);

        std::thread t1(dvcToTcp, fileHandle, std::ref(socket), cancelEvent);
        std::thread t2(tcpToDvc, std::ref(socket), fileHandle, cancelEvent);

        t1.join();
        t2.join();
    } else {
        uint16_t port = kq::defaultTargetPort;
        if (argOffset < argc)
            port = static_cast<uint16_t>(std::stoi(argv[argOffset]));

        spdlog::info("  mode: listen");
        spdlog::info("  listen port: {}", port);

        asio::ip::tcp::acceptor acceptor(io,
            asio::ip::tcp::endpoint(asio::ip::make_address("0.0.0.0"), port));

        spdlog::info("Waiting for TCP connection on port {}...", port);
        asio::ip::tcp::socket socket(io);
        acceptor.accept(socket);
        spdlog::info("TCP connection accepted");

        std::thread t1(dvcToTcp, fileHandle, std::ref(socket), cancelEvent);
        std::thread t2(tcpToDvc, std::ref(socket), fileHandle, cancelEvent);

        t1.join();
        t2.join();
    }

    spdlog::info("Shutting down");
    CloseHandle(cancelEvent);
    CloseHandle(fileHandle);
    WTSVirtualChannelClose(dvc);
    return 0;
}

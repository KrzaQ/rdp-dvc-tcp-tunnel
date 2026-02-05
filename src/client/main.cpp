#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include <windows.h>

#include "protocol.hpp"

namespace {

std::atomic<bool> running{true};

HANDLE createPipe()
{
    HANDLE pipe = CreateNamedPipeA(
        kq::pipeName,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        kq::bufferSize,
        kq::bufferSize,
        0,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE) {
        spdlog::error("Failed to create named pipe (error {})", GetLastError());
    }
    return pipe;
}

void pipeToTcp(HANDLE pipe, asio::ip::tcp::socket& socket)
{
    std::vector<char> buf(kq::bufferSize);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    while (running) {
        DWORD bytesRead = 0;
        ResetEvent(ov.hEvent);
        BOOL ok = ReadFile(pipe, buf.data(), static_cast<DWORD>(buf.size()),
                &bytesRead, &ov);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                if (!GetOverlappedResult(pipe, &ov, &bytesRead, TRUE)) {
                    spdlog::info("Pipe read ended ({})", GetLastError());
                    break;
                }
            } else {
                spdlog::info("Pipe read ended ({})", err);
                break;
            }
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
    CloseHandle(ov.hEvent);
    asio::error_code ec;
    socket.close(ec);
}

void tcpToPipe(asio::ip::tcp::socket& socket, HANDLE pipe)
{
    std::vector<char> buf(kq::bufferSize);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    while (running) {
        asio::error_code ec;
        auto n = socket.read_some(asio::buffer(buf), ec);
        if (ec) {
            spdlog::info("TCP read ended: {}", ec.message());
            break;
        }

        DWORD written = 0;
        ResetEvent(ov.hEvent);
        BOOL ok = WriteFile(pipe, buf.data(), static_cast<DWORD>(n),
                &written, &ov);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                if (!GetOverlappedResult(pipe, &ov, &written, TRUE)) {
                    spdlog::info("Pipe write failed ({})", GetLastError());
                    break;
                }
            } else {
                spdlog::info("Pipe write failed ({})", err);
                break;
            }
        }
    }
    running = false;
    CloseHandle(ov.hEvent);
    CancelIoEx(pipe, nullptr);
}

} // namespace

int main(int argc, char* argv[])
{
    uint16_t port = kq::defaultLocalPort;
    if (argc >= 2) port = static_cast<uint16_t>(std::stoi(argv[1]));

    spdlog::info("kq-tunnel-client starting");
    spdlog::info("  listen port: {}", port);
    spdlog::info("  pipe: {}", kq::pipeName);

    asio::io_context io;
    asio::ip::tcp::acceptor acceptor(io,
        asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));

    while (true) {
        running = true;

        HANDLE pipe = createPipe();
        if (pipe == INVALID_HANDLE_VALUE)
            return 1;

        spdlog::info("Waiting for plugin to connect to pipe...");
        OVERLAPPED connectOv{};
        connectOv.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &connectOv);
        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                WaitForSingleObject(connectOv.hEvent, INFINITE);
            } else if (err != ERROR_PIPE_CONNECTED) {
                spdlog::error("ConnectNamedPipe failed ({})", err);
                CloseHandle(connectOv.hEvent);
                CloseHandle(pipe);
                continue;
            }
        }
        CloseHandle(connectOv.hEvent);
        spdlog::info("Plugin connected to pipe");

        spdlog::info("Waiting for TCP connection on port {}...", port);
        asio::ip::tcp::socket socket(io);
        acceptor.accept(socket);
        spdlog::info("TCP connection accepted");

        std::thread t1(pipeToTcp, pipe, std::ref(socket));
        std::thread t2(tcpToPipe, std::ref(socket), pipe);

        t1.join();
        t2.join();

        CloseHandle(pipe);
        spdlog::info("Session ended, ready for next connection");
    }
}

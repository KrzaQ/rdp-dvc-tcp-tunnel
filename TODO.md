# kq-tunnel — Implementation Plan

## 1. Server EXE (`kq-tunnel-server`)

- [x] Parse target host:port from command-line args (default `localhost:22`)
- [x] Open DVC with `WTSVirtualChannelOpenEx`
- [x] Connect to target TCP endpoint with Asio
- [x] Bridge data: DVC read → TCP write, TCP read → DVC write
- [x] Clean shutdown on DVC close or TCP disconnect

## 2. Plugin DLL (`kq-tunnel-plugin`)

The plugin is a pipe **client**, not server. The client EXE owns the pipe.

- [x] On DVC channel open: connect to `\\.\pipe\kq-tunnel` via `CreateFile`
- [x] On `OnDataReceived`: write DVC data to pipe
- [x] Read from pipe, send via `IWTSVirtualChannel::Write`
- [x] On DVC close: disconnect from pipe
- [x] Handle pipe not existing gracefully (no server EXE running = no-op)
- [x] Threading: pipe reads need overlapped I/O or a worker thread

## 3. Client EXE (`kq-tunnel-client`)

- [ ] Create named pipe `\\.\pipe\kq-tunnel` (`CreateNamedPipe`)
- [ ] Listen on `localhost:2222` with Asio
- [ ] On TCP connection: bridge TCP ↔ pipe
- [ ] On TCP disconnect: clean up, keep listening for new connections
- [ ] Clean shutdown on pipe disconnect (plugin/RDP gone)

## Notes

- Client EXE owns the named pipe to avoid conflicts with multiple RDP sessions
- Single TCP connection at a time (SSH handles multiplexing)
- Plugin instances in other RDP sessions silently do nothing (no pipe to
  connect to, or no DVC channel open)
- Plugin connects to pipe lazily on first OnDataReceived, not eagerly on
  channel open. This removes startup order constraints and handles client
  EXE restarts.

# kq-tunnel — Implementation Plan

## 1. Server EXE (`kq-tunnel-server`)

- [ ] Parse target host:port from command-line args (default `localhost:22`)
- [ ] Open DVC with `WTSVirtualChannelOpenEx`
- [ ] Connect to target TCP endpoint with Asio
- [ ] Bridge data: DVC read → TCP write, TCP read → DVC write
- [ ] Clean shutdown on DVC close or TCP disconnect

## 2. Plugin DLL (`kq-tunnel-plugin`)

The plugin is a pipe **client**, not server. The client EXE owns the pipe.

- [ ] On DVC channel open: connect to `\\.\pipe\kq-tunnel` via `CreateFile`
- [ ] On `OnDataReceived`: write DVC data to pipe
- [ ] Read from pipe, send via `IWTSVirtualChannel::Write`
- [ ] On DVC close: disconnect from pipe
- [ ] Handle pipe not existing gracefully (no server EXE running = no-op)
- [ ] Threading: pipe reads need overlapped I/O or a worker thread

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

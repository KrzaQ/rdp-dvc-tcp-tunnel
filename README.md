# kq-tunnel

Tunnels a single TCP connection over an RDP Dynamic Virtual Channel (DVC).
Designed to forward SSH through an RDP session to a host behind a firewall.

## Architecture

Forward mode (default -- local service exposed to RDP host):
```
[local machine]                              [RDP host]

ssh localhost:2222
    |
    v
kq-tunnel-client.exe                    kq-tunnel-server.exe
  (TCP listener)                          (connects to target)
    |                                         |
    v                                         v
named pipe <-- kq-tunnel-plugin.dll ---DVC--> target:22
               (loaded by mstsc/RDCMan)
```

Reverse mode (RDP-host service exposed locally):
```
[local machine]                              [RDP host]

kq-tunnel-client.exe                    kq-tunnel-server.exe
  (connects to host:port)                 (TCP listener)
    |                                         ^
    v                                         |
named pipe <-- kq-tunnel-plugin.dll ---DVC--> incoming TCP
               (loaded by mstsc/RDCMan)
```

Three components:

- **kq-tunnel-plugin.dll** -- COM DVC plugin, loaded by the RDP client
- **kq-tunnel-client.exe** -- runs on your local machine, bridges TCP to the named pipe
- **kq-tunnel-server.exe** -- runs on the remote RDP host, bridges DVC to TCP

## Building

Cross-compiled from Linux using MinGW-w64.

Requirements: `mingw-w64-gcc`, `conan` (2.x), `cmake`, `ninja`.

```sh
make
```

Binaries are in `build/Windows/bin/`.

## Installation

### 1. Plugin registration (local machine, one-time)

Copy `kq-tunnel-plugin.dll` into the `install/` directory (next to the
scripts), then run:

```
install\install.bat
```

This registers the COM class and adds the RDP client AddIn entry under
`HKCU` (no admin required). Restart your RDP client afterwards.

### 2. Server (remote machine)

Copy `kq-tunnel-server.exe` to the remote RDP host. No installation needed,
just run it.

## Usage

Both binaries accept `listen` and `connect` subcommands. The defaults
match the forward-tunnel case, so bare invocations work as before.

```
kq-tunnel-client [listen] [port]            # default: listen on 2222
kq-tunnel-client connect <host> [port]

kq-tunnel-server [connect] [host] [port]    # default: connect to localhost:22
kq-tunnel-server listen [port]
```

### Forward tunnel (SSH through RDP)

1. Start the client on your local machine:
   ```
   kq-tunnel-client.exe
   ```

2. Connect to the remote machine via RDP (mstsc or RDCMan). The plugin
   loads automatically.

3. In the RDP session, start the server:
   ```
   kq-tunnel-server.exe
   ```

4. SSH through the tunnel:
   ```
   ssh -p 2222 localhost
   ```

### Reverse tunnel (expose RDP-host port locally)

1. Start the client in connect mode:
   ```
   kq-tunnel-client.exe connect localhost 8080
   ```

2. Connect via RDP as usual.

3. In the RDP session, start the server in listen mode:
   ```
   kq-tunnel-server.exe listen 9090
   ```

4. Connections to port 9090 on the RDP host are forwarded to
   localhost:8080 on your local machine.

### Notes

Startup order is flexible -- the plugin connects to the named pipe lazily
when data first flows through the DVC. The only requirement is that the
client EXE must be running when the server sends its first data.

## Uninstall

```
install\uninstall.bat
```

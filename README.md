# kq-tunnel

Tunnels a single TCP connection over an RDP Dynamic Virtual Channel (DVC).
Designed to forward SSH through an RDP session to a host behind a firewall.

## Architecture

```
[local machine]                              [RDP host]

ssh localhost:2222
    |
    v
kq-tunnel-client.exe                    kq-tunnel-server.exe
  (TCP listener)                          (DVC <-> TCP bridge)
    |                                         |
    v                                         v
named pipe <-- kq-tunnel-plugin.dll ---DVC--> target:22
               (loaded by mstsc/RDCMan)
```

Three components:

- **kq-tunnel-plugin.dll** -- COM DVC plugin, loaded by the RDP client
- **kq-tunnel-client.exe** -- runs on your local machine, bridges TCP to the named pipe
- **kq-tunnel-server.exe** -- runs on the remote RDP host, bridges DVC to a TCP target

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

1. Start the client on your local machine:
   ```
   kq-tunnel-client.exe [port]
   ```
   Default port is 2222.

2. Connect to the remote machine via RDP (mstsc or RDCMan). The plugin
   loads automatically.

3. In the RDP session, start the server:
   ```
   kq-tunnel-server.exe [host] [port]
   ```
   Default target is `localhost:22`.

4. SSH through the tunnel:
   ```
   ssh -p 2222 localhost
   ```

Startup order is flexible -- the plugin connects to the named pipe lazily
when data first flows through the DVC. The only requirement is that the
client EXE must be running when the server sends its first data.

## Uninstall

```
install\uninstall.bat
```

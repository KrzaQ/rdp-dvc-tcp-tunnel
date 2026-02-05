# kq-tunnel

A Dynamic Virtual Channel (DVC) plugin that tunnels a single TCP connection
over an RDP session. Designed to forward SSH through an RDP connection to a
host behind a firewall.

## Architecture

Three build targets:

- **kq-tunnel-plugin** (DLL) — thin COM shim loaded by mstsc/RDCMan, bridges
  DVC to a named pipe
- **kq-tunnel-client** (EXE) — runs locally, talks to the plugin via named pipe;
  can listen on a TCP port (forward mode) or connect out (reverse mode)
- **kq-tunnel-server** (EXE) — runs in the RDP session, bridges DVC to TCP;
  can connect to a target (forward mode) or listen for incoming connections
  (reverse mode)

Data flow (forward — default):
```
ssh localhost:2222 → client.exe → named pipe → plugin.dll ←DVC→ server.exe → target:22
```

Data flow (reverse):
```
client.exe → host:port   ...   plugin.dll ←DVC→ server.exe ← incoming TCP
```

### CLI

```
kq-tunnel-client [listen] [port]            # forward mode (default)
kq-tunnel-client connect <host> [port]      # reverse mode

kq-tunnel-server [connect] [host] [port]    # forward mode (default)
kq-tunnel-server listen [port]              # reverse mode
```

## Build

Cross-compiled from Linux using MinGW-w64. Targets Windows x86_64.

```sh
make          # conan install + cmake configure + build
```

Conan profiles: host=`mingw64`, build=`default`.

## Code style

- Classes: PascalCase (`ChannelBridge`, `TcpListener`)
- Functions/methods: camelCase (`openChannel()`, `startListening()`)
- Private members: trailing underscore (`socket_`, `buffer_`)
- Type aliases: lowercase (`using u8 = std::uint8_t`)
- Constants: UPPER_SNAKE_CASE
- Namespaces: lowercase (`namespace kq`)
- Headers: `#pragma once`, include order: stdlib → third-party → project
- Use fmt/spdlog for formatting and logging
- Modern C++: `std::optional`, `std::unique_ptr`, `std::span`, etc.
- No exceptions in COM code paths (return HRESULTs)

## CMake conventions

- C++ standard set per-target, not globally:
  ```cmake
  target_compile_features(target PUBLIC cxx_std_26)
  set_target_properties(target PROPERTIES CXX_EXTENSIONS OFF)
  ```
- Dependencies consumed via `find_package()` (Conan CMakeDeps generator)
- One `CMakeLists.txt` per subdirectory under `src/`

## Project structure

```
CMakeLists.txt
conanfile.py
Makefile
src/
  common/       # shared protocol definitions, framing
  plugin/       # COM DVC client plugin (DLL)
  client/       # local TCP listener (EXE)
  server/       # remote DVC-to-TCP bridge (EXE)
```

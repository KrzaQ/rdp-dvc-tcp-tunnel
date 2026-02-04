add_executable(kq-tunnel-server
    main.cpp
)

target_compile_features(kq-tunnel-server PRIVATE cxx_std_26)
set_target_properties(kq-tunnel-server PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME "kq-tunnel-server"
)

target_link_libraries(kq-tunnel-server PRIVATE
    kq-tunnel-common
    asio::asio
    spdlog::spdlog
    ws2_32
    wtsapi32
)

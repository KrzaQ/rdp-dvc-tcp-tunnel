add_executable(kq-tunnel-client
    main.cpp
)

target_compile_features(kq-tunnel-client PRIVATE cxx_std_26)
set_target_properties(kq-tunnel-client PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME "kq-tunnel-client"
)

target_link_libraries(kq-tunnel-client PRIVATE
    kq-tunnel-common
    asio::asio
    spdlog::spdlog
    ws2_32
)

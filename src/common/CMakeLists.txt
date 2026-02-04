add_library(kq-tunnel-common INTERFACE)

target_include_directories(kq-tunnel-common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(kq-tunnel-common INTERFACE cxx_std_26)

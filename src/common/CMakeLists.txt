add_library(kq-tunnel-common INTERFACE)

target_include_directories(kq-tunnel-common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(kq-tunnel-common INTERFACE cxx_std_26)

target_compile_definitions(kq-tunnel-common INTERFACE _WIN32_WINNT=0x0601)

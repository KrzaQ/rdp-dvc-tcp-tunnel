add_library(kq-tunnel-plugin SHARED
    dllmain.cpp
    plugin.cpp
)

target_compile_features(kq-tunnel-plugin PRIVATE cxx_std_26)
set_target_properties(kq-tunnel-plugin PROPERTIES
    CXX_EXTENSIONS OFF
    PREFIX ""
    OUTPUT_NAME "kq-tunnel-plugin"
)

target_link_libraries(kq-tunnel-plugin PRIVATE
    kq-tunnel-common
    ole32
    oleaut32
    uuid
)

target_sources(kq-tunnel-plugin PRIVATE plugin.def)

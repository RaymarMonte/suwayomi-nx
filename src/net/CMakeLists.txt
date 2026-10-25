# net: HttpClient interface + curl implementation, network thread, cancellation (ARCHITECTURE.md "net"). curl comes in WP-05.
add_library(snx_net STATIC
    Net.cpp
)
target_link_libraries(snx_net PUBLIC snx_core)
snx_target_defaults(snx_net)

# api: SuwayomiClient, typed async calls on top of net + core, auth (none/basic/JWT) (ARCHITECTURE.md "api").
add_library(snx_api STATIC
    Api.cpp
)
target_link_libraries(snx_api PUBLIC snx_core snx_net)
snx_target_defaults(snx_api)

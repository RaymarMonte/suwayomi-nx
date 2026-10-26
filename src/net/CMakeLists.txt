# net: HttpClient interface, the libcurl implementation (one network thread, D5), a fake for unit tests (WP-05).
add_library(snx_net STATIC
    CurlHttpClient.cpp
    FakeHttpClient.cpp
    HttpClient.cpp
)
find_package(Threads REQUIRED)
if (PLATFORM_SWITCH)
    # devkitPro's switch-curl (7.69, TLS through libnx) is a static library that needs zlib.
    find_path(SNX_CURL_INCLUDE curl/curl.h REQUIRED)
    find_library(SNX_CURL_LIB curl REQUIRED)
    find_library(SNX_ZLIB_LIB z REQUIRED)
    target_include_directories(snx_net PUBLIC ${SNX_CURL_INCLUDE})
    target_compile_definitions(snx_net PUBLIC CURL_STATICLIB)
    target_link_libraries(snx_net PUBLIC ${SNX_CURL_LIB} ${SNX_ZLIB_LIB})
else ()
    find_package(CURL REQUIRED)  # Ubuntu: libcurl4-openssl-dev
    target_link_libraries(snx_net PUBLIC CURL::libcurl)
endif ()
target_link_libraries(snx_net PUBLIC snx_core Threads::Threads)
snx_target_defaults(snx_net)

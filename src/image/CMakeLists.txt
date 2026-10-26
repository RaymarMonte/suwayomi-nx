# image: decode (JPEG/PNG/WebP/GIF), downscale, tiles, memory + disk caches, the loader (ARCHITECTURE.md "image", WP-07, D39).

# libwebp 1.6.0 from the pinned submodule library/libwebp, on both platforms (D39: devkitPro's Switch package is 1.0.2, which
# has the CVE-2023-4863 heap overflow). Only its decoder library gets built: the tools are switched off, and EXCLUDE_FROM_ALL
# leaves out every target nothing links.
set(SNX_LIBWEBP_DIR ${PROJECT_SOURCE_DIR}/library/libwebp)
if (NOT EXISTS ${SNX_LIBWEBP_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "libwebp is missing. Run: git submodule update --init library/libwebp")
endif ()
foreach (tool ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO LIBWEBPMUX WEBPMUX EXTRAS)
    set(WEBP_BUILD_${tool} OFF)
endforeach ()
set(WEBP_USE_THREAD OFF)  # we decode with use_threads = 0, on our own worker threads
add_subdirectory(${SNX_LIBWEBP_DIR} ${CMAKE_BINARY_DIR}/libwebp EXCLUDE_FROM_ALL)

add_library(snx_image STATIC
    Caches.cpp
    Decode.cpp
    DecodedImage.cpp
    DiskCache.cpp
    GifDecoder.cpp
    ImageLoader.cpp
    JpegDecoder.cpp
    PngDecoder.cpp
    Resize.cpp
    WebpDecoder.cpp
    WorkerPool.cpp
)
find_package(Threads REQUIRED)
if (PLATFORM_SWITCH)
    # devkitPro's switch-libjpeg-turbo and switch-libpng (static; libpng needs zlib).
    find_path(SNX_JPEG_INCLUDE jpeglib.h REQUIRED)
    find_library(SNX_JPEG_LIB jpeg REQUIRED)
    find_path(SNX_PNG_INCLUDE png.h REQUIRED)
    find_library(SNX_PNG_LIB png16 REQUIRED)
    find_library(SNX_ZLIB_LIB z REQUIRED)
    target_include_directories(snx_image PRIVATE ${SNX_JPEG_INCLUDE} ${SNX_PNG_INCLUDE})
    target_link_libraries(snx_image PRIVATE ${SNX_JPEG_LIB} ${SNX_PNG_LIB} ${SNX_ZLIB_LIB})
else ()
    find_package(JPEG REQUIRED)  # Ubuntu: libjpeg-turbo8-dev
    find_package(PNG REQUIRED)   # Ubuntu: libpng-dev
    target_link_libraries(snx_image PRIVATE JPEG::JPEG PNG::PNG)
endif ()
target_include_directories(snx_image PRIVATE ${SNX_LIBWEBP_DIR}/src)
target_link_libraries(snx_image PRIVATE webpdecoder)
target_link_libraries(snx_image PUBLIC snx_core snx_net snx_platform Threads::Threads)
snx_target_defaults(snx_image)

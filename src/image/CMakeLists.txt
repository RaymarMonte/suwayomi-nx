# image: decode (jpeg/png/webp), downscale, tiling, memory + SD disk caches (ARCHITECTURE.md "image"). Decoders come in WP-07.
add_library(snx_image STATIC
    Image.cpp
)
target_link_libraries(snx_image PUBLIC snx_core)
snx_target_defaults(snx_image)

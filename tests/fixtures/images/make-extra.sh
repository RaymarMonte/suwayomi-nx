#!/usr/bin/env bash
# Test images for the image module's unit tests and fuzz seeds (WP-07). Run in WSL from anywhere; needs ImageMagick 6 with
# JPEG/PNG/GIF/WebP (Ubuntu's imagemagick package). The committed files are what the tests use; this script only says how they
# were made. The first seven files are copies of tools/gen-fixtures output (D31, the test server's fixture manga):
#   alpha-p1.jpg       Alpha ch 1 p 1: baseline JPEG 1000x1500
#   beta-p1.png        Beta RTL ch 1 p 1: RGB PNG 1000x1500
#   beta-spread.png    Beta RTL ch 1 p 4: the double-page spread, 2000x1500
#   gamma-strip.webp   Gamma Webtoon ch 1 p 1: lossy WebP strip 800x12000
#   broken-*.jpg       Broken ch 1 p 1-3: corrupt, empty, 1x1
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

# A progressive JPEG (libjpeg keeps all its coefficients while decoding) and a small CMYK one (Adobe-inverted, like Photoshop's).
convert alpha-p1.jpg -strip -interlace JPEG -quality 80 alpha-progressive.jpg
convert alpha-p1.jpg -strip -resize 200x300 -colorspace CMYK -quality 85 alpha-cmyk.jpg
# GIFs: a plain one, an interlaced one, a transparent one, and a two-frame animation (red, then blue: the first frame counts).
convert alpha-p1.jpg -strip -resize 100x150 -colors 64 small.gif
convert alpha-p1.jpg -strip -resize 100x150 -colors 64 -interlace GIF small-interlaced.gif
convert -size 64x48 xc:none -fill red -draw "rectangle 8,8 40,30" transparent.gif
convert -delay 10 -size 32x32 xc:red xc:blue -loop 0 animated.gif
# A lossless WebP with half-transparent pixels, and a 16-bit grey PNG with alpha.
convert -size 300x200 gradient:red-blue -alpha set -channel A -evaluate set 50% +channel -define webp:lossless=true lossless.webp
convert -size 40x30 gradient:white-black -alpha set -channel A -evaluate set 75% +channel -depth 16 grey16.png
ls -l

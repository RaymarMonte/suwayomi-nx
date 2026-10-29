#pragma once

#include <string>

// Text that fits a number of lines (WP-09, shared since WP-10): borealis labels only shorten single lines with "…", so
// cover titles (two lines) and the manga description (a few lines until opened) are cut here. Main thread only (it measures
// with the window's NanoVG context).
namespace snx::ui {

struct FittedText {
    std::string text;  // ends in "…" when cut
    bool cut = false;  // the whole text needs more lines
};

// `text` broken into lines of `width` at this font, size and line height; at most `maxLines` of them.
FittedText fitLines(const std::string& text, float width, int font, float fontSize, float lineHeight, int maxLines);

}  // namespace snx::ui

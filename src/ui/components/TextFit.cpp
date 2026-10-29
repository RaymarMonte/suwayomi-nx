#include "ui/components/TextFit.hpp"

#include <borealis.hpp>
#include <vector>

namespace snx::ui {

FittedText fitLines(const std::string& text, float width, int font, float fontSize, float lineHeight, int maxLines) {
    FittedText fitted{text, false};
    if (maxLines < 1 || text.empty()) {
        return fitted;
    }
    NVGcontext* vg = brls::Application::getNVGContext();
    nvgSave(vg);
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, fontSize);
    nvgTextLineHeight(vg, lineHeight);
    std::vector<NVGtextRow> rows(static_cast<std::size_t>(maxLines) + 1);
    const int count = nvgTextBreakLines(vg, text.c_str(), nullptr, width, rows.data(), maxLines + 1);
    if (count > maxLines) {
        const char* const ellipsis = "…";
        const NVGtextRow& last = rows[static_cast<std::size_t>(maxLines) - 1];
        std::string tail(last.start, last.end);
        // Drop whole UTF-8 characters from the end until "…" fits too.
        while (!tail.empty() && nvgTextBounds(vg, 0, 0, (tail + ellipsis).c_str(), nullptr, nullptr) > width) {
            do {
                tail.pop_back();
            } while (!tail.empty() && (static_cast<unsigned char>(tail.back()) & 0xC0) == 0x80);
        }
        fitted.text = std::string(text.c_str(), last.start) + tail + ellipsis;
        fitted.cut = true;
    }
    nvgRestore(vg);
    return fitted;
}

}  // namespace snx::ui

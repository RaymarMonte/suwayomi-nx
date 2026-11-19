#pragma once

#include <cstddef>
#include <string>
#include <vector>

// The Languages checklist (WP-14b, D56): every language that has a source (Local included), ticked = listed on the Browse
// tab and searched by Search all. Without borealis or the network, so it is unit-tested (test_ui_browse.cpp);
// LanguagesActivity shows it, and its Done saves hidden() (AppConfig::browse.hiddenLanguages).
namespace snx::ui {

class LanguagesModel {
  public:
    struct Row {
        std::string code;  // the server's code ("en", Local's "localsourcelang")
        std::string name;  // core::languageName
        bool shown = true;
    };

    // languages: SourcesModel::languages() (the list's order). hidden: what config.json has; a hidden code that no source
    // has now (an uninstalled extension's) stays hidden.
    LanguagesModel(const std::vector<std::string>& languages, std::vector<std::string> hidden);

    const std::vector<Row>& rows() const { return rows_; }
    // Ticks or unticks row `index`. False (nothing changes) for a wrong index, and for unticking the last ticked row: at
    // least one language stays shown.
    bool toggle(std::size_t index);
    std::size_t shownCount() const;

    // The codes to keep hidden: the unticked rows and the hidden codes the list doesn't have, without repeats.
    std::vector<std::string> hidden() const;
    bool changed() const { return hidden() != initial_; }

  private:
    std::vector<Row> rows_;
    std::vector<std::string> kept_;     // hidden codes no source has now
    std::vector<std::string> initial_;  // hidden() at the start
};

}  // namespace snx::ui

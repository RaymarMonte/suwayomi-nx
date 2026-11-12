#include "ui/reader/StripModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <utility>

namespace snx::ui {

namespace {

// The page failed because the server didn't answer (not because it answered with something broken): worth asking again
// once the server may be back. The same rule as the page reader's.
bool failedForNetwork(const ReaderPage& page) {
    return page.state == ReaderPage::State::Failed &&
           (page.error.kind == image::ImageErrorKind::Network || page.error.kind == image::ImageErrorKind::Timeout);
}

bool failedForNetwork(const api::ApiError& error) {
    return error.kind == api::ApiErrorKind::Offline || error.kind == api::ApiErrorKind::Timeout;
}

}  // namespace

StripOptions stripOptionsFrom(const core::RuntimeProfile& profile, const core::ReaderSettings& reader) {
    StripOptions options;
    options.width = reader.stripWidth;
    options.screensBehind = profile.stripScreensBehind;
    options.screensAhead = profile.stripScreensAhead;
    options.progressSyncMs = profile.progressSyncMs;
    options.progressRetryMaxMs = profile.progressRetryMaxMs;
    options.format = reader.serverConvertsToJpeg ? api::PageFormat::Jpeg : api::PageFormat::AsStored;
    return options;
}

StripModel::StripModel(api::SuwayomiClient& client, image::ImageLoader& images, core::Manga manga,
                       std::vector<core::Chapter> chapters, StripOptions options)
    : client_(client), images_(images), manga_(std::move(manga)), chapters_(std::move(chapters)), options_(std::move(options)) {
    std::stable_sort(chapters_.begin(), chapters_.end(),
                     [](const core::Chapter& a, const core::Chapter& b) { return a.sourceOrder < b.sourceOrder; });
    layout_.setViewHeight(options_.units.height);
    saver_ = std::make_shared<ProgressSaver>(client_, options_.clock, options_.onSaved);
    saver_->toModel = [this](const core::ReadProgress& progress) { onSaved(progress); };
}

StripModel::~StripModel() {
    listCancel_.cancel();
    above_.cancel.cancel();
    below_.cancel.cancel();
    for (Part& part : parts_) {
        for (Slot& slot : part.slots) {
            slot.cancel.cancel();
        }
    }
    saver_->toModel = nullptr;
    if (!saver_->closing) {
        close(nullptr);
    }
}

void StripModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void StripModel::changed() {
    moved_ = false;
    if (listener_) {
        listener_();
    }
}

// --- Looking things up ---

int StripModel::chapterIndex(int chapterId) const {
    for (std::size_t i = 0; i < chapters_.size(); ++i) {
        if (chapters_[i].id == chapterId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

StripModel::Part* StripModel::partOf(int chapterId) {
    for (Part& part : parts_) {
        if (part.chapter.id == chapterId) {
            return &part;
        }
    }
    return nullptr;
}

const StripModel::Part* StripModel::partOf(int chapterId) const {
    for (const Part& part : parts_) {
        if (part.chapter.id == chapterId) {
            return &part;
        }
    }
    return nullptr;
}

const StripModel::Slot* StripModel::slotAt(int item) const {
    const core::StripItem& it = layout_.item(item);
    if (item < 0 || item >= layout_.size() || it.kind != core::StripItemKind::Page) {
        return nullptr;
    }
    const Part* part = partOf(it.chapterId);
    if (part == nullptr || it.page < 0 || it.page >= static_cast<int>(part->slots.size())) {
        return nullptr;
    }
    return &part->slots[static_cast<std::size_t>(it.page)];
}

const ReaderPage& StripModel::pageAt(int item) const {
    static const ReaderPage kNone;
    const Slot* slot = slotAt(item);
    return slot != nullptr ? slot->page : kNone;
}

int StripModel::firstItem(std::size_t part) const {
    int item = 1;  // the band above the first chapter
    for (std::size_t k = 0; k < part && k < parts_.size(); ++k) {
        item += static_cast<int>(parts_[k].slots.size()) + 1;  // its pages and the band below them
    }
    return item;
}

int StripModel::bandIndexOf(int item) const {
    int position = 0;
    for (std::size_t k = 0; k <= parts_.size(); ++k) {
        if (position == item) {
            return static_cast<int>(k);
        }
        if (k < parts_.size()) {
            position += 1 + static_cast<int>(parts_[k].slots.size());
        }
    }
    return -1;
}

StripBand StripModel::band(int item) const {
    StripBand band;
    const int k = bandIndexOf(item);
    if (k < 0 || parts_.empty()) {
        return band;
    }
    const int n = static_cast<int>(parts_.size());
    if (k > 0) {
        band.above = parts_[static_cast<std::size_t>(k - 1)].chapter.id;
    } else if (const int index = chapterIndex(parts_.front().chapter.id); index > 0) {
        band.above = chapters_[static_cast<std::size_t>(index - 1)].id;
    }
    if (k < n) {
        band.below = parts_[static_cast<std::size_t>(k)].chapter.id;
    } else if (const int index = chapterIndex(parts_.back().chapter.id);
               index >= 0 && index + 1 < static_cast<int>(chapters_.size())) {
        band.below = chapters_[static_cast<std::size_t>(index + 1)].id;
    }
    const Edge* edge = k == 0 ? &above_ : k == n ? &below_ : nullptr;
    const int waitsFor = k == 0 ? band.above : band.below;
    if (edge != nullptr && edge->chapterId != 0 && edge->chapterId == waitsFor) {
        if (edge->loading) {
            band.edge = StripBand::Edge::Loading;
        } else if (edge->failed) {
            band.edge = StripBand::Edge::Failed;
            band.error = edge->error;
        }
    }
    return band;
}

const core::Chapter* StripModel::chapterById(int id) const {
    if (const Part* part = partOf(id)) {
        return &part->chapter;
    }
    const int index = chapterIndex(id);
    return index >= 0 ? &chapters_[static_cast<std::size_t>(index)] : nullptr;
}

std::vector<int> StripModel::chaptersHeld() const {
    std::vector<int> ids;
    for (const Part& part : parts_) {
        ids.push_back(part.chapter.id);
    }
    return ids;
}

int StripModel::pagesHeld() const {
    int count = 0;
    for (const Part& part : parts_) {
        for (const Slot& slot : part.slots) {
            count += slot.page.state == ReaderPage::State::Ready ? 1 : 0;
        }
    }
    return count;
}

const core::Chapter& StripModel::chapter() const {
    static const core::Chapter kNone;
    const core::Chapter* chapter = chapterById(readingId_);
    return chapter != nullptr ? *chapter : kNone;
}

int StripModel::pageCount() const {
    const Part* part = partOf(readingId_);
    return part != nullptr ? static_cast<int>(part->slots.size()) : 0;
}

bool StripModel::chapterRead() const {
    return sync_ ? sync_->read() : chapter().isRead;
}

double StripModel::stripWidth() const {
    return core::stripWidth(options_.width, options_.orientation, static_cast<float>(options_.units.width));
}

core::ImageSize StripModel::decodeBox() const {
    return core::stripDecodeBox(options_.width, options_.orientation, options_.screen);
}

std::optional<double> StripModel::noteSpot(const core::Chapter& chapter) const {
    const auto note = core::parseStripSpot(chapter.stripSpotNote);
    if (note && note->page == chapter.lastPageRead) {
        return note->share;
    }
    return std::nullopt;
}

// --- Heights ---

core::StripItem StripModel::bandItem() const {
    core::StripItem band;
    band.kind = core::StripItemKind::Band;
    band.height = core::stripBandHeight(options_.units.height);
    return band;
}

double StripModel::heightFor(const Part& part, const Slot& slot) const {
    if (slot.page.state == ReaderPage::State::Failed) {
        return options_.units.height;  // the failed band: one screen, with the reason and the retry
    }
    if (slot.source) {
        return core::stripPageHeight(*slot.source, stripWidth());
    }
    return core::estimatedPageHeight(part.shape, stripWidth());
}

std::vector<core::StripItem> StripModel::itemsFor(const Part& part) const {
    std::vector<core::StripItem> items;
    for (std::size_t i = 0; i < part.slots.size(); ++i) {
        core::StripItem item;
        item.kind = core::StripItemKind::Page;
        item.chapterId = part.chapter.id;
        item.page = static_cast<int>(i);
        item.height = heightFor(part, part.slots[i]);
        items.push_back(item);
    }
    return items;
}

void StripModel::refreshHeight(Part& part, int index) {
    const int item = layout_.find(part.chapter.id, index);
    if (item < 0) {
        return;
    }
    const double height = heightFor(part, part.slots[static_cast<std::size_t>(index)]);
    if (layout_.item(item).height != height) {
        layout_.setHeight(item, height);
    }
}

void StripModel::relayout() {
    const core::StripItem band = bandItem();
    for (int item = 0; item < layout_.size(); ++item) {
        const core::StripItem& it = layout_.item(item);
        if (it.kind == core::StripItemKind::Band) {
            if (it.height != band.height) {
                layout_.setHeight(item, band.height);
            }
        } else if (Part* part = partOf(it.chapterId)) {
            refreshHeight(*part, it.page);
        }
    }
}

// --- Opening ---

void StripModel::open(int chapterId, int page, Open how) {
    if (opened_) {
        return;
    }
    opened_ = true;
    load(chapterId, page, how);
}

void StripModel::retry() {
    if (state_ == State::Failed) {
        load(openChapter_, openPage_, openHow_);
    }
}

void StripModel::load(int chapterId, int page, Open how) {
    openChapter_ = chapterId;
    openPage_ = page;
    openHow_ = how;
    readingId_ = chapterId;
    state_ = State::Loading;
    error_ = api::ApiError{};
    listCancel_ = net::CancelToken();
    const net::CancelToken token = listCancel_;
    client_.pages(chapterId, options_.format, token)
        .then([this, token, page, how](const api::ApiResult<core::PageList>& result) {
            if (token.cancelled()) {
                return;
            }
            onOpened(page, how, result);
        });
}

StripModel::Part StripModel::makePart(int chapterId, const core::PageList& list) const {
    Part part;
    const int index = chapterIndex(chapterId);
    if (index >= 0) {
        part.chapter = chapters_[static_cast<std::size_t>(index)];
    } else {
        part.chapter.id = chapterId;
        part.chapter.mangaId = manga_.id;
    }
    // What the server says about the chapter now (the list's copy may be older).
    part.chapter.isRead = list.chapter.isRead;
    part.chapter.lastPageRead = list.chapter.lastPageRead;
    part.chapter.pageCount = static_cast<int>(list.pages.size());
    part.chapter.stripSpotNote = list.chapter.stripSpotNote;
    part.slots.resize(list.pages.size());
    for (std::size_t i = 0; i < list.pages.size(); ++i) {
        if (!list.pages[i].empty()) {
            part.slots[i].url = client_.absoluteUrl(list.pages[i]);
        }
    }
    return part;
}

void StripModel::onOpened(int page, Open how, const api::ApiResult<core::PageList>& result) {
    if (!result.ok()) {
        state_ = State::Failed;
        error_ = result.error();
        changed();
        return;
    }
    Part part = makePart(openChapter_, result.value());
    if (const int index = chapterIndex(part.chapter.id); index >= 0) {
        chapters_[static_cast<std::size_t>(index)] = part.chapter;
    }
    const int count = static_cast<int>(part.slots.size());
    const int at = count == 0 ? 0 : std::clamp(page, 0, count - 1);
    std::optional<core::StripSpot> note;
    if (how == Open::Resume && !part.chapter.isRead) {
        note = core::parseStripSpot(part.chapter.stripSpotNote);
        if (note && note->page != at) {
            note.reset();  // another app moved on: the page's top instead
        }
    }

    parts_.clear();
    layout_.clear();
    std::vector<core::StripItem> items{bandItem()};
    for (const core::StripItem& item : itemsFor(part)) {
        items.push_back(item);
    }
    items.push_back(bandItem());
    layout_.insert(0, std::move(items));
    parts_.push_back(std::move(part));
    readingId_ = parts_.front().chapter.id;
    startReading(parts_.front());

    page_ = count == 0 ? -1 : at;
    spot_ = note ? note->share : 0;
    if (count > 0) {
        const int item = layout_.find(readingId_, at);
        if (note) {
            layout_.pin(core::StripPlace{item, note->share}, layout_.viewHeight() / 2);  // the spot under the middle line
        } else {
            layout_.pin(core::StripPlace{item, 0}, 0);  // the page's top at the screen's top
        }
    }
    state_ = State::Ready;
    update();
    changed();
}

void StripModel::startReading(Part& part) {
    sync_ = std::make_shared<core::ProgressSync>(part.chapter.id, part.chapter.lastPageRead, part.chapter.isRead,
                                                 options_.progressSyncMs, options_.progressRetryMaxMs, options_.clock(),
                                                 noteSpot(part.chapter));
}

// --- Following the screen ---

void StripModel::update() {
    if (state_ != State::Ready) {
        return;
    }
    if (updating_) {
        again_ = true;  // a change made while update() runs (a synchronous answer): one more round
        return;
    }
    updating_ = true;
    int rounds = 0;
    do {
        again_ = false;
        followMiddle();
        updateWindow();
        const core::StripEnds ends = core::stripWindowEnds(layout_, options_.screensBehind, options_.screensAhead);
        if (ends.top) {
            requestEdge(false);
        }
        if (ends.bottom) {
            requestEdge(true);
        }
    } while (again_ && ++rounds < 8);  // each round can only join or settle something, so this ends quickly
    updating_ = false;
}

bool StripModel::partRead(const Part& part) const {
    if (part.slots.empty()) {
        return false;
    }
    const Slot& last = part.slots.back();
    if (!last.source && last.page.state != ReaderPage::State::Failed) {
        return false;  // its height is a guess: its bottom isn't really known to be on screen
    }
    const int item = layout_.find(part.chapter.id, static_cast<int>(part.slots.size()) - 1);
    return item >= 0 && layout_.itemBottom(item) <= layout_.bottom() + 0.5;
}

bool StripModel::followMiddle() {
    const core::StripPlace place = layout_.middle();
    bool moved = false;
    if (place.item >= 0 && layout_.item(place.item).kind == core::StripItemKind::Page) {
        const core::StripItem& item = layout_.item(place.item);
        if (item.chapterId != readingId_) {
            // The middle line moved into another chapter: everything of the old one is saved at once (D44's "leaving").
            if (const Part* old = partOf(readingId_); old != nullptr && sync_ && page_ >= 0) {
                sync_->shownAt(page_, spot_, partRead(*old));
            }
            ProgressSaver::leave(saver_, sync_);
            sync_.reset();
            readingId_ = item.chapterId;
            if (Part* part = partOf(readingId_)) {
                startReading(*part);
            }
            moved = true;
        }
        moved = moved || item.page != page_;
        page_ = item.page;
        spot_ = place.share;
    }
    // A band under the middle line changes nothing, but the chapter's end may have come on screen.
    if (sync_ && page_ >= 0) {
        if (const Part* part = partOf(readingId_)) {
            sync_->shownAt(page_, spot_, partRead(*part));
        }
    }
    moved_ = moved_ || moved;
    return moved;
}

void StripModel::updateWindow() {
    const std::vector<int> wanted = core::stripPrefetchOrder(layout_, options_.screensBehind, options_.screensAhead);
    std::set<std::pair<int, int>> window;  // (chapter, page)
    for (const int item : wanted) {
        const core::StripItem& it = layout_.item(item);
        window.insert({it.chapterId, it.page});
    }
    for (Part& part : parts_) {
        for (std::size_t i = 0; i < part.slots.size(); ++i) {
            Slot& slot = part.slots[i];
            const bool keep = window.count({part.chapter.id, static_cast<int>(i)}) > 0;
            // Let go of what left the window (the memory cache keeps it while its budget allows). A failed page keeps its
            // band: it holds nothing, and its height doesn't jump while scrolling past.
            if (!keep && (slot.page.state == ReaderPage::State::Ready || slot.page.state == ReaderPage::State::Loading)) {
                resetSlot(slot);
            }
        }
    }
    const auto [first, last] = layout_.overlapping(layout_.top(), layout_.bottom());
    for (const int item : wanted) {
        const core::StripItem& it = layout_.item(item);
        Part* part = partOf(it.chapterId);
        if (part == nullptr || it.page < 0 || it.page >= static_cast<int>(part->slots.size())) {
            continue;
        }
        if (part->slots[static_cast<std::size_t>(it.page)].page.state == ReaderPage::State::Waiting) {
            const bool onScreen = item >= first && item < last;
            startLoad(*part, it.page, onScreen ? net::Priority::Reader : net::Priority::Prefetch);
        }
    }
}

void StripModel::resetSlot(Slot& slot) {
    slot.cancel.cancel();
    slot.page = ReaderPage{};  // the source size stays: the height doesn't change
}

void StripModel::startLoad(Part& part, int index, net::Priority priority) {
    Slot& slot = part.slots[static_cast<std::size_t>(index)];
    if (slot.url.empty()) {
        slot.page.state = ReaderPage::State::Failed;
        slot.page.error = image::imageError(image::ImageErrorKind::Corrupt, "the server gave no address for this page");
        refreshHeight(part, index);
        return;
    }
    const core::ImageSize box = decodeBox();
    if (image::ImagePtr hit = images_.cached(slot.url, box, image::ImageKind::Page)) {
        arrived(part, index, std::move(hit));
        return;
    }
    slot.cancel = net::CancelToken();
    slot.page.state = ReaderPage::State::Loading;
    const net::CancelToken token = slot.cancel;
    const int chapterId = part.chapter.id;
    images_.load(image::LoadRequest{slot.url, box, priority, image::ImageKind::Page}, token,
                 [this, token, chapterId, index](const image::ImageResult& result) {
                     if (token.cancelled()) {
                         return;  // left the window, or the model is gone
                     }
                     onLoaded(chapterId, index, result);
                 });
}

void StripModel::arrived(Part& part, int index, image::ImagePtr image) {
    Slot& slot = part.slots[static_cast<std::size_t>(index)];
    const core::ImageSize source = image->sourceSize();
    slot.page.state = ReaderPage::State::Ready;
    slot.page.image = std::move(image);
    slot.page.error = image::ImageError{};
    slot.source = source;
    const bool newShape = !part.shape || *part.shape != source;
    part.shape = source;
    if (newShape) {
        // The pages not known yet take the chapter's last known shape.
        for (std::size_t i = 0; i < part.slots.size(); ++i) {
            if (!part.slots[i].source) {
                refreshHeight(part, static_cast<int>(i));
            }
        }
    }
    refreshHeight(part, index);
    if (updating_) {
        again_ = true;  // a cached page found by updateWindow: the layout changed under it
    }
}

void StripModel::onLoaded(int chapterId, int index, const image::ImageResult& result) {
    Part* part = partOf(chapterId);
    if (part == nullptr || index < 0 || index >= static_cast<int>(part->slots.size())) {
        return;
    }
    Slot& slot = part->slots[static_cast<std::size_t>(index)];
    if (result.ok()) {
        arrived(*part, index, result.value());
        reloadNetworkFailures();
    } else {
        const image::ImageErrorKind kind = result.error().kind;
        const int item = layout_.find(chapterId, index);
        const auto [first, last] = layout_.overlapping(layout_.top(), layout_.bottom());
        const bool onScreen = item >= first && item < last;
        if (kind == image::ImageErrorKind::Paused || (kind == image::ImageErrorKind::Busy && !onScreen)) {
            slot.page.state = ReaderPage::State::Waiting;  // asked for again by the next update
        } else {
            slot.page.state = ReaderPage::State::Failed;
            slot.page.error = result.error();
            refreshHeight(*part, index);
        }
    }
    update();
    changed();
}

// A page arrived, so the server may answer again: the pages that failed for want of it load again (and a neighbouring
// chapter's list that failed the same way is asked for again), instead of each waiting for its own retry. Only a success
// calls this, so a server that is still away can't make it loop.
void StripModel::reloadNetworkFailures() {
    for (Part& part : parts_) {
        for (std::size_t i = 0; i < part.slots.size(); ++i) {
            if (failedForNetwork(part.slots[i].page)) {
                resetSlot(part.slots[i]);
                refreshHeight(part, static_cast<int>(i));
            }
        }
    }
    for (Edge* edge : {&above_, &below_}) {
        if (edge->failed && failedForNetwork(edge->error)) {
            *edge = Edge{};
        }
    }
}

bool StripModel::retryItem(int item) {
    if (state_ != State::Ready || item < 0 || item >= layout_.size()) {
        return false;
    }
    const core::StripItem& it = layout_.item(item);
    if (it.kind == core::StripItemKind::Page) {
        Part* part = partOf(it.chapterId);
        if (part == nullptr) {
            return false;
        }
        Slot& slot = part->slots[static_cast<std::size_t>(it.page)];
        if (slot.page.state != ReaderPage::State::Failed) {
            return false;
        }
        resetSlot(slot);
        refreshHeight(*part, it.page);
        startLoad(*part, it.page, net::Priority::Reader);
        update();
        changed();
        return true;
    }
    const int k = bandIndexOf(item);
    Edge* edge = k == 0 ? &above_ : k == static_cast<int>(parts_.size()) ? &below_ : nullptr;
    if (edge == nullptr || !edge->failed) {
        return false;
    }
    *edge = Edge{};
    update();
    changed();
    return true;
}

bool StripModel::retryNearest() {
    if (state_ != State::Ready) {
        return false;
    }
    const auto [first, last] = layout_.overlapping(layout_.top(), layout_.bottom());
    const double middle = layout_.top() + layout_.viewHeight() / 2;
    int best = -1;
    double bestDistance = 0;
    for (int item = first; item < last; ++item) {
        if (pageAt(item).state != ReaderPage::State::Failed || slotAt(item) == nullptr) {
            continue;
        }
        const double centre = (layout_.itemTop(item) + layout_.itemBottom(item)) / 2;
        const double distance = std::abs(centre - middle);
        if (best < 0 || distance < bestDistance) {
            best = item;
            bestDistance = distance;
        }
    }
    if (best >= 0) {
        return retryItem(best);
    }
    for (int item = first; item < last; ++item) {
        if (band(item).edge == StripBand::Edge::Failed) {
            return retryItem(item);
        }
    }
    return false;
}

// --- Joining the neighbours ---

void StripModel::requestEdge(bool below) {
    if (parts_.empty()) {
        return;
    }
    const Part& end = below ? parts_.back() : parts_.front();
    if (end.chapter.id != readingId_) {
        return;  // only the chapter being read gets a neighbour joined: at most one above it and one below
    }
    const int index = chapterIndex(end.chapter.id);
    const int want = index + (below ? 1 : -1);
    if (index < 0 || want < 0 || want >= static_cast<int>(chapters_.size())) {
        return;  // the strip's real start or end
    }
    const int chapterId = chapters_[static_cast<std::size_t>(want)].id;
    Edge& edge = below ? below_ : above_;
    if (edge.chapterId == chapterId && (edge.loading || edge.failed)) {
        return;  // on its way, or failed: waits for a retry
    }
    edge.cancel.cancel();
    edge = Edge{};
    edge.chapterId = chapterId;
    edge.loading = true;
    const net::CancelToken token = edge.cancel;
    client_.pages(chapterId, options_.format, token, net::Priority::Prefetch)
        .then([this, token, below, chapterId](const api::ApiResult<core::PageList>& result) {
            if (token.cancelled()) {
                return;
            }
            onEdge(below, chapterId, result);
        });
}

void StripModel::onEdge(bool below, int chapterId, const api::ApiResult<core::PageList>& result) {
    Edge& edge = below ? below_ : above_;
    if (edge.chapterId != chapterId || parts_.empty()) {
        return;
    }
    edge.loading = false;
    if (!result.ok()) {
        edge.failed = true;
        edge.error = result.error();
        changed();
        return;
    }
    // Still that end's neighbour? (The strip may have changed while the list was on its way.)
    const Part& end = below ? parts_.back() : parts_.front();
    const int index = chapterIndex(end.chapter.id);
    const int want = index + (below ? 1 : -1);
    edge = Edge{};
    if (index < 0 || want < 0 || want >= static_cast<int>(chapters_.size()) ||
        chapters_[static_cast<std::size_t>(want)].id != chapterId || partOf(chapterId) != nullptr) {
        return;
    }
    Part part = makePart(chapterId, result.value());
    chapters_[static_cast<std::size_t>(want)] = part.chapter;
    join(below, std::move(part));
}

void StripModel::join(bool below, Part part) {
    std::vector<core::StripItem> items = itemsFor(part);
    if (below) {
        items.push_back(bandItem());  // the strip's new last band; the old one is now between the two chapters
        layout_.insert(layout_.size(), std::move(items));
        parts_.push_back(std::move(part));
    } else {
        items.insert(items.begin(), bandItem());
        layout_.insert(0, std::move(items));  // above the screen: the layout's anchor keeps the view still
        parts_.insert(parts_.begin(), std::move(part));
    }
    if (parts_.size() > 3) {
        cutFarthest();
    }
    update();
    changed();
}

void StripModel::cutFarthest() {
    std::size_t reading = 0;
    for (std::size_t k = 0; k < parts_.size(); ++k) {
        if (parts_[k].chapter.id == readingId_) {
            reading = k;
        }
    }
    const bool top = reading >= parts_.size() - 1 - reading;  // the end farther from the chapter being read
    const std::size_t cut = top ? 0 : parts_.size() - 1;
    for (Slot& slot : parts_[cut].slots) {
        slot.cancel.cancel();
    }
    if (top) {
        layout_.erase(0, firstItem(1) - 1);  // its band above and its pages; the band below it is the strip's first now
        above_.cancel.cancel();
        above_ = Edge{};
    } else {
        layout_.erase(firstItem(cut), layout_.size());  // its pages and the band below them
        below_.cancel.cancel();
        below_ = Edge{};
    }
    parts_.erase(parts_.begin() + static_cast<std::ptrdiff_t>(cut));
}

// --- Scrolling and the screen ---

double StripModel::scrollBy(double dy) {
    return scrollTo(layout_.top() + dy);
}

double StripModel::scrollTo(double top) {
    const double moved = layout_.scrollTo(top);
    if (moved != 0) {
        update();
        if (moved_) {
            changed();  // the page or the chapter under the middle line changed
        }
    }
    return moved;
}

void StripModel::setScreen(core::ImageSize units, core::ImageSize screen, core::ReaderOrientation orientation) {
    if (units == options_.units && screen == options_.screen && orientation == options_.orientation) {
        return;
    }
    const core::ImageSize before = decodeBox();
    options_.units = units;
    options_.screen = screen;
    options_.orientation = orientation;
    layout_.setViewHeight(units.height);
    relayout();
    if (decodeBox() != before) {
        decodeAgain();
    }
    update();
    changed();
}

void StripModel::setStripWidth(core::StripWidth width) {
    if (width == options_.width) {
        return;
    }
    const core::ImageSize before = decodeBox();
    options_.width = width;
    relayout();
    if (decodeBox() != before) {
        decodeAgain();
    }
    update();
    changed();
}

void StripModel::decodeAgain() {
    for (Part& part : parts_) {
        for (Slot& slot : part.slots) {
            if (slot.page.state == ReaderPage::State::Ready || slot.page.state == ReaderPage::State::Loading) {
                resetSlot(slot);  // updateWindow loads it again at the new box
            }
        }
    }
}

// --- Progress ---

void StripModel::onFrame() {
    if (!saver_->closing) {
        ProgressSaver::flush(saver_, sync_, false);
        ProgressSaver::flushLeft(saver_, false);
    }
}

void StripModel::saveNow() {
    ProgressSaver::flush(saver_, sync_, true);
    ProgressSaver::flushLeft(saver_, true);
}

void StripModel::close(std::function<void()> done) {
    ProgressSaver::close(saver_, sync_, std::move(done));
}

void StripModel::onSaved(const core::ReadProgress& progress) {
    auto apply = [&progress](core::Chapter& chapter) {
        chapter.lastPageRead = progress.lastPageRead;
        chapter.isRead = chapter.isRead || progress.isRead;
        if (progress.spot) {
            chapter.stripSpotNote = core::stripSpotValue(progress.lastPageRead, *progress.spot);
        }
    };
    if (const int index = chapterIndex(progress.chapterId); index >= 0) {
        apply(chapters_[static_cast<std::size_t>(index)]);
    }
    if (Part* part = partOf(progress.chapterId)) {
        apply(part->chapter);
    }
}

}  // namespace snx::ui

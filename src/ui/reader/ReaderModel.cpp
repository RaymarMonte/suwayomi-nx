#include "ui/reader/ReaderModel.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace snx::ui {

namespace {

// The page failed because the server didn't answer, not because it answered with something broken: worth asking again
// once the server may be back.
bool failedForNetwork(const ReaderPage& page) {
    return page.state == ReaderPage::State::Failed &&
           (page.error.kind == image::ImageErrorKind::Network || page.error.kind == image::ImageErrorKind::Timeout);
}

}  // namespace

ReaderOptions readerOptionsFrom(const core::RuntimeProfile& profile, const core::ReaderSettings& reader) {
    ReaderOptions options;
    options.prefetchBehind = profile.prefetchBehind;
    options.prefetchAhead = profile.prefetchAhead;
    options.nextChapterPages = profile.nextChapterPrefetchPages;
    options.backgroundPrefetch = profile.backgroundPrefetch;
    options.progressSyncMs = profile.progressSyncMs;
    options.progressRetryMaxMs = profile.progressRetryMaxMs;
    options.maxZoom = profile.maxZoom;
    options.maxDecodeScale = profile.maxDecodeScale;
    options.spreadSplit = reader.splitSpreads;
    options.format = reader.serverConvertsToJpeg ? api::PageFormat::Jpeg : api::PageFormat::AsStored;
    return options;
}

// Sends the progress updates. Shared with the requests it starts, so a save that is on its way when the reader closes
// finishes (and starts the next one, if pages were turned meanwhile) after the model is gone. Saves are never cancelled:
// an update cut off half way might or might not have reached the server. Main thread only (the client's deliver).
struct ReaderModel::Saver {
    api::SuwayomiClient& client;
    std::function<std::int64_t()> clock;
    std::function<void(const core::ReadProgress&, bool)> onSaved;
    std::function<void(const core::ReadProgress&)> toModel;  // the model, while it exists
    std::vector<std::shared_ptr<core::ProgressSync>> left;   // chapters left behind with something not saved yet
    int inFlight = 0;
    bool closing = false;  // no more retries: send what's left once, then call whenDone
    std::vector<std::function<void()>> whenDone;
    net::CancelToken never;

    Saver(api::SuwayomiClient& c, std::function<std::int64_t()> time, std::function<void(const core::ReadProgress&, bool)> saved)
        : client(c), clock(std::move(time)), onSaved(std::move(saved)) {}

    static void send(const std::shared_ptr<Saver>& self, const std::shared_ptr<core::ProgressSync>& sync,
                     const core::ReadProgress& progress) {
        sync->sending(progress, self->clock());
        ++self->inFlight;
        self->client.setProgress(progress.chapterId, progress.lastPageRead, progress.isRead, self->never)
            .then([self, sync, progress](const api::ApiResult<core::ReadProgress>& result) {
                --self->inFlight;
                const bool ok = result.ok();
                if (ok) {
                    sync->saved(progress);
                } else {
                    sync->failed(self->clock());
                }
                if (self->onSaved) {
                    self->onSaved(progress, ok);
                }
                if (ok && self->toModel) {
                    self->toModel(progress);
                }
                if (self->closing && ok) {
                    // Pages turned while this one was on its way.
                    if (const auto more = sync->dueNow()) {
                        send(self, sync, *more);
                    }
                }
                self->tidy();
            });
    }

    // Sends what is due (now: without waiting for the interval).
    static void flush(const std::shared_ptr<Saver>& self, const std::shared_ptr<core::ProgressSync>& sync, bool now) {
        if (!sync) {
            return;
        }
        const auto progress = now ? sync->dueNow() : sync->due(self->clock());
        if (progress) {
            send(self, sync, *progress);
        }
    }

    static void flushLeft(const std::shared_ptr<Saver>& self, bool now) {
        const auto chapters = self->left;  // send() may change the list
        for (const auto& sync : chapters) {
            flush(self, sync, now);
        }
        self->tidy();
    }

    void tidy() {
        left.erase(std::remove_if(left.begin(), left.end(),
                                  [](const auto& sync) { return !sync->pending() && !sync->inFlight(); }),
                   left.end());
        if (closing && inFlight == 0) {
            std::vector<std::function<void()>> callbacks;
            callbacks.swap(whenDone);
            for (auto& callback : callbacks) {
                if (callback) {
                    callback();
                }
            }
        }
    }
};

ReaderModel::ReaderModel(api::SuwayomiClient& client, image::ImageLoader& images, core::Manga manga,
                         std::vector<core::Chapter> chapters, core::MangaReaderSettings settings, ReaderOptions options)
    : client_(client),
      images_(images),
      manga_(std::move(manga)),
      chapters_(std::move(chapters)),
      settings_(settings),
      options_(std::move(options)) {
    std::stable_sort(chapters_.begin(), chapters_.end(),
                     [](const core::Chapter& a, const core::Chapter& b) { return a.sourceOrder < b.sourceOrder; });
    settings_.mangaId = manga_.id;
    saver_ = std::make_shared<Saver>(client_, options_.clock, options_.onSaved);
    saver_->toModel = [this](const core::ReadProgress& progress) { onSaved(progress); };
}

ReaderModel::~ReaderModel() {
    listCancel_.cancel();
    nextCancel_.cancel();
    zoom_.cancel.cancel();
    for (Slot& slot : slots_) {
        slot.cancel.cancel();
    }
    if (next_) {
        for (Slot& slot : next_->slots) {
            slot.cancel.cancel();
        }
    }
    saver_->toModel = nullptr;
    if (!saver_->closing) {
        close(nullptr);
    }
}

void ReaderModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void ReaderModel::changed() {
    if (listener_) {
        listener_();
    }
}

int ReaderModel::chapterIndex(int chapterId) const {
    for (std::size_t i = 0; i < chapters_.size(); ++i) {
        if (chapters_[i].id == chapterId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const core::Chapter* ReaderModel::nextChapter() const {
    const int index = chapterIndex(chapter_.id);
    if (index < 0 || index + 1 >= static_cast<int>(chapters_.size())) {
        return nullptr;
    }
    return &chapters_[static_cast<std::size_t>(index + 1)];
}

const core::Chapter* ReaderModel::previousChapter() const {
    const int index = chapterIndex(chapter_.id);
    if (index <= 0) {
        return nullptr;
    }
    return &chapters_[static_cast<std::size_t>(index - 1)];
}

const ReaderPage& ReaderModel::page(int index) const {
    static const ReaderPage kNone;
    if (index < 0 || index >= pageCount()) {
        return kNone;
    }
    return slots_[static_cast<std::size_t>(index)].page;
}

core::ImageSize ReaderModel::pageBox() const {
    return core::decodeBoxFor(settings_.fit, options_.screen);
}

bool ReaderModel::splitting() const {
    return core::splitsSpread(options_.spreadSplit, orientation_, true);
}

core::ImageSize ReaderModel::boxFor(const Slot& slot) const {
    // A spread known to split decodes twice as wide, so each half has the pixels a page would (WP-13c).
    return slot.spread && splitting() ? core::splitDecodeBox(pageBox()) : pageBox();
}

bool ReaderModel::split() const {
    if (!onPage()) {
        return false;
    }
    const ReaderPage& page = current();
    return page.state == ReaderPage::State::Ready && page.image &&
           core::splitsSpread(options_.spreadSplit, orientation_, core::isSpread(page.image->sourceSize()));
}

std::optional<core::Side> ReaderModel::halfSide() const {
    if (!split()) {
        return std::nullopt;
    }
    return core::spreadHalfSide(half_, settings_.direction);
}

bool ReaderModel::chapterRead() const {
    return sync_ ? sync_->read() : chapter_.isRead;
}

void ReaderModel::open(int chapterId, int page) {
    if (opened_) {
        return;
    }
    opened_ = true;
    load(chapterId, Start{Start::Kind::Page, page});
}

void ReaderModel::retry() {
    if (state_ == State::Failed) {
        load(chapter_.id, start_);
    }
}

void ReaderModel::retryPage() {
    if (state_ != State::Ready || !onPage()) {
        return;
    }
    Slot& slot = slots_[static_cast<std::size_t>(position_)];
    if (slot.page.state != ReaderPage::State::Failed) {
        return;
    }
    resetSlot(slot);
    startLoad(slot, chapter_.id, position_, net::Priority::Reader);
    changed();
}

void ReaderModel::leaveChapter() {
    dropZoom();
    listCancel_.cancel();
    for (Slot& slot : slots_) {
        slot.cancel.cancel();
    }
    slots_.clear();
    if (sync_) {
        saver_->left.push_back(sync_);
        Saver::flush(saver_, sync_, true);
        saver_->tidy();
        sync_.reset();
    }
}

void ReaderModel::load(int chapterId, Start start) {
    const int index = chapterIndex(chapterId);
    const bool same = chapterId == chapter_.id;
    leaveChapter();
    if (index >= 0) {
        chapter_ = chapters_[static_cast<std::size_t>(index)];
    } else if (!same) {
        chapter_ = core::Chapter{};
        chapter_.id = chapterId;
        chapter_.mangaId = manga_.id;
    }
    start_ = start;
    position_ = 0;
    error_ = api::ApiError{};

    // The next chapter's list may be here already (fetched near the end of the last one): no request, no wait.
    if (next_ && next_->chapterId == chapterId && next_->listReady) {
        Prefetched prefetched = std::move(*next_);
        next_.reset();
        showList(prefetched.list, std::move(prefetched.slots), start);
        return;
    }
    nextCancel_.cancel();
    if (next_) {
        for (Slot& slot : next_->slots) {
            slot.cancel.cancel();
        }
        next_.reset();
    }

    state_ = State::Loading;
    listCancel_ = net::CancelToken();
    const net::CancelToken token = listCancel_;
    client_.pages(chapterId, options_.format, token).then([this, token, start](const api::ApiResult<core::PageList>& result) {
        if (token.cancelled()) {
            return;
        }
        onPages(start, result);
    });
}

void ReaderModel::onPages(Start start, const api::ApiResult<core::PageList>& result) {
    if (!result.ok()) {
        state_ = State::Failed;
        error_ = result.error();
        changed();
        return;
    }
    showList(result.value(), makeSlots(result.value()), start);
}

std::vector<ReaderModel::Slot> ReaderModel::makeSlots(const core::PageList& list) const {
    std::vector<Slot> slots(list.pages.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!list.pages[i].empty()) {
            slots[i].url = client_.absoluteUrl(list.pages[i]);
        }
    }
    return slots;
}

void ReaderModel::showList(const core::PageList& list, std::vector<Slot> ready, Start start) {
    // What the server says about the chapter now (the list's copy may be older).
    const core::Chapter& fresh = list.chapter;
    chapter_.isRead = fresh.isRead;
    chapter_.lastPageRead = fresh.lastPageRead;
    chapter_.pageCount = static_cast<int>(list.pages.size());
    if (const int index = chapterIndex(chapter_.id); index >= 0) {
        chapters_[static_cast<std::size_t>(index)] = chapter_;
    }

    slots_ = std::move(ready);
    const int count = pageCount();
    int page = start.page;
    if (start.kind == Start::Kind::Resume) {
        page = fresh.isRead ? 0 : fresh.lastPageRead;
    } else if (start.kind == Start::Kind::Last) {
        page = count - 1;
    }
    position_ = count == 0 ? 0 : std::clamp(page, 0, count - 1);  // no pages: the "next chapter" screen
    half_ = start.kind == Start::Kind::Last ? 1 : 0;  // back from the next chapter: a spread's last half

    sync_ = std::make_shared<core::ProgressSync>(chapter_.id, fresh.lastPageRead, fresh.isRead, options_.progressSyncMs,
                                                 options_.progressRetryMaxMs, options_.clock());
    state_ = State::Ready;
    arrive();
    changed();
}

bool ReaderModel::step(int delta) {
    if (state_ != State::Ready || delta == 0) {
        return false;
    }
    // A split spread (WP-13c): its first half steps forward to its second, its second back to its first. The same page, so
    // no new progress and the same window.
    if (split() && ((delta == 1 && half_ == 0) || (delta == -1 && half_ == 1))) {
        half_ += delta;
        dropZoom();  // a zoomed decode belongs to the half it was made for; the reader asks again for this one
        changed();
        return true;
    }
    const int target = position_ + delta;
    if (target > pageCount()) {
        const core::Chapter* next = nextChapter();
        if (next == nullptr) {
            return false;
        }
        load(next->id, Start{Start::Kind::Resume, 0});
        changed();
        return true;
    }
    if (target < -1) {
        const core::Chapter* previous = previousChapter();
        if (previous == nullptr) {
            return false;
        }
        load(previous->id, Start{Start::Kind::Last, 0});
        changed();
        return true;
    }
    position_ = target;
    half_ = delta > 0 ? 0 : 1;  // a step back lands on a spread's last half (also one not decoded yet)
    arrive();
    changed();
    return true;
}

bool ReaderModel::turn(core::Side side) {
    return step(core::pageStep(side, settings_.direction));
}

void ReaderModel::arrive() {
    dropZoom();  // a zoomed decode belongs to the page it was made for; the reader asks again for the new one
    if (onPage() && sync_) {
        sync_->shown(position_, position_ == pageCount() - 1);
    }
    // A page that failed for want of the server loads again when it is turned to (updateWindow starts it).
    if (onPage() && failedForNetwork(slots_[static_cast<std::size_t>(position_)].page)) {
        resetSlot(slots_[static_cast<std::size_t>(position_)]);
    }
    updateWindow();
}

void ReaderModel::resetSlot(Slot& slot) {
    slot.cancel.cancel();
    slot.page = ReaderPage{};
}

void ReaderModel::updateWindow() {
    const int count = pageCount();
    if (count == 0) {
        return;
    }
    const int centre = std::clamp(position_, 0, count - 1);
    const std::vector<int> wanted = core::prefetchOrder(centre, count, options_.prefetchBehind, options_.prefetchAhead);
    const std::set<int> window(wanted.begin(), wanted.end());
    for (int i = 0; i < count; ++i) {
        Slot& slot = slots_[static_cast<std::size_t>(i)];
        if (window.count(i) == 0 && slot.page.state != ReaderPage::State::Waiting) {
            resetSlot(slot);  // let it go: the memory cache keeps it as long as its budget allows
        }
    }
    for (const int i : wanted) {
        Slot& slot = slots_[static_cast<std::size_t>(i)];
        const net::Priority priority = i == position_ ? net::Priority::Reader : net::Priority::Prefetch;
        if (slot.page.state == ReaderPage::State::Waiting) {
            startLoad(slot, chapter_.id, i, priority);
        } else {
            sharpenSpread(slot, chapter_.id, i, priority);  // e.g. a spread prefetched with the next chapter
        }
    }
    if (options_.backgroundPrefetch && options_.nextChapterPages > 0 &&
        core::nearChapterEnd(centre, count, options_.prefetchAhead) && !next_ && nextChapter() != nullptr) {
        requestNext();
    }
}

void ReaderModel::startLoad(Slot& slot, int chapterId, int index, net::Priority priority) {
    if (slot.url.empty()) {
        slot.page.state = ReaderPage::State::Failed;
        slot.page.error = image::imageError(image::ImageErrorKind::Corrupt, "the server gave no address for this page");
        return;
    }
    const core::ImageSize box = boxFor(slot);
    if (image::ImagePtr hit = images_.cached(slot.url, box, image::ImageKind::Page)) {
        slot.page.state = ReaderPage::State::Ready;
        slot.page.image = std::move(hit);
        slot.page.reloading = false;
        return;
    }
    slot.cancel = net::CancelToken();
    if (slot.page.state == ReaderPage::State::Ready) {
        slot.page.reloading = true;  // a spread decoding again for its halves: its smaller image shows meanwhile
    } else {
        slot.page.state = ReaderPage::State::Loading;
    }
    const net::CancelToken token = slot.cancel;
    images_.load(image::LoadRequest{slot.url, box, priority, image::ImageKind::Page}, token,
                 [this, token, chapterId, index](const image::ImageResult& result) {
                     if (token.cancelled()) {
                         return;  // left the window, or the model is gone
                     }
                     onLoaded(chapterId, index, result);
                 });
}

void ReaderModel::onLoaded(int chapterId, int index, const image::ImageResult& result) {
    std::vector<Slot>* slots = nullptr;
    if (chapterId == chapter_.id) {
        slots = &slots_;
    } else if (next_ && next_->chapterId == chapterId) {
        slots = &next_->slots;
    }
    if (slots == nullptr || index < 0 || index >= static_cast<int>(slots->size())) {
        return;
    }
    Slot& slot = (*slots)[static_cast<std::size_t>(index)];
    ReaderPage& page = slot.page;
    const bool onScreen = slots == &slots_ && index == position_;
    if (result.ok()) {
        page.state = ReaderPage::State::Ready;
        page.image = result.value();
        page.error = image::ImageError{};
        page.reloading = false;
        if (core::isSpread(page.image->sourceSize())) {
            slot.spread = true;
            // Found only now: one that splits decodes again at its doubled box at once (usually before it is on screen).
            sharpenSpread(slot, chapterId, index, onScreen ? net::Priority::Reader : net::Priority::Prefetch);
        }
        if (slots == &slots_) {
            reloadNetworkFailures();
        }
    } else if (page.reloading) {
        page.reloading = false;  // the doubled decode didn't come (busy, memory): the smaller image stays, scaled by the GPU
    } else {
        const image::ImageErrorKind kind = result.error().kind;
        if (kind == image::ImageErrorKind::Paused || (kind == image::ImageErrorKind::Busy && !onScreen)) {
            page.state = ReaderPage::State::Waiting;  // asked for again when it is the page on screen
        } else {
            page.state = ReaderPage::State::Failed;
            page.error = result.error();
        }
    }
    changed();
}

// A page arrived, so the server may answer again: the pages in the window that failed for want of it load again, instead of
// each waiting for its own Retry. Only a success calls this, so a server that is still away can't make it loop.
void ReaderModel::reloadNetworkFailures() {
    bool any = false;
    for (Slot& slot : slots_) {
        if (failedForNetwork(slot.page)) {
            resetSlot(slot);
            any = true;
        }
    }
    if (any) {
        updateWindow();
    }
}

void ReaderModel::requestNext() {
    const core::Chapter* next = nextChapter();
    if (next == nullptr) {
        return;
    }
    const int chapterId = next->id;
    next_ = Prefetched{};
    next_->chapterId = chapterId;
    nextCancel_ = net::CancelToken();
    const net::CancelToken token = nextCancel_;
    client_.pages(chapterId, options_.format, token, net::Priority::Prefetch)
        .then([this, token, chapterId](const api::ApiResult<core::PageList>& result) {
            if (token.cancelled()) {
                return;
            }
            onNextPages(chapterId, result);
        });
}

void ReaderModel::onNextPages(int chapterId, const api::ApiResult<core::PageList>& result) {
    if (!next_ || next_->chapterId != chapterId || !result.ok()) {
        return;  // the next chapter loads its list again when it opens
    }
    next_->list = result.value();
    next_->slots = makeSlots(next_->list);
    next_->listReady = true;
    const int count = std::min(options_.nextChapterPages, static_cast<int>(next_->slots.size()));
    for (int i = 0; i < count; ++i) {
        startLoad(next_->slots[static_cast<std::size_t>(i)], chapterId, i, net::Priority::Prefetch);
    }
    changed();
}

void ReaderModel::setDirection(core::ReadingDirection direction) {
    if (settings_.direction == direction) {
        return;
    }
    settings_.direction = direction;
    changed();
}

void ReaderModel::setFit(core::PageFit fit) {
    if (settings_.fit == fit) {
        return;
    }
    settings_.fit = fit;
    decodeAgain();
}

void ReaderModel::setScreen(core::ImageSize screen, core::ReaderOrientation orientation) {
    const core::ImageSize before = pageBox();
    const bool splitBefore = splitting();
    options_.screen = screen;
    orientation_ = orientation;
    const bool splitNow = splitting();
    if (splitNow != splitBefore) {
        half_ = 0;  // a spread that splits now starts at its first half (WP-13c)
    }
    if (pageBox() != before) {
        decodeAgain();  // every page, the known spreads at their new box
    } else if (splitNow != splitBefore) {
        setSpreadBoxes();
    }
}

void ReaderModel::setSpreadSplit(core::SpreadSplit mode) {
    if (mode == options_.spreadSplit) {
        return;
    }
    const bool splitBefore = splitting();
    options_.spreadSplit = mode;
    if (splitting() != splitBefore) {
        half_ = 0;
        setSpreadBoxes();
    }
}

void ReaderModel::setSpreadBoxes() {
    // Whether spreads split changed, so the known ones want another box; every other page keeps its image. Split now: they
    // decode again at the doubled box, showing their image meanwhile (updateWindow -> sharpenSpread). Whole again: they load
    // at the plain box (most likely from the memory cache), so they don't keep the bigger image.
    dropZoom();
    if (!splitting()) {
        for (Slot& slot : slots_) {
            if (slot.spread) {
                resetSlot(slot);
            }
        }
    }
    if (state_ == State::Ready) {
        updateWindow();
    }
    changed();
}

void ReaderModel::sharpenSpread(Slot& slot, int chapterId, int index, net::Priority priority) {
    // A spread that splits wants its doubled box (boxFor). One decoded smaller (found as a spread only once decoded, or
    // prefetched before the split was on) loads again at it; its smaller image shows meanwhile. Never smaller again.
    const ReaderPage& page = slot.page;
    if (!slot.spread || page.state != ReaderPage::State::Ready || page.reloading || !page.image) {
        return;
    }
    if (core::isSharp(page.image->size(), page.image->sourceSize(), boxFor(slot))) {
        return;
    }
    startLoad(slot, chapterId, index, priority);
}

void ReaderModel::decodeAgain() {
    // Every page decodes again for the new box.
    dropZoom();
    for (Slot& slot : slots_) {
        resetSlot(slot);
    }
    if (next_) {
        nextCancel_.cancel();
        for (Slot& slot : next_->slots) {
            slot.cancel.cancel();
        }
        next_.reset();
    }
    if (state_ == State::Ready) {
        updateWindow();
    }
    changed();
}

void ReaderModel::dropZoom() {
    zoom_.cancel.cancel();
    zoom_ = ZoomLoad{};
}

bool ReaderModel::zoomIsForScreen() const {
    return zoom_.chapterId == chapter_.id && zoom_.index == position_ && zoom_.half == half();
}

void ReaderModel::setPageZoom(float zoom) {
    if (state_ != State::Ready || !onPage()) {
        return;
    }
    const Slot& slot = slots_[static_cast<std::size_t>(position_)];
    const core::ImageSize box = boxForZoom(zoom);
    const bool samePage = zoomIsForScreen();
    // A bigger box that gives no more pixels than the page's own (the source has no more detail, or the cap holds it, like
    // Beta's split spread at maxDecodeScale 2) would decode the same image again: the page's image is as sharp as it gets.
    const image::ImagePtr& own = slot.page.image;
    const bool noSharper =
        own && core::fitInside(own->sourceSize(), box) == core::fitInside(own->sourceSize(), boxFor(slot));
    if (!core::isZoomed(zoom) || box == boxFor(slot) || noSharper || slot.url.empty()) {
        // At the fit (or no bigger box allowed): the page's own image. Let a sharper one go.
        const bool had = zoom_.image != nullptr || zoom_.loading;
        dropZoom();
        zoom_.chapterId = chapter_.id;
        zoom_.index = position_;
        zoom_.half = half();
        zoom_.zoom = zoom;
        if (had) {
            changed();
        }
        return;
    }
    // The same bucket, or a box with no more pixels than the zoomed decode there (or on its way): that one is right.
    const bool sameDetail =
        own && core::fitInside(own->sourceSize(), box) == core::fitInside(own->sourceSize(), zoom_.box);
    if (samePage && zoom_.box.width + zoom_.box.height > 0 && (zoom_.box == box || sameDetail)) {
        zoom_.zoom = zoom;
        return;
    }
    dropZoom();
    zoom_.chapterId = chapter_.id;
    zoom_.index = position_;
    zoom_.half = half();
    zoom_.zoom = zoom;
    zoom_.box = box;
    if (image::ImagePtr hit = images_.cached(slot.url, box, image::ImageKind::Page)) {
        zoom_.image = std::move(hit);
        changed();
        return;
    }
    zoom_.loading = true;
    const net::CancelToken token = zoom_.cancel;
    const int chapterId = chapter_.id;
    const int index = position_;
    const int half = zoom_.half;
    images_.load(image::LoadRequest{slot.url, box, net::Priority::Reader, image::ImageKind::Page}, token,
                 [this, token, chapterId, index, half, box](const image::ImageResult& result) {
                     if (token.cancelled() || zoom_.chapterId != chapterId || zoom_.index != index ||
                         zoom_.half != half || zoom_.box != box) {
                         return;  // turned away, zoomed again, or the model is gone
                     }
                     zoom_.loading = false;
                     if (result.ok()) {
                         zoom_.image = result.value();
                     }
                     // A failed zoomed decode (busy, memory) isn't an error: the fit's image stays, scaled by the GPU.
                     changed();
                 });
}

image::ImagePtr ReaderModel::shownImage() const {
    if (!onPage() || current().state != ReaderPage::State::Ready) {
        return nullptr;
    }
    if (zoom_.image && zoomIsForScreen()) {
        return zoom_.image;
    }
    return current().image;
}

core::ImageSize ReaderModel::wantedBox() const {
    return boxForZoom(zoomIsForScreen() ? zoom_.zoom : 1.0f);
}

core::ImageSize ReaderModel::boxForZoom(float zoom) const {
    // A split spread: the doubled box times the zoom (WP-13c), capped like any page.
    return core::zoomDecodeBox(settings_.fit, options_.screen, zoom, options_.maxZoom, options_.maxDecodeScale, split());
}

void ReaderModel::onFrame() {
    if (!saver_->closing) {
        Saver::flush(saver_, sync_, false);
        Saver::flushLeft(saver_, false);
    }
}

void ReaderModel::saveNow() {
    Saver::flush(saver_, sync_, true);
    Saver::flushLeft(saver_, true);
}

void ReaderModel::close(std::function<void()> done) {
    saver_->closing = true;
    saver_->whenDone.push_back(std::move(done));
    saveNow();
    saver_->tidy();
}

void ReaderModel::onSaved(const core::ReadProgress& progress) {
    const int index = chapterIndex(progress.chapterId);
    if (index >= 0) {
        core::Chapter& chapter = chapters_[static_cast<std::size_t>(index)];
        chapter.lastPageRead = progress.lastPageRead;
        chapter.isRead = chapter.isRead || progress.isRead;
    }
    if (progress.chapterId == chapter_.id) {
        chapter_.lastPageRead = progress.lastPageRead;
        chapter_.isRead = chapter_.isRead || progress.isRead;
    }
    changed();
}

}  // namespace snx::ui

#include "ui/reader/ReaderModel.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace snx::ui {

ReaderOptions readerOptionsFrom(const core::RuntimeProfile& profile, const core::ReaderSettings& reader) {
    ReaderOptions options;
    options.prefetchBehind = profile.prefetchBehind;
    options.prefetchAhead = profile.prefetchAhead;
    options.nextChapterPages = profile.nextChapterPrefetchPages;
    options.backgroundPrefetch = profile.backgroundPrefetch;
    options.progressSyncMs = profile.progressSyncMs;
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
                    sync->failed();
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

    sync_ = std::make_shared<core::ProgressSync>(chapter_.id, fresh.lastPageRead, fresh.isRead, options_.progressSyncMs,
                                                 options_.clock());
    state_ = State::Ready;
    arrive();
    changed();
}

bool ReaderModel::step(int delta) {
    if (state_ != State::Ready || delta == 0) {
        return false;
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
    arrive();
    changed();
    return true;
}

bool ReaderModel::turn(core::Side side) {
    return step(core::pageStep(side, settings_.direction));
}

void ReaderModel::arrive() {
    if (onPage() && sync_) {
        sync_->shown(position_, position_ == pageCount() - 1);
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
        if (slot.page.state == ReaderPage::State::Waiting) {
            startLoad(slot, chapter_.id, i, i == position_ ? net::Priority::Reader : net::Priority::Prefetch);
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
    const core::ImageSize box = pageBox();
    if (image::ImagePtr hit = images_.cached(slot.url, box, image::ImageKind::Page)) {
        slot.page.state = ReaderPage::State::Ready;
        slot.page.image = std::move(hit);
        return;
    }
    slot.cancel = net::CancelToken();
    slot.page.state = ReaderPage::State::Loading;
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
    ReaderPage& page = (*slots)[static_cast<std::size_t>(index)].page;
    if (result.ok()) {
        page.state = ReaderPage::State::Ready;
        page.image = result.value();
        page.error = image::ImageError{};
    } else {
        const image::ImageErrorKind kind = result.error().kind;
        const bool onScreen = slots == &slots_ && index == position_;
        if (kind == image::ImageErrorKind::Paused || (kind == image::ImageErrorKind::Busy && !onScreen)) {
            page.state = ReaderPage::State::Waiting;  // asked for again when it is the page on screen
        } else {
            page.state = ReaderPage::State::Failed;
            page.error = result.error();
        }
    }
    changed();
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
    // Every page decodes again for the new box.
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

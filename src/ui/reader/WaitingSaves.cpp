#include "ui/reader/WaitingSaves.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

#include "core/StripMath.hpp"

namespace snx::ui {

namespace {

constexpr int kErrorsToDrop = 3;  // other errors in a row for one entry before it is dropped (D53)

// The login's user name, or "" when the server has no login (a name left over from an earlier login doesn't count).
std::string userOf(const core::ServerSettings& settings) {
    return settings.auth == core::AuthMode::None ? std::string() : settings.username;
}

bool serverAway(api::ApiErrorKind kind) {
    return kind == api::ApiErrorKind::Offline || kind == api::ApiErrorKind::Timeout;
}

// For the log: why a decision sends ours or drops it (D61).
std::string whyText(core::WaitingDecision decision) {
    switch (decision) {
        case core::WaitingDecision::Finished:
            return "finished here, unread on the server";
        case core::WaitingDecision::Later:
            return "read here later than on the server";
        case core::WaitingDecision::ServerLater:
            return "read on the server later";
        default:
            return "the server is still where it was";
    }
}

}  // namespace

WaitingSaves::Options WaitingSaves::optionsFrom(const core::RuntimeProfile& profile) {
    Options options;
    options.maxEntries = static_cast<std::size_t>(std::max(profile.pendingSavesMax, 1));
    options.fileBytes = static_cast<std::size_t>(profile.progressQueueFileBytes);
    options.maxDepth = profile.json.maxDepth;
    options.saveDelayMs = profile.configSaveDelayMs;
    options.retryMs = profile.progressSyncMs;
    options.retryMaxMs = profile.progressRetryMaxMs;
    return options;
}

WaitingSaves::WaitingSaves(api::SuwayomiClient& client, const platform::SafeFs* fs, Options options, Hooks hooks)
    : client_(client), fs_(fs), options_(options), hooks_(std::move(hooks)), scheduler_(options.saveDelayMs) {}

WaitingSaves::~WaitingSaves() {
    *alive_ = false;
    waitForWrite();  // the worker writes with fs_: it ends first
}

void WaitingSaves::say(bool warning, const std::string& line) const {
    if (hooks_.log) {
        hooks_.log(warning, line);
    }
}

void WaitingSaves::tell(const char* what, const core::WaitingSave& entry, const std::string& reason) const {
    if (hooks_.report) {
        hooks_.report(what, entry, reason, entries_.size(), held(entry.chapterId));
    }
}

void WaitingSaves::notify(Change change, const core::WaitingSave& entry) {
    const core::WaitingSave copy = entry;  // the listeners may look at the queue meanwhile
    for (const auto& listener : std::vector(listeners_)) {
        listener.second(change, copy);
    }
}

void WaitingSaves::waitingChanged() {
    nudgeWanted_ = !entries_.empty() && !authStopped_;
}

void WaitingSaves::changed() {
    scheduler_.changed(hooks_.clockMs());
}

core::WaitingSave* WaitingSaves::find(const std::string& url, const std::string& user, int chapterId) {
    for (core::WaitingSave& entry : entries_) {
        if (entry.chapterId == chapterId && entry.serverUrl == url && entry.user == user) {
            return &entry;
        }
    }
    return nullptr;
}

void WaitingSaves::forget(const core::WaitingSave* entry) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [entry](const core::WaitingSave& e) { return &e == entry; }),
                   entries_.end());
    changed();
    waitingChanged();
}

// --- The file ---

void WaitingSaves::load() {
    if (fs_ == nullptr) {
        return;
    }
    core::JsonLimits limits;
    limits.maxBytes = options_.fileBytes;
    limits.maxDepth = options_.maxDepth;
    const std::optional<std::string> text = fs_->readFile(kFile, options_.fileBytes);
    const bool fileThere = text.has_value() || fs_->exists(kFile);
    core::ProgressQueueLoad loaded;
    if (text) {
        loaded = core::loadProgressQueue(*text, limits, options_.maxEntries);
    }
    bool fromBackup = false;
    if (!loaded.usable) {
        // A file cut short (a power cut while writing), one too big to read (S6) or none left by an interrupted write.
        if (const auto backup = fs_->readFile(kBackupFile, options_.fileBytes)) {
            core::ProgressQueueLoad again = core::loadProgressQueue(*backup, limits, options_.maxEntries);
            if (again.usable) {
                say(true, std::string("progress queue: ") + kFile + (fileThere ? " is unreadable" : " is missing") +
                              ", using " + kBackupFile);
                loaded = std::move(again);
                fromBackup = true;
            }
        }
        if (!fromBackup && fileThere) {
            say(true, std::string("progress queue: ") + kFile + " is unreadable and there is no usable " + kBackupFile +
                          ": starting empty");
        }
    }
    for (const std::string& problem : loaded.problems) {
        say(true, problem);
    }
    entries_ = std::move(loaded.entries);
    if (fromBackup) {
        changed();  // the file is written again from what the backup had
    }
    say(false, "progress queue: " + std::to_string(entries_.size()) + " waiting");
    for (const core::WaitingSave& entry : entries_) {
        say(false, "progress queue: waiting " + core::describeWaitingSave(entry) + " for " + entry.serverUrl);
    }
    waitingChanged();
}

void WaitingSaves::onFrame(bool readerMoving) {
    finishWrite();
    if (!writing_ && !readerMoving && scheduler_.due(hooks_.clockMs())) {
        save();
    }
}

void WaitingSaves::finishWrite() {
    if (!writing_ || !writing_->done.load()) {
        return;
    }
    const std::shared_ptr<Write> write = std::move(writing_);
    writing_.reset();
    if (!write->ok) {
        say(true, std::string("progress queue: couldn't save ") + kFile);
        scheduler_.changed(hooks_.clockMs());  // try again after the next delay
        return;
    }
    say(false, "progress queue: saved (" + std::to_string(write->entries) + " waiting, " + std::to_string(write->bytes) +
                   " bytes, " + std::to_string(write->ms) + " ms on the worker)");
}

void WaitingSaves::waitForWrite() const {
    while (writing_ && !writing_->done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool WaitingSaves::flush() {
    waitForWrite();  // HOME / sleep / exit: a write under way ends first, then whatever changed since is written here
    finishWrite();
    if (!scheduler_.pending()) {
        return true;
    }
    if (fs_ == nullptr) {
        scheduler_.saved();
        return true;
    }
    for (const core::WaitingSave& gone : core::fitProgressQueue(entries_, options_.fileBytes)) {
        say(true, "progress queue: the file would be too big, dropped the oldest: " + core::describeWaitingSave(gone));
        tell("dropped", gone, "full");
        waitingChanged();
    }
    return writeNow(core::saveProgressQueue(entries_), hooks_.clockMs());
}

bool WaitingSaves::save() {
    if (fs_ == nullptr) {
        scheduler_.saved();
        return true;
    }
    for (const core::WaitingSave& gone : core::fitProgressQueue(entries_, options_.fileBytes)) {
        say(true, "progress queue: the file would be too big, dropped the oldest: " + core::describeWaitingSave(gone));
        tell("dropped", gone, "full");
        waitingChanged();
    }
    const std::int64_t started = hooks_.clockMs();
    std::string text = core::saveProgressQueue(entries_);
    if (!hooks_.writeOffThread) {
        return writeNow(text, started);
    }
    // WP-18b (D61): the text is made here (main thread), the write runs on the worker; onFrame() takes the result. Changes
    // meanwhile mark the queue pending again, so the next due frame writes them after this write ended.
    auto write = std::make_shared<Write>();
    write->entries = entries_.size();
    write->bytes = text.size();
    scheduler_.saved();
    writing_ = write;
    const platform::SafeFs* fs = fs_;
    hooks_.writeOffThread([write, fs, text = std::move(text)] {
        const auto begin = std::chrono::steady_clock::now();
        try {
            write->ok = fs->writeAtomic(kFile, text, platform::SafeFs::Backup::Keep);
        } catch (...) {
            write->ok = false;  // done must come whatever happens: flush() waits for it
        }
        write->ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
        write->done.store(true);  // last: the main thread reads ok and ms after it sees done
    });
    say(false, "progress queue: " + std::to_string(write->entries) + " entries made in " +
                   std::to_string(hooks_.clockMs() - started) + " ms, writing on the worker");
    return true;
}

bool WaitingSaves::writeNow(const std::string& text, std::int64_t started) {
    if (!fs_->writeAtomic(kFile, text, platform::SafeFs::Backup::Keep)) {
        say(true, std::string("progress queue: couldn't save ") + kFile);
        scheduler_.changed(hooks_.clockMs());  // try again after the next delay
        return false;
    }
    scheduler_.saved();
    say(false, "progress queue: saved (" + std::to_string(entries_.size()) + " waiting, " + std::to_string(text.size()) +
                   " bytes, " + std::to_string(hooks_.clockMs() - started) + " ms)");
    return true;
}

// --- In and out ---

void WaitingSaves::add(core::WaitingSave entry) {
    const core::ServerSettings settings = client_.settings();
    entry.serverUrl = settings.url;
    entry.user = userOf(settings);
    entry.madeAt = hooks_.nowSeconds ? hooks_.nowSeconds() : 0;
    entry.errors = 0;
    for (const core::WaitingSave& gone : core::addWaitingSave(entries_, std::move(entry), options_.maxEntries)) {
        say(true, "progress queue: full, dropped the oldest: " + core::describeWaitingSave(gone));
        tell("dropped", gone, "full");
    }
    const core::WaitingSave& added = entries_.back();
    // A held chapter's saves (the reader offline, or its held copy) come often: not a warning.
    say(!held(added.chapterId), "progress queue: keeps " + core::describeWaitingSave(added) + " (the server had " +
                                    core::describeServerProgress(added.base) + ")" +
                                    (held(added.chapterId) ? ", held by the reader" : ""));
    changed();
    waitingChanged();
    tell("added", added, "");
    notify(Change::Added, added);
}

std::optional<core::WaitingSave> WaitingSaves::takeOver(int chapterId, const core::ServerProgress& opened) {
    const core::ServerSettings settings = client_.settings();
    core::WaitingSave* found = find(settings.url, userOf(settings), chapterId);
    if (found == nullptr) {
        return std::nullopt;
    }
    const core::WaitingSave entry = *found;
    const core::WaitingDecision decision = core::decideWaitingSave(entry, opened, offset());
    forget(found);
    if (core::sendsWaitingSave(decision)) {
        say(false, "progress queue: the reader takes over " + core::describeWaitingSave(entry) + " (" +
                       whyText(decision) + ")");
        tell("taken", entry, core::waitingDecisionName(decision));
        return entry;
    }
    if (decision == core::WaitingDecision::Already) {
        say(false, "progress queue: the server has " + core::describeWaitingSave(entry) + " already");
        tell("already", entry, "reader");
    } else {
        say(true, "progress queue: dropped " + core::describeWaitingSave(entry) + ": " + whyText(decision) + ", " +
                      core::describeServerProgress(opened) + " (ours was made from " +
                      core::describeServerProgress(entry.base) + ", at " + std::to_string(entry.madeAt) + ")");
        tell("dropped", entry, core::waitingDecisionName(decision));
    }
    return std::nullopt;
}

std::int64_t WaitingSaves::offset() const {
    return hooks_.clockOffset ? hooks_.clockOffset() : 0;
}

void WaitingSaves::hold(int chapterId) {
    held_.insert(chapterId);
}

void WaitingSaves::release(int chapterId) {
    const auto hold = held_.find(chapterId);
    if (hold == held_.end()) {
        return;
    }
    held_.erase(hold);  // one hold: the paged reader and the strip may both hold a chapter
    if (!held(chapterId) && waitingFor(chapterId) != nullptr) {
        say(false, "progress queue: the reader let go of chapter " + std::to_string(chapterId));
    }
}

void WaitingSaves::removeHeld(int chapterId) {
    if (!held(chapterId)) {
        return;
    }
    const core::ServerSettings settings = client_.settings();
    core::WaitingSave* found = find(settings.url, userOf(settings), chapterId);
    if (found == nullptr) {
        return;
    }
    const core::WaitingSave gone = *found;
    forget(found);
    say(false, "progress queue: the reader saved " + core::describeWaitingSave(gone) + " itself, its held copy goes");
    tell("saved", gone, "reader");
    notify(Change::Gone, gone);
}

const core::WaitingSave* WaitingSaves::waitingFor(int chapterId) const {
    const core::ServerSettings settings = client_.settings();
    const std::string user = userOf(settings);
    for (const core::WaitingSave& entry : entries_) {
        if (entry.chapterId == chapterId && entry.serverUrl == settings.url && entry.user == user) {
            return &entry;
        }
    }
    return nullptr;
}

const core::WaitingSave* WaitingSaves::waitingForManga(int mangaId) const {
    const core::ServerSettings settings = client_.settings();
    const std::string user = userOf(settings);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {  // newest first
        if (it->mangaId == mangaId && it->serverUrl == settings.url && it->user == user) {
            return &*it;
        }
    }
    return nullptr;
}

int WaitingSaves::addListener(Listener listener) {
    const int id = nextListener_++;
    listeners_.emplace_back(id, std::move(listener));
    return id;
}

void WaitingSaves::removeListener(int id) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const auto& l) { return l.first == id; }),
                     listeners_.end());
}

// --- Rounds ---

void WaitingSaves::connected() {
    authStopped_ = false;  // a login works (again)
    waitingChanged();
    startRound("connected");
}

void WaitingSaves::serverAnswered() {
    if (!nudgeWanted_.load() || nudgePosted_.exchange(true)) {
        return;
    }
    std::weak_ptr<bool> alive = alive_;
    hooks_.post([this, alive] {
        if (alive.expired()) {
            return;
        }
        nudgePosted_ = false;
        if (anyRound_ && hooks_.clockMs() - lastRoundMs_ < options_.retryMs) {
            return;  // a round ran a moment ago (its own requests answer too)
        }
        startRound("the server answered");
    });
}

void WaitingSaves::startRound(const char* why) {
    if (running_ || authStopped_) {
        return;
    }
    if (hooks_.online && !hooks_.online()) {
        return;  // offline (WP-18b): connected() starts the round once the server is back
    }
    const core::ServerSettings settings = client_.settings();
    if (settings.url.empty()) {
        return;
    }
    Round round;
    round.url = settings.url;
    round.user = userOf(settings);
    for (const core::WaitingSave& entry : entries_) {
        if (entry.serverUrl == round.url && entry.user == round.user && !held(entry.chapterId)) {
            round.chapterIds.push_back(entry.chapterId);
        }
    }
    if (round.chapterIds.empty()) {
        return;
    }
    running_ = true;
    anyRound_ = true;
    lastRoundMs_ = hooks_.clockMs();
    say(false, "progress queue: sending " + std::to_string(round.chapterIds.size()) + " waiting (" + why + ")");
    round_ = std::move(round);
    step();
}

void WaitingSaves::step() {
    while (round_ && round_->next < round_->chapterIds.size()) {
        const core::ServerSettings settings = client_.settings();
        if (settings.url != round_->url || userOf(settings) != round_->user) {
            endRound(false);  // another server or login now: its own round comes after its check
            return;
        }
        const int chapterId = round_->chapterIds[round_->next];
        if (find(round_->url, round_->user, chapterId) == nullptr || held(chapterId)) {
            ++round_->next;  // the reader took it over meanwhile, or opened it (held)
            continue;
        }
        std::weak_ptr<bool> alive = alive_;
        client_.chapterState(chapterId, never_)
            .then([this, alive, chapterId](const api::ApiResult<core::ServerProgress>& result) {
                if (!alive.expired()) {
                    onState(chapterId, result);
                }
            });
        return;  // the answer goes on (it may already have, if the client answers at once)
    }
    if (round_) {
        endRound(false);
    }
}

void WaitingSaves::onState(int chapterId, const api::ApiResult<core::ServerProgress>& result) {
    if (!round_) {
        return;
    }
    core::WaitingSave* entry = find(round_->url, round_->user, chapterId);
    if (entry == nullptr || held(chapterId)) {
        ++round_->next;  // gone meanwhile, or a reader opened it while the check was on its way
        step();
        return;
    }
    if (!result.ok()) {
        if (failed(chapterId, result.error(), "check")) {
            ++round_->next;
            step();
        }
        return;
    }
    const core::ServerProgress& server = result.value();
    const core::WaitingDecision decision = core::decideWaitingSave(*entry, server, offset());
    if (core::sendsWaitingSave(decision)) {
        if (decision != core::WaitingDecision::Send) {
            say(false, "progress queue: the server moved on to " + core::describeServerProgress(server) + ", sending " +
                           core::describeWaitingSave(*entry) + " anyway (" + whyText(decision) + ")");
        }
        round_->reason = core::waitingDecisionName(decision);
        if (hooks_.serverProgress) {
            hooks_.serverProgress(chapterId, server, true);  // what the server had (the Switch's progress.json learns it)
        }
        std::weak_ptr<bool> alive = alive_;
        client_.setProgress(core::progressOf(*entry), never_)
            .then([this, alive, chapterId](const api::ApiResult<core::ReadProgress>& sent) {
                if (!alive.expired()) {
                    onSent(chapterId, sent);
                }
            });
        return;
    }
    if (hooks_.serverProgress) {
        hooks_.serverProgress(chapterId, server, true);
    }
    const core::WaitingSave gone = *entry;
    forget(entry);
    if (decision == core::WaitingDecision::Already) {
        say(false, "progress queue: the server has " + core::describeWaitingSave(gone) + " already");
        tell("already", gone, "");
    } else {
        say(true, "progress queue: dropped " + core::describeWaitingSave(gone) + ": " + whyText(decision) + ", " +
                      core::describeServerProgress(server) + " (ours was made from " +
                      core::describeServerProgress(gone.base) + ", at " + std::to_string(gone.madeAt) + ")");
        tell("dropped", gone, core::waitingDecisionName(decision));
    }
    notify(Change::Gone, gone);  // the screens showing it reload (History's note goes)
    ++round_->next;
    step();
}

void WaitingSaves::onSent(int chapterId, const api::ApiResult<core::ReadProgress>& result) {
    if (!round_) {
        return;
    }
    core::WaitingSave* entry = find(round_->url, round_->user, chapterId);
    if (!result.ok()) {
        if (entry == nullptr || failed(chapterId, result.error(), "send")) {
            ++round_->next;
            step();
        }
        return;
    }
    if (hooks_.serverProgress) {
        const core::ReadProgress& answer = result.value();
        core::ServerProgress now{answer.lastPageRead, answer.isRead, answer.lastReadAt, {}};
        if (entry != nullptr && entry->spot) {
            now.spotNote = core::stripSpotValue(entry->page, *entry->spot);
        }
        hooks_.serverProgress(chapterId, now, false);
    }
    if (entry != nullptr) {
        const core::WaitingSave sent = *entry;
        forget(entry);
        say(false, "progress queue: sent " + core::describeWaitingSave(sent));
        tell("sent", sent, round_->reason);
        notify(Change::Gone, sent);
    }
    ++round_->next;
    step();
}

bool WaitingSaves::failed(int chapterId, const api::ApiError& error, const char* stage) {
    core::WaitingSave* entry = find(round_->url, round_->user, chapterId);
    const std::string kind = api::apiErrorKindName(error.kind);
    if (entry == nullptr) {
        return true;
    }
    if (serverAway(error.kind)) {
        say(false, std::string("progress queue: can't reach the server (") + stage + ", " + kind + "), " +
                       std::to_string(entries_.size()) + " still waiting");
        tell("kept", *entry, kind);
        endRound(true);
        return false;
    }
    if (error.kind == api::ApiErrorKind::Auth || error.kind == api::ApiErrorKind::Cancelled) {
        // Auth: no more rounds until a login works (connected()). Cancelled can't happen (never_), but would end it too.
        say(true, std::string("progress queue: ") + stage + " refused (" + kind + "): waiting for a login");
        tell("kept", *entry, kind);
        if (error.kind == api::ApiErrorKind::Auth) {
            authStopped_ = true;
            waitingChanged();
        }
        endRound(false);
        return false;
    }
    ++entry->errors;
    changed();
    if (entry->errors >= kErrorsToDrop) {
        const core::WaitingSave gone = *entry;
        forget(entry);
        say(true, "progress queue: dropped " + core::describeWaitingSave(gone) + " after " +
                      std::to_string(kErrorsToDrop) + " errors in a row (the last: " + stage + ", " + kind + ", " +
                      error.detail + ")");
        tell("dropped", gone, "errors");
        notify(Change::Gone, gone);
    } else {
        say(true, std::string("progress queue: ") + stage + " failed (" + kind + ", " + error.detail + ") for " +
                      core::describeWaitingSave(*entry) + ", " + std::to_string(entry->errors) + " in a row");
        tell("kept", *entry, kind);
    }
    return true;
}

void WaitingSaves::endRound(bool serverAway) {
    round_.reset();
    running_ = false;
    if (serverAway) {
        ++awayRounds_;
        scheduleRetry();
    } else {
        awayRounds_ = 0;
    }
}

void WaitingSaves::scheduleRetry() {
    // retryMs, doubled for each round in a row that couldn't reach the server, at most retryMaxMs (D45's back-off).
    std::int64_t waitMs = options_.retryMs;
    for (int i = 1; i < awayRounds_ && waitMs < options_.retryMaxMs; ++i) {
        waitMs *= 2;
    }
    waitMs = std::min<std::int64_t>(waitMs, options_.retryMaxMs);
    const std::uint64_t ticket = ++retryTicket_;
    std::weak_ptr<bool> alive = alive_;
    hooks_.later(static_cast<int>(waitMs), [this, alive, ticket] {
        if (!alive.expired() && ticket == retryTicket_) {
            startRound("retry");
        }
    });
}

}  // namespace snx::ui

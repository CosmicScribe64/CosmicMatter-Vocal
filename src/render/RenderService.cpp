#include "RenderService.hpp"

#include "core/Serialization.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace vocalrack {

const RenderedAudio* RenderSlot::acquireAudioRt() noexcept {
    const RenderedAudio* audio = nullptr;
    do {
        audio = current.load(std::memory_order_acquire);
        rtReader.store(audio, std::memory_order_release);
    } while (audio != current.load(std::memory_order_acquire));
    return audio;
}

void RenderSlot::publishAudio(std::shared_ptr<const RenderedAudio> audio, std::string error) {
    std::lock_guard<std::mutex> lock(workerMutex);
    if (currentOwner) retiredOwners.push_back(std::move(currentOwner));
    currentOwner = std::move(audio);
    current.store(currentOwner.get(), std::memory_order_release);
    lastError = std::move(error);
    const auto* protectedAudio = rtReader.load(std::memory_order_acquire);
    retiredOwners.erase(std::remove_if(retiredOwners.begin(), retiredOwners.end(), [protectedAudio](const auto& owner) {
        return owner.get() != protectedAudio;
    }), retiredOwners.end());
}

void RenderSlot::clearAudio() {
    std::lock_guard<std::mutex> lock(workerMutex);
    if (currentOwner) retiredOwners.push_back(std::move(currentOwner));
    current.store(nullptr, std::memory_order_release);
    publishedRequestSerial.store(0, std::memory_order_release);
    const auto* protectedAudio = rtReader.load(std::memory_order_acquire);
    retiredOwners.erase(std::remove_if(retiredOwners.begin(), retiredOwners.end(), [protectedAudio](const auto& owner) {
        return owner.get() != protectedAudio;
    }), retiredOwners.end());
}

RenderDiagnostics RenderSlot::copyDiagnostics() const {
    std::lock_guard<std::mutex> lock(workerMutex);
    return currentOwner ? currentOwner->diagnostics : RenderDiagnostics{};
}

size_t RenderSlot::retainedBufferCount() const {
    std::lock_guard<std::mutex> lock(workerMutex);
    return (currentOwner ? 1u : 0u) + retiredOwners.size();
}

bool RenderKey::operator==(const RenderKey& o) const noexcept {
    return scoreRevision == o.scoreRevision && scoreHash == o.scoreHash && singerId == o.singerId &&
        singerRevision == o.singerRevision &&
        sampleRate == o.sampleRate && bpmMilli == o.bpmMilli && transpose == o.transpose &&
        startTick == o.startTick && endTick == o.endTick && phonemizer == o.phonemizer;
}

static size_t combine(size_t seed, size_t value) noexcept { return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2)); }

size_t RenderKeyHash::operator()(const RenderKey& k) const noexcept {
    size_t h = std::hash<uint64_t>{}(k.scoreHash);
    h = combine(h, std::hash<std::string>{}(k.singerId)); h = combine(h, k.singerRevision); h = combine(h, k.sampleRate);
    h = combine(h, k.bpmMilli); h = combine(h, k.transpose); h = combine(h, std::hash<int64_t>{}(k.startTick));
    h = combine(h, std::hash<int64_t>{}(k.endTick)); h = combine(h, std::hash<std::string>{}(k.phonemizer));
    return h;
}

std::string RenderKey::toString() const {
    std::ostringstream out; out << scoreHash << ':' << singerId << ':' << singerRevision << ':' << sampleRate << ':' << bpmMilli
        << ':' << transpose << ':' << startTick << ':' << endTick << ':' << phonemizer; return out.str();
}

std::shared_ptr<const RenderedAudio> RenderCache::get(const RenderKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) { ++misses_; return {}; }
    ++hits_; return it->second;
}

void RenderCache::put(const RenderKey& key, std::shared_ptr<const RenderedAudio> audio) {
    std::lock_guard<std::mutex> lock(mutex_);
    constexpr size_t kMaximumCacheBytes = 128u * 1024u * 1024u;
    const size_t incomingBytes = audio ? audio->samples.size() * sizeof(float) : 0;
    if (incomingBytes > kMaximumCacheBytes) return;
    if (auto existing = entries_.find(key); existing != entries_.end()) {
        bytes_ -= existing->second->samples.size() * sizeof(float);
        entries_.erase(existing);
    }
    while (!entries_.empty() && (entries_.size() >= 32 || bytes_ + incomingBytes > kMaximumCacheBytes)) {
        const auto victim = entries_.begin();
        bytes_ -= victim->second->samples.size() * sizeof(float);
        entries_.erase(victim);
    }
    entries_[key] = std::move(audio);
    bytes_ += incomingBytes;
}

void RenderCache::invalidateSinger(const std::string& singerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.singerId == singerId) {
            bytes_ -= it->second->samples.size() * sizeof(float);
            it = entries_.erase(it);
        } else ++it;
    }
}

void RenderCache::clear() { std::lock_guard<std::mutex> lock(mutex_); entries_.clear(); bytes_ = 0; }

RenderKey makeRenderKey(const VocalScore& score, const Voicebank& singer, const RenderOptions& options) {
    RenderKey key;
    key.scoreRevision = score.revision;
    key.scoreHash = std::hash<std::string>{}(scoreToJson(score));
    key.singerId = singer.stableId; key.singerRevision = singer.contentRevision; key.sampleRate = options.sampleRate;
    key.bpmMilli = static_cast<int>(std::llround((options.bpm > 0 ? options.bpm : score.nominalBpm) * 1000.0));
    key.transpose = options.transposeSemitones; key.startTick = options.startTick;
    key.endTick = options.endTick > options.startTick ? options.endTick : score.endTick(); key.phonemizer = options.phonemizer;
    return key;
}

RenderService& RenderService::instance() { static RenderService service; return service; }

static RenderedAudio renderChunked(const VocalScore& score, const Voicebank& singer, const RenderOptions& options,
                                   const std::atomic<bool>* canceled, std::atomic<uint64_t>& completedChunks,
                                   std::atomic<uint64_t>& lastChunkRenderMicros) {
    const auto began = std::chrono::steady_clock::now();
    const double bpm = options.bpm > 0.0 ? options.bpm : score.nominalBpm;
    const int64_t startTick = std::max<int64_t>(0, options.startTick);
    const int64_t endTick = options.endTick > startTick ? options.endTick : score.endTick();
    const int64_t chunkTicks = kTicksPerQuarter * std::max(1, score.beatsPerBar);
    const int64_t overlapTicks = kTicksPerQuarter / 20;  // 1/20 beat overlap tail.
    const auto framesForTicks = [&](int64_t ticks) {
        return static_cast<size_t>(std::llround(ticks * 60.0 * options.sampleRate / (bpm * kTicksPerQuarter)));
    };
    RenderedAudio assembled;
    assembled.sampleRate = options.sampleRate; assembled.bpm = bpm; assembled.startTick = startTick;
    assembled.endTick = endTick; assembled.scoreRevision = score.revision;
    if (!std::isfinite(bpm) || bpm < 20.0 || bpm > 400.0 || options.sampleRate < 8000 || options.sampleRate > 384000)
        throw std::runtime_error("Render settings are outside supported finite limits");
    constexpr size_t kMaximumRenderedFrames = 32u * 1024u * 1024u;
    const long double requestedFrames = std::ceil(static_cast<long double>(endTick - startTick) * 60.0L *
        options.sampleRate / (static_cast<long double>(bpm) * kTicksPerQuarter));
    if (requestedFrames < 0.0L || requestedFrames > static_cast<long double>(kMaximumRenderedFrames))
        throw std::runtime_error("Requested render exceeds the 128 MiB audio-buffer budget; render a shorter section");
    assembled.samples.assign(static_cast<size_t>(requestedFrames), 0.f);
    for (int64_t coreStart = startTick; coreStart < endTick; coreStart += chunkTicks) {
        if (canceled && canceled->load(std::memory_order_relaxed)) {
            assembled.samples.clear(); assembled.diagnostics.warnings.emplace_back("Render canceled"); return assembled;
        }
        const int64_t coreEnd = std::min(endTick, coreStart + chunkTicks);
        RenderOptions chunkOptions = options;
        chunkOptions.startTick = std::max(startTick, coreStart - overlapTicks);
        chunkOptions.endTick = std::min(endTick, coreEnd + overlapTicks);
        const auto chunkBegan = std::chrono::steady_clock::now();
        auto chunk = NativeV1Renderer{}.render(score, singer, chunkOptions, canceled);
        lastChunkRenderMicros.store(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - chunkBegan).count()), std::memory_order_relaxed);
        completedChunks.fetch_add(1, std::memory_order_relaxed);
        const size_t destination = framesForTicks(coreStart - startTick);
        const size_t source = framesForTicks(coreStart - chunkOptions.startTick);
        const size_t coreFrames = framesForTicks(coreEnd - coreStart);
        const size_t count = std::min({coreFrames, chunk.samples.size() > source ? chunk.samples.size() - source : 0,
            assembled.samples.size() > destination ? assembled.samples.size() - destination : 0});
        if (count) std::copy_n(chunk.samples.begin() + source, count, assembled.samples.begin() + destination);
        assembled.diagnostics.warnings.insert(assembled.diagnostics.warnings.end(), chunk.diagnostics.warnings.begin(), chunk.diagnostics.warnings.end());
        assembled.diagnostics.errors.insert(assembled.diagnostics.errors.end(), chunk.diagnostics.errors.begin(), chunk.diagnostics.errors.end());
        for (const auto& phoneme : chunk.diagnostics.phonemes)
            if (phoneme.relativeTick >= coreStart && phoneme.relativeTick < coreEnd) assembled.diagnostics.phonemes.push_back(phoneme);
    }
    std::sort(assembled.diagnostics.errors.begin(), assembled.diagnostics.errors.end());
    assembled.diagnostics.errors.erase(std::unique(assembled.diagnostics.errors.begin(), assembled.diagnostics.errors.end()), assembled.diagnostics.errors.end());
    assembled.diagnostics.renderSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    return assembled;
}

RenderService::RenderService() {
    const unsigned count = std::clamp(std::thread::hardware_concurrency(), 1u, 2u);
    for (unsigned i = 0; i < count; ++i) workers_.emplace_back([this] { workerLoop(); });
}

RenderService::~RenderService() { shutdown(); }

std::shared_ptr<RenderSlot> RenderService::createSlot() { return std::make_shared<RenderSlot>(); }

uint64_t RenderService::submit(const std::shared_ptr<RenderSlot>& slot, VocalScore score, Voicebank singer, RenderOptions options) {
    auto cancellation = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(slot->workerMutex);
        if (slot->currentCancellation)
            slot->currentCancellation->store(true, std::memory_order_release);
        slot->currentCancellation = cancellation;
    }
    const uint64_t generation = slot->requestedGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    slot->rendering.store(true, std::memory_order_release);
    slot->renderFailed.store(false, std::memory_order_release);
    const RenderKey key = makeRenderKey(score, singer, options);
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.push_back({[this, weak = std::weak_ptr<RenderSlot>(slot), generation, key, cancellation,
                          score = std::move(score), singer = std::move(singer), options = std::move(options)]() mutable {
            auto state = weak.lock();
            if (!state || !state->alive.load() || cancellation->load(std::memory_order_acquire)
                    || generation != state->requestedGeneration.load()) { ++canceled_; return; }
            ++active_;
            std::shared_ptr<const RenderedAudio> result = cache_.get(key);
            std::string failure;
            if (!result) {
                try {
                    auto rendered = std::make_shared<RenderedAudio>(renderChunked(score, singer, options, cancellation.get(),
                        completedChunks_, lastChunkRenderMicros_));
                    // Never let an obsolete generation poison the shared cache
                    // with a render whose canceled chunk was left as silence.
                    if (!cancellation->load(std::memory_order_acquire)
                            && generation == state->requestedGeneration.load(std::memory_order_acquire)
                            && rendered->samples.size()) {
                        cache_.put(key, rendered); result = std::move(rendered);
                    }
                } catch (const std::exception& e) {
                    failure = e.what();
                } catch (...) {
                    failure = "Unknown renderer exception";
                }
            }
            --active_;
            if (!state->alive.load() || cancellation->load(std::memory_order_acquire)
                    || generation != state->requestedGeneration.load(std::memory_order_acquire)) {
                ++canceled_;
                // A newer generation owns the slot's rendering flag.
                return;
            }
            if (result) {
                state->publishAudio(result, result->diagnostics.errors.empty() ? "" : result->diagnostics.errors.front());
                state->completedGeneration.store(generation, std::memory_order_release);
                state->publishedRequestSerial.store(options.requestSerial, std::memory_order_release);
                state->renderFailed.store(false, std::memory_order_release);
                ++completed_;
            } else if (!failure.empty()) {
                {
                    std::lock_guard<std::mutex> lock(state->workerMutex);
                    state->lastError = std::move(failure);
                }
                state->renderFailed.store(true, std::memory_order_release);
            }
            state->rendering.store(false, std::memory_order_release);
        }});
    }
    queueCv_.notify_one();
    return generation;
}

void RenderService::cancel(const std::shared_ptr<RenderSlot>& slot) {
    if (!slot) return;
    slot->alive.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(slot->workerMutex);
    if (slot->currentCancellation)
        slot->currentCancellation->store(true, std::memory_order_release);
}

void RenderService::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            job = std::move(queue_.front()); queue_.pop_front();
        }
        try { job.run(); } catch (...) { --active_; }
    }
}

RenderServiceStats RenderService::stats() const noexcept {
    return {active_.load(), canceled_.load(), completed_.load(), cache_.hits(), cache_.misses(),
        decodedSampleBytes(), completedChunks_.load(), lastChunkRenderMicros_.load()};
}

void RenderService::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (stopping_) return;
        stopping_ = true;
        queue_.clear();
    }
    queueCv_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
    workers_.clear(); cache_.clear();
}

}  // namespace vocalrack

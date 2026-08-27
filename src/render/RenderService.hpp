#pragma once

#include "NativeV1Renderer.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace vocalrack {

struct RenderKey {
    uint64_t scoreRevision = 0;
    uint64_t scoreHash = 0;
    std::string singerId;
    uint64_t singerRevision = 0;
    uint32_t sampleRate = 48000;
    int bpmMilli = 120000;
    int transpose = 0;
    int64_t startTick = 0;
    int64_t endTick = 0;
    std::string phonemizer;

    bool operator==(const RenderKey& other) const noexcept;
    std::string toString() const;
};

struct RenderKeyHash {
    size_t operator()(const RenderKey& key) const noexcept;
};

class RenderCache {
public:
    std::shared_ptr<const RenderedAudio> get(const RenderKey& key);
    void put(const RenderKey& key, std::shared_ptr<const RenderedAudio> audio);
    void invalidateSinger(const std::string& singerId);
    void clear();
    uint64_t hits() const noexcept { return hits_.load(); }
    uint64_t misses() const noexcept { return misses_.load(); }
private:
    std::mutex mutex_;
    std::unordered_map<RenderKey, std::shared_ptr<const RenderedAudio>, RenderKeyHash> entries_;
    size_t bytes_ = 0;
    std::atomic<uint64_t> hits_{0}, misses_{0};
};

struct RenderSlot {
    std::atomic<const RenderedAudio*> current{nullptr};
    std::atomic<uint64_t> requestedGeneration{0};
    std::atomic<uint64_t> completedGeneration{0};
    std::atomic<uint64_t> publishedRequestSerial{0};
    std::atomic<bool> rendering{false};
    std::atomic<bool> renderFailed{false};
    std::atomic<bool> alive{true};
    std::atomic<uint64_t> underruns{0};
    std::atomic<const RenderedAudio*> rtReader{nullptr};
    mutable std::mutex workerMutex;
    // Cancellation belongs to one submitted generation. A single reusable
    // boolean is unsafe: a newer submission must clear its own token, which
    // can accidentally "uncancel" an older renderer before that worker has
    // observed the request.
    std::shared_ptr<std::atomic<bool>> currentCancellation;
    std::shared_ptr<const RenderedAudio> currentOwner;
    std::vector<std::shared_ptr<const RenderedAudio>> retiredOwners;
    std::string lastError;

    const RenderedAudio* audio() const noexcept { return current.load(std::memory_order_acquire); }
    const RenderedAudio* acquireAudioRt() noexcept;
    void releaseAudioRt() noexcept { rtReader.store(nullptr, std::memory_order_release); }
    void publishAudio(std::shared_ptr<const RenderedAudio> audio, std::string error);
    void clearAudio();
    RenderDiagnostics copyDiagnostics() const;
    size_t retainedBufferCount() const;
};

struct RenderServiceStats {
    uint64_t activeJobs = 0;
    uint64_t canceledJobs = 0;
    uint64_t completedJobs = 0;
    uint64_t cacheHits = 0;
    uint64_t cacheMisses = 0;
    uint64_t decodedSampleBytes = 0;
    uint64_t completedChunks = 0;
    uint64_t lastChunkRenderMicros = 0;
};

class RenderService {
public:
    static RenderService& instance();
    std::shared_ptr<RenderSlot> createSlot();
    uint64_t submit(const std::shared_ptr<RenderSlot>& slot, VocalScore score, Voicebank singer, RenderOptions options);
    void cancel(const std::shared_ptr<RenderSlot>& slot);
    RenderServiceStats stats() const noexcept;
    void shutdown();
    ~RenderService();

private:
    RenderService();
    struct Job { std::function<void()> run; };
    void workerLoop();
    mutable std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<Job> queue_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
    RenderCache cache_;
    std::atomic<uint64_t> active_{0}, canceled_{0}, completed_{0};
    std::atomic<uint64_t> completedChunks_{0}, lastChunkRenderMicros_{0};
};

RenderKey makeRenderKey(const VocalScore& score, const Voicebank& singer, const RenderOptions& options);

}  // namespace vocalrack

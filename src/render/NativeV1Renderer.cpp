#include "NativeV1Renderer.hpp"
#include "core/PitchModel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <tuple>
#include <unordered_map>

namespace vocalrack {

static constexpr double kPi = 3.14159265358979323846;
// Pinned Classic renders its score-aligned amplitude features a few
// milliseconds ahead of the otherwise equivalent native result. A complete
// -4..+4 ms sweep over all 543 corpus boundaries selects 4 ms as the only
// global value in that range that satisfies every predeclared subgroup gate
// (including continuation and pitch stress) without any per-phoneme special
// case. This is render-time score alignment, not Rack transport latency.
static constexpr double kClassicOutputAdvanceMs = 4.0;

struct SampleCache {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<AudioBuffer>> samples;
    std::deque<std::string> insertionOrder;
    uint64_t retainedBytes = 0;
    std::shared_ptr<AudioBuffer> get(const std::filesystem::path& path) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) throw std::runtime_error("Unable to stat WAV " + path.string());
        const auto stamp = std::filesystem::last_write_time(path, ec);
        if (ec) throw std::runtime_error("Unable to stat WAV timestamp " + path.string());
        const auto key = path.lexically_normal().string() + ":" + std::to_string(size) + ":" +
            std::to_string(static_cast<long long>(stamp.time_since_epoch().count()));
        { std::lock_guard<std::mutex> lock(mutex); if (auto it = samples.find(key); it != samples.end()) return it->second; }
        auto decoded = std::make_shared<AudioBuffer>(readWavMono(path));
        std::lock_guard<std::mutex> lock(mutex);
        if (auto existing = samples.find(key); existing != samples.end()) return existing->second;
        constexpr uint64_t kMaximumDecodedBytes = 128u * 1024u * 1024u;
        const uint64_t decodedBytes = decoded->samples.size() * sizeof(float);
        if (decodedBytes > kMaximumDecodedBytes) return decoded;
        while (!insertionOrder.empty() && retainedBytes + decodedBytes > kMaximumDecodedBytes) {
            const auto victimKey = std::move(insertionOrder.front());
            insertionOrder.pop_front();
            if (auto victim = samples.find(victimKey); victim != samples.end()) {
                retainedBytes -= victim->second->samples.size() * sizeof(float);
                samples.erase(victim);
            }
        }
        insertionOrder.push_back(key);
        retainedBytes += decodedBytes;
        return samples.emplace(key, decoded).first->second;
    }
    uint64_t bytes() {
        std::lock_guard<std::mutex> lock(mutex);
        return retainedBytes;
    }
};

static SampleCache& sampleCache() { static SampleCache cache; return cache; }

uint64_t decodedSampleBytes() { return sampleCache().bytes(); }

static float interpolated(const AudioBuffer& audio, double pos) {
    if (audio.samples.empty()) return 0.f;
    pos = std::clamp(pos, 0.0, static_cast<double>(audio.samples.size() - 1));
    const auto i = static_cast<size_t>(pos);
    const auto j = std::min(i + 1, audio.samples.size() - 1);
    const float a = static_cast<float>(pos - i);
    return audio.samples[i] + (audio.samples[j] - audio.samples[i]) * a;
}

static double tickToSeconds(int64_t ticks, double bpm) { return ticks * 60.0 / (bpm * kTicksPerQuarter); }

static bool beginsWithUnvoicedJapaneseConsonant(const std::string& alias) {
    // VCV aliases begin with a romanized vowel plus a space and should enter
    // pitch synthesis at their overlap handoff. For ordinary CV aliases,
    // preserve the bank's fixed unvoiced consonant through oto.consonant.
    if (alias.find(' ') != std::string::npos) return false;
    static const char* prefixes[] = {
        "か", "き", "く", "け", "こ",
        "さ", "し", "す", "せ", "そ",
        "た", "ち", "つ", "て", "と",
        "は", "ひ", "ふ", "へ", "ほ",
        "ぱ", "ぴ", "ぷ", "ぺ", "ぽ",
    };
    for (const char* prefix : prefixes) {
        if (alias.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

RenderedAudio NativeV1Renderer::render(const VocalScore& score, const Voicebank& singer, const RenderOptions& options,
                                       const std::atomic<bool>* canceled) const {
    const auto began = std::chrono::steady_clock::now();
    RenderedAudio out;
    out.sampleRate = std::clamp<uint32_t>(options.sampleRate, 8000, 192000);
    out.bpm = options.bpm > 0.0 ? options.bpm : score.nominalBpm;
    out.startTick = std::max<int64_t>(0, options.startTick);
    out.endTick = options.endTick > out.startTick ? options.endTick : score.endTick();
    out.scoreRevision = score.revision;
    if (!singer.valid()) {
        out.diagnostics.errors.emplace_back("Singer missing or invalid");
        return out;
    }
    if (out.endTick <= out.startTick) return out;
    const auto totalFrames = static_cast<size_t>(std::ceil(tickToSeconds(out.endTick - out.startTick, out.bpm) * out.sampleRate));
    out.samples.assign(totalFrames, 0.f);
    const auto phonemizerName = canonicalPhonemizerName(options.phonemizer);
    auto phonemizer = makePhonemizer(phonemizerName);

    for (size_t noteIndex = 0; noteIndex < score.notes.size(); ++noteIndex) {
        if (canceled && canceled->load(std::memory_order_relaxed)) {
            out.diagnostics.warnings.emplace_back("Render canceled"); out.samples.clear(); return out;
        }
        const auto& note = score.notes[noteIndex];
        if (note.durationTick <= 0) continue;
        size_t chainEndIndex = noteIndex;
        while (chainEndIndex + 1 < score.notes.size()) {
            const auto& candidate = score.notes[chainEndIndex + 1];
            const auto& prior = score.notes[chainEndIndex];
            if (candidate.startTick != prior.endTick() || candidate.aliasOverride ||
                !candidate.phonemeOverrides.empty() || !lyricIsExtender(candidate.lyric)) break;
            ++chainEndIndex;
        }
        const auto& chainLast = score.notes[chainEndIndex];
        const int64_t logicalEndTick = chainLast.endTick();
        const Note* previous = noteIndex ? &score.notes[noteIndex - 1] : nullptr;
        // English continuation notes belong to the preceding lexical note.
        // When a new word follows +/+* notes, pass a duration-extended copy of
        // that lexical root so connected-word coda transfer still sees the
        // real previous lyric and the immediate boundary.
        std::optional<Note> previousEnglishChain;
        if (phonemizerName == kEnglishToJapanesePhonemizer && previous &&
            lyricIsExtender(previous->lyric)) {
            size_t rootIndex = noteIndex - 1;
            while (rootIndex > 0 && lyricIsExtender(score.notes[rootIndex].lyric))
                --rootIndex;
            if (!lyricIsExtender(score.notes[rootIndex].lyric)) {
                previousEnglishChain = score.notes[rootIndex];
                previousEnglishChain->durationTick =
                    score.notes[noteIndex - 1].endTick() - previousEnglishChain->startTick;
                previous = &*previousEnglishChain;
            }
        }
        const Note* next = chainEndIndex + 1 < score.notes.size() ? &score.notes[chainEndIndex + 1] : nullptr;
        std::vector<Note> phonemizerNotes;
        phonemizerNotes.reserve(chainEndIndex - noteIndex + 1);
        for (size_t chainIndex = noteIndex; chainIndex <= chainEndIndex; ++chainIndex)
            phonemizerNotes.push_back(score.notes[chainIndex]);
        auto phones = phonemizer->processAllChain(phonemizerNotes, previous, next, singer);
        if (!note.phonemeOverrides.empty()) {
            const size_t eventCount = std::max(phones.size(), note.phonemeOverrides.size());
            phones.resize(eventCount);
            for (size_t phoneIndex = 0; phoneIndex < eventCount; ++phoneIndex) {
                auto& phone = phones[phoneIndex];
                if (phone.sourceNoteId.empty()) {
                    phone.relativeTick = note.startTick + static_cast<int64_t>(
                        static_cast<long double>(note.durationTick) * phoneIndex /
                        std::max<size_t>(1, eventCount));
                    phone.targetMidiNote = note.midiNote;
                    phone.sourceNoteId = note.id;
                }
                if (phoneIndex >= note.phonemeOverrides.size() ||
                    note.phonemeOverrides[phoneIndex].empty()) continue;
                const auto& alias = note.phonemeOverrides[phoneIndex];
                phone.requestedAlias = alias;
                phone.selectedAlias.clear();
                phone.oto = singer.findAlias(alias, note.midiNote);
                phone.attempts = {{alias, phone.oto != nullptr}};
                phone.diagnostic = phone.oto
                    ? "Indexed phoneme override selected " + phone.oto->alias
                    : "Indexed phoneme override missing: " + alias;
                if (phone.oto) phone.selectedAlias = phone.oto->alias;
            }
        }
        if (phones.empty()) {
            out.diagnostics.errors.push_back("Phonemizer produced no aliases for lyric " + note.lyric +
                                             " at tick " + std::to_string(note.startTick));
            noteIndex = chainEndIndex;
            continue;
        }
        const auto sourceNoteFor = [&](const PhonemeEvent& event) -> const Note* {
            const auto found = std::find_if(
                score.notes.begin() + static_cast<std::ptrdiff_t>(noteIndex),
                score.notes.begin() + static_cast<std::ptrdiff_t>(chainEndIndex + 1),
                [&](const Note& candidate) { return candidate.id == event.sourceNoteId; });
            return found == score.notes.begin() + static_cast<std::ptrdiff_t>(chainEndIndex + 1)
                ? &note : &*found;
        };
        std::unordered_map<std::string, size_t> sourcePhoneIndices;
        for (auto& phone : phones) {
            const Note* sourceNote = sourceNoteFor(phone);
            const size_t sourcePhoneIndex = sourcePhoneIndices[phone.sourceNoteId]++;
            phone.automaticRelativeTick = phone.relativeTick;
            phone.relativeTick = adjustedInternalPhonemeTick(
                *sourceNote, sourcePhoneIndex, phone.relativeTick);
        }
        for (size_t phoneIndex = 0; phoneIndex < phones.size(); ++phoneIndex) {
        auto phone = phones[phoneIndex];
        const Note* eventNote = sourceNoteFor(phone);
        const bool firstPhoneForNote = phoneIndex == 0 ||
            phones[phoneIndex - 1].sourceNoteId != phone.sourceNoteId;
        const bool lastPhoneForNote = phoneIndex + 1 == phones.size() ||
            phones[phoneIndex + 1].sourceNoteId != phone.sourceNoteId;
        const int64_t timingOffset = eventNote->phonemeTiming.positionOffsetTick.value_or(0);
        const int64_t nextTimingOffset = phoneIndex + 1 < phones.size()
            ? sourceNoteFor(phones[phoneIndex + 1])->phonemeTiming.positionOffsetTick.value_or(0)
            : 0;
        const int64_t phoneEndTick = phoneIndex + 1 < phones.size()
            ? std::max<int64_t>(phone.relativeTick + timingOffset + 1,
                phones[phoneIndex + 1].relativeTick + nextTimingOffset)
            : logicalEndTick;
        if (!phone.oto) {
            out.diagnostics.phonemes.push_back(phone);
            out.diagnostics.errors.push_back(phone.diagnostic + " at tick " + std::to_string(phone.relativeTick));
            continue;
        }
        std::shared_ptr<AudioBuffer> source;
        try { source = sampleCache().get(phone.oto->wavPath); }
        catch (const std::exception& e) {
            out.diagnostics.phonemes.push_back(phone);
            out.diagnostics.errors.push_back(e.what());
            continue;
        }
        if (source->samples.size() < 32) {
            out.diagnostics.phonemes.push_back(phone);
            continue;
        }
        const double srRatio = source->sampleRate / static_cast<double>(out.sampleRate);
        const double offset = std::clamp(phone.oto->offsetMs * source->sampleRate / 1000.0, 0.0, static_cast<double>(source->samples.size() - 2));
        double end = phone.oto->cutoffMs >= 0.0
            ? source->samples.size() - phone.oto->cutoffMs * source->sampleRate / 1000.0
            : offset - phone.oto->cutoffMs * source->sampleRate / 1000.0;
        end = std::clamp(end, offset + 16.0, static_cast<double>(source->samples.size() - 1));
        const double fixedEnd = std::clamp(offset + phone.oto->consonantMs * source->sampleRate / 1000.0, offset + 8.0, end - 8.0);
        const double loopEnd = std::max(fixedEnd + 8.0, end - source->sampleRate * 0.015);
        const double loopStart = std::clamp(std::max(fixedEnd, loopEnd - source->sampleRate * 0.18), fixedEnd, loopEnd - 4.0);
        const double sourcePitch = [&] {
            const double metadataPitch = singer.sourcePitchFor(phone.oto->wavPath);
            if (metadataPitch >= 50.0 && metadataPitch <= 1200.0) return metadataPitch;
            const double f = estimateFundamental(*source, static_cast<size_t>(loopStart), static_cast<size_t>(loopEnd));
            return f >= 70.0 && f <= 600.0 ? f : 277.1826;
        }();
        phone.sourcePitchHz = sourcePitch;
        const size_t diagnosticPhoneIndex = out.diagnostics.phonemes.size();
        out.diagnostics.phonemes.push_back(phone);
        std::vector<size_t> continuationDiagnosticIndices;
        // Emit a diagnostic for an extender only when this event is the one
        // sounding at that note line, and the phonemizer did not put
        // a new event there. This keeps audit logs honest for multi-syllable
        // English chains while retaining explicit coverage for true holds.
        for (size_t tiedIndex = noteIndex + 1; tiedIndex <= chainEndIndex; ++tiedIndex) {
            const auto& tiedNote = score.notes[tiedIndex];
            size_t activePhoneIndex = 0;
            for (size_t candidateIndex = 0; candidateIndex < phones.size(); ++candidateIndex) {
                if (phones[candidateIndex].relativeTick <= tiedNote.startTick)
                    activePhoneIndex = candidateIndex;
            }
            const bool hasNewEventAtLine = std::any_of(
                phones.begin(), phones.end(), [&](const PhonemeEvent& candidate) {
                    return candidate.sourceNoteId == tiedNote.id &&
                           candidate.relativeTick <= tiedNote.startTick;
                });
            if (activePhoneIndex != phoneIndex || hasNewEventAtLine) continue;
            PhonemeEvent continuation = phone;
            continuation.relativeTick = tiedNote.startTick;
            continuation.targetMidiNote = tiedNote.midiNote;
            continuation.sourceNoteId = tiedNote.id;
            continuation.requestedAlias = tiedNote.lyric;
            continuation.diagnostic = "Continued " + phone.selectedAlias + " without re-articulation";
            continuationDiagnosticIndices.push_back(out.diagnostics.phonemes.size());
            out.diagnostics.phonemes.push_back(std::move(continuation));
        }

        // UTAU's preutterance is the time between the usable sample start and
        // the musical note boundary. Start the phoneme early so its consonant
        // lands at the boundary instead of being swallowed by an attack fade.
        // The first note cannot begin before tick zero, so it starts at zero
        // and reaches its vowel slightly after the transport starts.
        const double preutterMs = std::clamp(phone.oto->preutterMs +
            (firstPhoneForNote ? eventNote->phonemeTiming.preutteranceDeltaMs.value_or(0.f) : 0.f),
            0.0, 500.0);
        const double overlapMs = std::clamp(phone.oto->overlapMs +
            (firstPhoneForNote ? eventNote->phonemeTiming.overlapDeltaMs.value_or(0.f) : 0.f),
            -500.0, preutterMs);
        const int64_t phonemePositionTick = phone.relativeTick + timingOffset;
        const int64_t preutterTick = static_cast<int64_t>(std::llround(
            preutterMs * out.bpm * kTicksPerQuarter / 60000.0));
        // Keep the true (possibly negative) preutterance origin. When the
        // score begins at tick zero, the inaudible part before the render
        // range is skipped rather than restarting the phoneme's attack at
        // zero. This matches UTAU/OpenUtau's render-then-crop behavior.
        const int64_t eventStartTick = phonemePositionTick - preutterTick;
        if (phoneEndTick <= out.startTick || eventStartTick >= out.endTick) continue;
        const int64_t clippedStartTick = std::max(eventStartTick, out.startTick);
        const int64_t clippedEndTick = std::min(phoneEndTick, out.endTick);
        const size_t beginFrame = static_cast<size_t>(std::llround(tickToSeconds(clippedStartTick - out.startTick, out.bpm) * out.sampleRate));
        const size_t endFrame = std::min(out.samples.size(), static_cast<size_t>(std::llround(
            tickToSeconds(clippedEndTick - out.startTick, out.bpm) * out.sampleRate)));
        const size_t eventFrames = std::max<size_t>(1, static_cast<size_t>(std::llround(
            tickToSeconds(std::max<int64_t>(1, phoneEndTick - eventStartTick), out.bpm) * out.sampleRate)));
        const size_t eventOffsetFrames = static_cast<size_t>(std::llround(
            tickToSeconds(clippedStartTick - eventStartTick, out.bpm) * out.sampleRate));
        const size_t boundaryFrames = static_cast<size_t>(std::llround(
            tickToSeconds(phonemePositionTick - eventStartTick, out.bpm) * out.sampleRate));
        // OpenUtau's classic envelope uses a 5 ms attack unless this phoneme
        // positively overlaps an adjacent predecessor; in that case the
        // overlap duration is the crossfade attack.
        const bool hasAdjacentPredecessor = phoneIndex > 0 ||
            (previous && previous->endTick() == note.startTick);
        const bool overlapsPrevious = hasAdjacentPredecessor && overlapMs > 0.0;
        const bool nonOverlappingConsonant = hasAdjacentPredecessor && overlapMs <= 0.0;
        const bool preserveUnvoicedConsonant = beginsWithUnvoicedJapaneseConsonant(
            phone.selectedAlias);
        const double attackMs = overlapsPrevious
            ? std::clamp(overlapMs, 0.0, preutterMs)
            : 5.0;
        const double adjustedAttackMs = std::clamp(attackMs +
            (firstPhoneForNote ? eventNote->phonemeTiming.attackTimeDeltaMs.value_or(0.f) : 0.f),
            0.0, 500.0);
        const size_t attackFrames = std::max<size_t>(16, static_cast<size_t>(
            adjustedAttackMs * out.sampleRate / 1000.0));
        size_t envelopeEndFrames = eventFrames;
        size_t releaseFrames = std::max<size_t>(16, static_cast<size_t>(0.010 * out.sampleRate));
        PhonemeEvent handoffPhone;
        const Note* handoffNote = nullptr;
        bool handoffIsFirstPhone = false;
        if (phoneIndex + 1 < phones.size()) {
            handoffPhone = phones[phoneIndex + 1];
            handoffNote = sourceNoteFor(handoffPhone);
            handoffIsFirstPhone = handoffPhone.sourceNoteId != phone.sourceNoteId;
        } else if (next) {
            const Note* following = chainEndIndex + 2 < score.notes.size() ? &score.notes[chainEndIndex + 2] : nullptr;
            auto followingPhones = phonemizer->processAll(*next, &chainLast, following, singer);
            if (!followingPhones.empty()) {
                handoffPhone = std::move(followingPhones.front());
                handoffNote = next;
                handoffIsFirstPhone = true;
            }
        }
        if (handoffNote && handoffPhone.oto) {
            // Match UTAU's phoneme-envelope handoff: the preceding vowel fades
            // out before the following phoneme's preutterance, leaving room for
            // low-energy consonants instead of masking them until the note line.
                const double nextPreutterMs = std::clamp(handoffPhone.oto->preutterMs +
                    (handoffIsFirstPhone ? handoffNote->phonemeTiming.preutteranceDeltaMs.value_or(0.f) : 0.f),
                    0.0, 500.0);
                const double nextOverlapMs = std::clamp(handoffPhone.oto->overlapMs +
                    (handoffIsFirstPhone ? handoffNote->phonemeTiming.overlapDeltaMs.value_or(0.f) : 0.f),
                    -500.0, nextPreutterMs);
                const int64_t nextPreutterTick = static_cast<int64_t>(std::llround(
                    nextPreutterMs * out.bpm * kTicksPerQuarter / 60000.0));
                const int64_t nextOverlapTick = static_cast<int64_t>(std::llround(
                    nextOverlapMs * out.bpm * kTicksPerQuarter / 60000.0));
                const int64_t nextPositionTick = handoffPhone.relativeTick +
                    handoffNote->phonemeTiming.positionOffsetTick.value_or(0);
                const int64_t handoffTick = nextPositionTick - nextPreutterTick + nextOverlapTick;
                const double handoffSeconds = tickToSeconds(handoffTick - eventStartTick, out.bpm);
                envelopeEndFrames = static_cast<size_t>(std::clamp<double>(
                    std::llround(handoffSeconds * out.sampleRate), 0.0, static_cast<double>(eventFrames)));
                const double releaseMs = std::clamp((nextOverlapMs > 0.0 ? nextOverlapMs : 35.0) +
                    (lastPhoneForNote
                        ? eventNote->phonemeTiming.releaseTimeDeltaMs.value_or(0.f) : 0.f),
                    0.0, 500.0);
                releaseFrames = std::max<size_t>(16, static_cast<size_t>(
                    releaseMs * out.sampleRate / 1000.0));
        } else if (lastPhoneForNote) {
            const double releaseMs = std::clamp(
                10.0 + eventNote->phonemeTiming.releaseTimeDeltaMs.value_or(0.f),
                0.0, 500.0);
            releaseFrames = std::max<size_t>(16, static_cast<size_t>(
                releaseMs * out.sampleRate / 1000.0));
        }

        const auto notePosition = [&](size_t eventFrame) {
            const int64_t absoluteTick = eventStartTick + static_cast<int64_t>(std::llround(
                eventFrame * out.bpm * kTicksPerQuarter / (60.0 * out.sampleRate)));
            size_t activeIndex = noteIndex;
            while (activeIndex < chainEndIndex && absoluteTick >= score.notes[activeIndex].endTick()) ++activeIndex;
            const Note* activeNote = &score.notes[activeIndex];
            const int64_t tickOffset = absoluteTick - activeNote->startTick;
            const double progress = std::clamp(tickOffset / static_cast<double>(
                std::max<int64_t>(1, activeNote->durationTick)), 0.0, 1.0);
            return std::tuple<const Note*, double, int64_t>{activeNote, progress, tickOffset};
        };
        const auto targetFrequency = [&](size_t eventFrame) {
            const int64_t absoluteTick = eventStartTick + static_cast<int64_t>(std::llround(
                eventFrame * out.bpm * kTicksPerQuarter / (60.0 * out.sampleRate)));
            const double midi = performedAbsoluteMidi(score, absoluteTick, out.bpm) +
                options.transposeSemitones;
            return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
        };
        const auto pitchedIncrement = [&](size_t eventFrame) {
            return srRatio * targetFrequency(eventFrame) / sourcePitch;
        };

        const size_t naturalFrames = static_cast<size_t>(std::ceil((fixedEnd - offset) / srRatio));
        // UTAU's fixed region protects consonant timing, but voiced material
        // at the note line is still pitch-corrected by the resampler. Begin
        // PSOLA at the musical boundary (5 ms later for plosives so their
        // unvoiced release remains untouched) instead of leaving the first
        // 20-60 ms of each vowel at the bank's recorded source pitch.
        const size_t pitchStartFrames = std::min(naturalFrames,
            preserveUnvoicedConsonant
                ? naturalFrames
                : overlapsPrevious
                ? attackFrames
                : boundaryFrames + (nonOverlappingConsonant
                    ? static_cast<size_t>(0.005 * out.sampleRate) : 0));
        const size_t renderedFrames = endFrame > beginFrame ? endFrame - beginFrame : 0;
        // Pitch-synchronous overlap/add keeps the source's formant envelope
        // while moving pitch periods to the authored target. This avoids the
        // chipmunk/keyboard timbre of changing the whole sample playback rate.
        // Epochs are stepped from the event origin even for clipped chunks, so
        // arbitrary seek/rerender boundaries remain deterministic.
        const double sourcePeriod = source->sampleRate / sourcePitch;
        const bool psolaUsable = loopEnd - loopStart >= sourcePeriod * 4.0 && renderedFrames > 0;
        const size_t representativeFrame = std::min(eventFrames - 1,
            boundaryFrames + (eventFrames - boundaryFrames) / 2);
        const double representativePitch = targetFrequency(representativeFrame);
        // At large downward transpositions, source-period-sized grains become
        // shorter than the spacing between synthesis epochs and leave holes.
        // Widen only those grains enough to overlap at the target period. This
        // retains several real source cycles (and therefore the recorded
        // harmonic/formant texture) instead of stretching a single cycle into
        // a smooth, second-harmonic-dominant waveform.
        const double sourceHalfGrain = sourcePeriod / srRatio;
        const double targetHalfGrain = out.sampleRate
            / std::clamp(representativePitch, 40.0, 2000.0) * 0.55;
        const int halfGrain = std::max(8, static_cast<int>(std::ceil(
            std::max(sourceHalfGrain, targetHalfGrain))));
        // Allocate only the requested slice plus one grain of context. Epochs
        // still advance from the absolute event origin below, so rendering a
        // slice remains deterministic without allocating the entire note.
        const size_t sliceContext = static_cast<size_t>(halfGrain);
        const size_t localBufferStart = eventOffsetFrames > sliceContext
            ? eventOffsetFrames - sliceContext : 0;
        const size_t requestedEnd = eventOffsetFrames > eventFrames - std::min(eventFrames, renderedFrames)
            ? eventFrames : eventOffsetFrames + renderedFrames;
        const size_t localBufferEnd = std::min(eventFrames,
            requestedEnd > eventFrames - std::min(eventFrames, sliceContext)
                ? eventFrames : requestedEnd + sliceContext);
        const size_t localBufferFrames = localBufferEnd > localBufferStart
            ? localBufferEnd - localBufferStart : 0;
        std::vector<float> vowel(localBufferFrames, 0.f);
        std::vector<float> vowelWeight(localBufferFrames, 0.f);
        if (psolaUsable && eventFrames > pitchStartFrames) {
            // Anchor the first stretched grain to the corresponding sample at
            // the note-line handoff. After the one-time consonant/vowel lead-in
            // is consumed, ping-pong only over the stable oto vowel region.
            // Uniform spacing avoids the buzz caused by per-cycle peak snaps.
            const double sourceCycleStart = offset + pitchStartFrames * srRatio;
            const double sourceCycleEnd = loopEnd - sourcePeriod;
            const size_t sourceCycleCount = std::max<size_t>(1, static_cast<size_t>(
                std::floor(std::max(0.0, sourceCycleEnd - sourceCycleStart) / sourcePeriod) + 1));
            const size_t stableCycleIndex = std::min(sourceCycleCount - 1, static_cast<size_t>(std::max(
                0.0, std::ceil((loopStart - sourceCycleStart) / sourcePeriod))));
            size_t sourceCycleIndex = 0;
            int sourceCycleDirection = 1;
            double sourceCenter = sourceCycleStart;
            double synthesisEpoch = static_cast<double>(pitchStartFrames);
            size_t epochGuard = 0;
            while (synthesisEpoch - halfGrain < static_cast<double>(eventFrames)
                   && epochGuard++ < eventFrames * 2 + 4096) {
                const int64_t centerFrame = static_cast<int64_t>(std::llround(synthesisEpoch));
                for (int relative = -halfGrain; relative <= halfGrain; ++relative) {
                    const int64_t eventFrame = centerFrame + relative;
                    if (eventFrame < static_cast<int64_t>(localBufferStart) ||
                        eventFrame >= static_cast<int64_t>(localBufferEnd)) continue;
                    const size_t destination = static_cast<size_t>(eventFrame) - localBufferStart;
                    const double phase = relative / static_cast<double>(halfGrain);
                    const float window = static_cast<float>(0.5 + 0.5 * std::cos(kPi * phase));
                    vowel[destination] += interpolated(*source, sourceCenter + relative * srRatio) * window;
                    vowelWeight[destination] += window;
                }
                const size_t epochFrame = static_cast<size_t>(std::clamp<double>(
                    synthesisEpoch, 0.0, static_cast<double>(eventFrames - 1)));
                synthesisEpoch += out.sampleRate / std::clamp(targetFrequency(epochFrame), 40.0, 2000.0);
                if (sourceCycleCount > 1) {
                    if (sourceCycleDirection > 0 && sourceCycleIndex + 1 >= sourceCycleCount) {
                        sourceCycleDirection = -1;
                        if (sourceCycleIndex > stableCycleIndex) --sourceCycleIndex;
                    } else if (sourceCycleDirection < 0 && sourceCycleIndex <= stableCycleIndex) {
                        sourceCycleDirection = 1;
                        if (sourceCycleIndex + 1 < sourceCycleCount) ++sourceCycleIndex;
                    } else {
                        sourceCycleIndex = static_cast<size_t>(
                            static_cast<int64_t>(sourceCycleIndex) + sourceCycleDirection);
                    }
                    sourceCenter = sourceCycleStart + sourceCycleIndex * sourcePeriod;
                }
            }
        }

        const size_t vowelCrossfadeFrames = std::max<size_t>(16, static_cast<size_t>(0.005 * out.sampleRate));
        // Dense overlap at pitches above the recorded F0 averages several
        // slightly different source cycles and otherwise loses level. Restore
        // that predictable OLA attenuation without touching consonants or
        // ordinary near-source pitches.
        const float pitchedGain = static_cast<float>(std::clamp(
            representativePitch / sourcePitch, 1.0, 1.9));
        std::vector<float> eventAudio(renderedFrames, 0.f);
        for (size_t sliceFrame = 0; sliceFrame < renderedFrames; ++sliceFrame) {
            const size_t eventFrame = eventOffsetFrames + sliceFrame;
            if (eventFrame >= eventFrames) break;
            const auto positioned = notePosition(eventFrame);
            const Note* activeNote = std::get<0>(positioned);
            const int64_t tickOffset = std::get<2>(positioned);
            float sample = 0.f;
            if (psolaUsable) {
                const float natural = interpolated(*source, offset + eventFrame * srRatio);
                // vowel/weight is a convex weighted average of normalized
                // source samples and is therefore mathematically inside
                // [-1, 1].  Near a short event's PSOLA edge both floats can
                // become tiny enough for rounding error to create a huge
                // quotient.  That single impulse can charge the following DC
                // blocker and pin the limiter for hundreds of milliseconds.
                // Enforce the invariant before applying the intentional pitch
                // compensation gain.
                const size_t localIndex = eventFrame - localBufferStart;
                const float reconstructed = localIndex < vowelWeight.size() && vowelWeight[localIndex] > 1e-5f
                    ? std::clamp(vowel[localIndex] / vowelWeight[localIndex], -1.f, 1.f)
                    : natural;
                const float pitched = reconstructed * pitchedGain;
                const float blend = eventFrame <= pitchStartFrames ? 0.f : std::min(
                    1.f, (eventFrame - pitchStartFrames) / static_cast<float>(vowelCrossfadeFrames));
                sample = natural + (pitched - natural) * blend;
            } else {
                const size_t fixedFrames = static_cast<size_t>(std::ceil((fixedEnd - offset) / srRatio));
                double sourcePosition = offset + std::min(eventFrame, fixedFrames) * srRatio;
                if (eventFrame > fixedFrames) {
                    sourcePosition = fixedEnd + (eventFrame - fixedFrames) * pitchedIncrement(eventFrame);
                    if (sourcePosition >= loopEnd)
                        sourcePosition = loopStart + std::fmod(sourcePosition - loopStart,
                            std::max(4.0, loopEnd - loopStart));
                }
                sample = interpolated(*source, sourcePosition);
            }
            const size_t remaining = envelopeEndFrames > eventFrame ? envelopeEndFrames - eventFrame : 0;
            float envelope = std::min(1.f, eventFrame / static_cast<float>(attackFrames));
            envelope *= std::min(1.f, remaining / static_cast<float>(releaseFrames));
            const float gain = std::pow(10.f, activeNote->dynamicsDb.sample(tickOffset, 0.f) / 20.f);
            eventAudio[sliceFrame] = sample * gain * envelope * 0.72f;
        }

        // Record the contribution before the final mix/limiter. This turns a
        // resolved alias into testable evidence that the individual phoneme
        // produced a nontrivial, finite signal in the requested render, not
        // merely that the surrounding word was non-silent.
        double eventEnergy = 0.0;
        size_t contributingFrames = 0;
        for (size_t frame = beginFrame, local = 0;
             frame < endFrame && local < eventAudio.size(); ++frame, ++local) {
            const float sample = eventAudio[local];
            if (std::isfinite(sample)) {
                eventEnergy += static_cast<double>(sample) * sample;
                ++contributingFrames;
            }
        }
        auto& diagnosticPhone = out.diagnostics.phonemes[diagnosticPhoneIndex];
        diagnosticPhone.renderedFrames = contributingFrames;
        diagnosticPhone.renderedRms = contributingFrames
            ? static_cast<float>(std::sqrt(eventEnergy / contributingFrames)) : 0.f;
        for (const size_t continuationIndex : continuationDiagnosticIndices) {
            out.diagnostics.phonemes[continuationIndex].renderedFrames = diagnosticPhone.renderedFrames;
            out.diagnostics.phonemes[continuationIndex].renderedRms = diagnosticPhone.renderedRms;
        }

        // Tick-to-frame conversions above are rounded independently. At some
        // tempi an event's destination interval can therefore be one frame
        // longer than its local buffer. Never let that harmless rounding
        // difference become an out-of-bounds sample read: besides undefined
        // behavior, a large stray float can charge the DC blocker and pin the
        // soft limiter long after a short phoneme has ended.
        for (size_t frame = beginFrame, local = 0;
             frame < endFrame && local < eventAudio.size(); ++frame, ++local) {
            out.samples[frame] += eventAudio[local];
        }
        }
        noteIndex = chainEndIndex;
    }

    // Lightweight deterministic safety stage: DC blocker, soft limiter, and 5 V-compatible headroom.
    float x1 = 0.f, y1 = 0.f;
    for (auto& x : out.samples) {
        const float y = x - x1 + 0.995f * y1; x1 = x; y1 = y;
        x = std::tanh(y * 1.15f) * 0.82f;
    }
    const size_t advanceFrames = std::min(out.samples.size(), static_cast<size_t>(
        std::llround(kClassicOutputAdvanceMs * out.sampleRate / 1000.0)));
    if (advanceFrames > 0 && advanceFrames < out.samples.size()) {
        std::move(out.samples.begin() + static_cast<std::ptrdiff_t>(advanceFrames),
                  out.samples.end(), out.samples.begin());
        std::fill(out.samples.end() - static_cast<std::ptrdiff_t>(advanceFrames),
                  out.samples.end(), 0.f);
    }
    out.diagnostics.renderSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    return out;
}

}  // namespace vocalrack

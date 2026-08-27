#pragma once

#include "core/VocalScore.hpp"
#include "voicebank/Voicebank.hpp"

#include <memory>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace vocalrack {

inline constexpr const char* kEnglishXSampaPhonemizer = "EN X-SAMPA";
inline constexpr const char* kEnglishVccvPhonemizer = "English VCCV";
inline constexpr const char* kEnglishToJapanesePhonemizer = "English to Japanese";
inline constexpr const char* kLegacyEnglishToJapanesePhonemizer = "English \xE2\x86\x92 Japanese";
inline constexpr const char* kJapaneseAutoPhonemizer = "Japanese Auto";
inline constexpr const char* kJapaneseCvvcPhonemizer = "Japanese CVVC";
inline constexpr const char* kDirectAliasPhonemizer = "Direct Alias";

struct AliasAttempt {
    std::string alias;
    bool found = false;
};

inline std::string canonicalPhonemizerName(std::string name) {
    return name == kLegacyEnglishToJapanesePhonemizer
        ? std::string(kEnglishToJapanesePhonemizer)
        : name;
}

struct PhonemeEvent {
    std::string requestedAlias;
    std::string selectedAlias;
    int64_t relativeTick = 0;
    int targetMidiNote = 60;
    double sourcePitchHz = 0.0;
    std::string sourceNoteId;
    const OtoEntry* oto = nullptr;
    // Native V1 fills these fields after the event contributes samples.
    // Alias resolution does not prove that an internal phoneme was
    // audible; these fields let regression tests enforce that distinction.
    size_t renderedFrames = 0;
    float renderedRms = 0.f;
    std::vector<AliasAttempt> attempts;
    std::string diagnostic;
};

class IPhonemizer {
public:
    virtual ~IPhonemizer() = default;
    virtual std::string name() const = 0;
    virtual PhonemeEvent process(const Note& note, const Note* previous, const Note* next,
                                 const Voicebank& singer) const = 0;
    virtual std::vector<PhonemeEvent> processAll(const Note& note, const Note* previous,
                                                const Note* next, const Voicebank& singer) const;
    // A lyric followed by UTAU extension notes forms one pronunciation,
    // but those notes still carry syllable-boundary meaning. The default
    // combines their duration, preserving the former behavior for simple
    // phonemizers. English phonemizers can override this to distinguish +*
    // (hold this vowel) from + (advance to the next syllable).
    virtual std::vector<PhonemeEvent> processAllChain(const std::vector<Note>& notes,
                                                     const Note* previous, const Note* next,
                                                     const Voicebank& singer) const;
};

class JapaneseAutoPhonemizer final : public IPhonemizer {
public:
    std::string name() const override { return kJapaneseAutoPhonemizer; }
    PhonemeEvent process(const Note& note, const Note* previous, const Note* next,
                         const Voicebank& singer) const override;
};

class JapaneseCvvcPhonemizer final : public IPhonemizer {
public:
    std::string name() const override { return kJapaneseCvvcPhonemizer; }
    PhonemeEvent process(const Note& note, const Note* previous, const Note* next,
                         const Voicebank& singer) const override;
    std::vector<PhonemeEvent> processAll(const Note& note, const Note* previous,
                                        const Note* next, const Voicebank& singer) const override;
};

// Lyrics may be ordinary English words or explicit X-SAMPA hints in square
// brackets, for example `read [r i d]`. Alias lookup follows OpenUtau's
// Delta-style EN X-SAMPA start/context/CV conventions.
class EnglishXSampaPhonemizer final : public IPhonemizer {
public:
    std::string name() const override { return kEnglishXSampaPhonemizer; }
    PhonemeEvent process(const Note& note, const Note* previous, const Note* next,
                         const Voicebank& singer) const override;
    std::vector<PhonemeEvent> processAll(const Note& note, const Note* previous,
                                        const Note* next, const Voicebank& singer) const override;
};

class EnglishVccvPhonemizer final : public IPhonemizer {
public:
    std::string name() const override { return kEnglishVccvPhonemizer; }
    PhonemeEvent process(const Note& note, const Note* previous, const Note* next,
                         const Voicebank& singer) const override;
    std::vector<PhonemeEvent> processAll(const Note& note, const Note* previous,
                                        const Note* next, const Voicebank& singer) const override;
};

// English pronunciation mapped onto an ordinary Japanese CV/VCV bank. This
// retains the bank's Japanese accent and mirrors OpenUtau's
// built-in EN-to-JA use case for singers such as Adachi Rei.
class EnglishToJapanesePhonemizer final : public IPhonemizer {
public:
    std::string name() const override { return kEnglishToJapanesePhonemizer; }
    PhonemeEvent process(const Note& note, const Note* previous, const Note* next,
                         const Voicebank& singer) const override;
    std::vector<PhonemeEvent> processAll(const Note& note, const Note* previous,
                                        const Note* next, const Voicebank& singer) const override;
    std::vector<PhonemeEvent> processAllChain(const std::vector<Note>& notes,
                                             const Note* previous, const Note* next,
                                             const Voicebank& singer) const override;
};

class DirectAliasPhonemizer final : public IPhonemizer {
public:
    std::string name() const override { return kDirectAliasPhonemizer; }
    PhonemeEvent process(const Note& note, const Note* previous, const Note* next,
                         const Voicebank& singer) const override;
};

std::string normalizeJapanese(const std::string& text);
bool validJapaneseLyricInput(const std::string& text);
std::string trailingVowel(const std::string& kana);
std::vector<std::string> englishToXSampa(const std::string& lyric);
// Configure the CMU pronunciation dictionary, which loads on first use. Rack supplies
// the packaged asset path during plugin initialization; standalone tools use
// the repository-relative default unless they override it.
void setEnglishDictionaryPath(const std::filesystem::path& path);
std::string trailingXSampaVowel(const std::string& lyric);
bool lyricIsExtender(const std::string& lyric);
std::string sustainedVowelKana(const Note* previous);
std::unique_ptr<IPhonemizer> makePhonemizer(const std::string& name);

}  // namespace vocalrack

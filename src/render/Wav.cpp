#include "Wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace vocalrack {

static uint16_t u16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
static uint32_t u32(const unsigned char* p) { return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }

AudioBuffer readWavMono(const std::filesystem::path& path) {
    std::error_code sizeError;
    const auto fileBytes = std::filesystem::file_size(path, sizeError);
    if (sizeError) throw std::runtime_error("Unable to inspect WAV " + path.string());
    if (fileBytes > 128u * 1024u * 1024u) throw std::runtime_error("WAV exceeds the 128 MiB input limit: " + path.string());
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Unable to read WAV " + path.string());
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4))
        throw std::runtime_error("Unsupported WAV header: " + path.string());
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t sampleRate = 0;
    const unsigned char* data = nullptr;
    size_t dataSize = 0;
    for (size_t pos = 12; pos + 8 <= bytes.size();) {
        const uint32_t size = u32(bytes.data() + pos + 4);
        if (pos + 8 + size > bytes.size()) break;
        if (!std::memcmp(bytes.data() + pos, "fmt ", 4) && size >= 16) {
            format = u16(bytes.data() + pos + 8);
            channels = u16(bytes.data() + pos + 10);
            sampleRate = u32(bytes.data() + pos + 12);
            bits = u16(bytes.data() + pos + 22);
            // WAVE_FORMAT_EXTENSIBLE stores the actual PCM/float subtype at
            // the beginning of its GUID. Ordinary UTAU banks increasingly
            // contain WAVs written in this otherwise standard form.
            if (format == 0xfffe && size >= 40) format = u16(bytes.data() + pos + 32);
        } else if (!std::memcmp(bytes.data() + pos, "data", 4)) {
            data = bytes.data() + pos + 8;
            dataSize = size;
        }
        pos += 8 + size + (size & 1u);
    }
    if (!data || !channels || channels > 64 || sampleRate < 8000 || sampleRate > 384000 ||
        (format != 1 && format != 3))
        throw std::runtime_error("Unsupported WAV format: " + path.string());
    const size_t bytesPerSample = bits / 8;
    if (!bytesPerSample || dataSize < bytesPerSample * channels) throw std::runtime_error("Empty WAV " + path.string());
    const size_t frames = dataSize / (bytesPerSample * channels);
    if (frames > 32u * 1024u * 1024u) throw std::runtime_error("Decoded WAV exceeds the frame budget: " + path.string());
    AudioBuffer out{sampleRate, std::vector<float>(frames)};
    for (size_t frame = 0; frame < frames; ++frame) {
        double sum = 0.0;
        for (uint16_t channel = 0; channel < channels; ++channel) {
            const auto* p = data + (frame * channels + channel) * bytesPerSample;
            float value = 0.f;
            if (format == 1 && bits == 8) value = (static_cast<int>(p[0]) - 128) / 128.f;
            else if (format == 1 && bits == 16) value = static_cast<int16_t>(u16(p)) / 32768.f;
            else if (format == 1 && bits == 24) {
                int32_t v = p[0] | (p[1] << 8) | (p[2] << 16); if (v & 0x800000) v |= ~0xFFFFFF;
                value = v / 8388608.f;
            } else if (format == 1 && bits == 32) value = static_cast<int32_t>(u32(p)) / 2147483648.f;
            else if (format == 3 && bits == 32) std::memcpy(&value, p, sizeof(value));
            else if (format == 3 && bits == 64) {
                double wide = 0.0;
                std::memcpy(&wide, p, sizeof(wide));
                value = static_cast<float>(wide);
            }
            else throw std::runtime_error("Unsupported WAV bit depth: " + path.string());
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite WAV sample: " + path.string());
            sum += value;
        }
        const float mixed = static_cast<float>(sum / channels);
        if (!std::isfinite(mixed)) throw std::runtime_error("Non-finite WAV mix: " + path.string());
        out.samples[frame] = mixed;
    }
    return out;
}

static void writeLe16(std::ostream& out, uint16_t v) { out.put(v & 0xff); out.put((v >> 8) & 0xff); }
static void writeLe32(std::ostream& out, uint32_t v) {
    out.put(v & 0xff); out.put((v >> 8) & 0xff); out.put((v >> 16) & 0xff); out.put((v >> 24) & 0xff);
}

void writeWavMono16(const std::filesystem::path& path, const AudioBuffer& audio) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Unable to write WAV " + path.string());
    const uint32_t dataBytes = static_cast<uint32_t>(audio.samples.size() * 2);
    out.write("RIFF", 4); writeLe32(out, 36 + dataBytes); out.write("WAVEfmt ", 8); writeLe32(out, 16);
    writeLe16(out, 1); writeLe16(out, 1); writeLe32(out, audio.sampleRate); writeLe32(out, audio.sampleRate * 2);
    writeLe16(out, 2); writeLe16(out, 16); out.write("data", 4); writeLe32(out, dataBytes);
    for (float sample : audio.samples) {
        const auto value = static_cast<int16_t>(std::lrint(std::clamp(sample, -1.f, 1.f) * 32767.f));
        writeLe16(out, static_cast<uint16_t>(value));
    }
}

double estimateFundamental(const AudioBuffer& audio, size_t begin, size_t end) {
    if (audio.samples.size() < 256) return 0.0;
    begin = std::min(begin, audio.samples.size());
    if (end == 0 || end > audio.samples.size()) end = audio.samples.size();
    if (end <= begin + 256) { begin = 0; end = audio.samples.size(); }
    const size_t maxWindow = std::min<size_t>(end - begin, 8192);
    begin += ((end - begin) - maxWindow) / 2;
    end = begin + maxWindow;
    // YIN's cumulative mean normalized difference selects the first convincing
    // period rather than the globally strongest autocorrelation. The latter is
    // often a two- or three-period multiple for nearly periodic UTAU vowels and
    // therefore caused octave/subharmonic source-pitch mistakes.
    // The V1 analyzer targets the bundled singing range and
    // caps F0 at 450 Hz. This also prevents a dominant even harmonic around
    // 520-600 Hz from being mistaken for a C4-range fundamental.
    const size_t minLag = std::max<size_t>(2, audio.sampleRate / 450);
    const size_t maxLag = std::min<size_t>((end - begin) / 2, audio.sampleRate / 70);
    if (maxLag <= minLag + 2) return 0.0;
    std::vector<double> difference(maxLag + 1, 0.0);
    std::vector<double> normalized(maxLag + 1, 1.0);
    for (size_t lag = 1; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (size_t i = begin; i + lag < end; ++i) {
            const double delta = audio.samples[i] - audio.samples[i + lag];
            sum += delta * delta;
        }
        difference[lag] = sum / std::max<size_t>(1, end - begin - lag);
    }
    double cumulative = 0.0;
    for (size_t lag = 1; lag <= maxLag; ++lag) {
        cumulative += difference[lag];
        normalized[lag] = cumulative > 1e-18 ? difference[lag] * lag / cumulative : 1.0;
    }
    size_t candidate = 0;
    constexpr double threshold = 0.24;
    for (size_t lag = minLag + 1; lag < maxLag; ++lag) {
        if (normalized[lag] < threshold && normalized[lag] <= normalized[lag - 1]
            && normalized[lag] <= normalized[lag + 1]) {
            candidate = lag;
            break;
        }
    }
    if (!candidate) {
        candidate = minLag;
        for (size_t lag = minLag + 1; lag <= maxLag; ++lag)
            if (normalized[lag] < normalized[candidate]) candidate = lag;
    }
    if (normalized[candidate] > 0.55) return 0.0;
    double refinedLag = static_cast<double>(candidate);
    if (candidate > 0 && candidate < maxLag) {
        const double left = normalized[candidate - 1], center = normalized[candidate], right = normalized[candidate + 1];
        const double denominator = left - 2.0 * center + right;
        if (std::abs(denominator) > 1e-12)
            refinedLag += std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5);
    }
    return refinedLag > 0.0 ? static_cast<double>(audio.sampleRate) / refinedLag : 0.0;
}

bool finiteAudio(const AudioBuffer& audio) noexcept {
    return std::all_of(audio.samples.begin(), audio.samples.end(), [](float v) { return std::isfinite(v); });
}

float peakAudio(const AudioBuffer& audio) noexcept {
    float peak = 0.f; for (float v : audio.samples) peak = std::max(peak, std::abs(v)); return peak;
}

double rmsAudio(const AudioBuffer& audio) noexcept {
    if (audio.samples.empty()) return 0.0;
    long double sum = 0.0; for (float v : audio.samples) sum += v * v;
    return std::sqrt(static_cast<double>(sum / audio.samples.size()));
}

}  // namespace vocalrack

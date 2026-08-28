#include "Encoding.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <iconv.h>
#include <type_traits>
#endif

namespace vocalrack {

bool isValidUtf8(const std::string& s) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = 0;
    while (i < s.size()) {
        if (p[i] < 0x80) { ++i; continue; }
        int extra = (p[i] & 0xE0) == 0xC0 ? 1 : (p[i] & 0xF0) == 0xE0 ? 2 : (p[i] & 0xF8) == 0xF0 ? 3 : -1;
        if (extra < 0 || i + extra >= s.size()) return false;
        uint32_t cp = p[i] & ((1u << (6 - extra)) - 1u);
        for (int j = 1; j <= extra; ++j) {
            if ((p[i + j] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i + j] & 0x3F);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += static_cast<size_t>(extra + 1);
    }
    return true;
}

#ifndef _WIN32
template <typename IconvFunction>
static size_t callIconv(IconvFunction function, iconv_t cd, const char* bytes,
                        size_t* inLeft, char** output, size_t* outLeft) {
    // POSIX iconv implementations disagree about whether the input buffer is
    // `char**` or `const char**`. Select the signature exposed by the active
    // SDK instead of relying on implementation-specific macros.
    if constexpr (std::is_invocable_r_v<size_t, IconvFunction, iconv_t,
                                        const char**, size_t*, char**, size_t*>) {
        const char* input = bytes;
        return function(cd, &input, inLeft, output, outLeft);
    } else {
        char* input = const_cast<char*>(bytes);
        return function(cd, &input, inLeft, output, outLeft);
    }
}

static std::string iconvDecode(const std::string& bytes, const char* from) {
    iconv_t cd = iconv_open("UTF-8", from);
    if (cd == reinterpret_cast<iconv_t>(-1)) throw std::runtime_error("iconv does not support requested encoding");
    std::vector<char> output(bytes.size() * 4 + 16);
    size_t inLeft = bytes.size();
    char* out = output.data();
    size_t outLeft = output.size();
    const size_t result = callIconv(&iconv, cd, bytes.data(), &inLeft, &out, &outLeft);
    iconv_close(cd);
    if (result == static_cast<size_t>(-1) || inLeft != 0) throw std::runtime_error("Invalid or ambiguous text encoding");
    return std::string(output.data(), static_cast<size_t>(out - output.data()));
}
#else
static std::string wideToUtf8(const wchar_t* input, int inputLength) {
    if (inputLength == 0) return {};
    const int outputLength = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, input, inputLength, nullptr, 0, nullptr, nullptr);
    if (outputLength <= 0) throw std::runtime_error("Invalid UTF-16 text");
    std::string output(static_cast<size_t>(outputLength), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, inputLength,
                            output.data(), outputLength, nullptr, nullptr) != outputLength) {
        throw std::runtime_error("Unable to encode UTF-8 text");
    }
    return output;
}

static std::string windowsCodePageDecode(const std::string& bytes, unsigned int codePage) {
    if (bytes.empty()) return {};
    if (bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Encoded text is too large");
    const int inputLength = static_cast<int>(bytes.size());
    const int wideLength = MultiByteToWideChar(
        codePage, MB_ERR_INVALID_CHARS, bytes.data(), inputLength, nullptr, 0);
    if (wideLength <= 0) throw std::runtime_error("Invalid or ambiguous text encoding");
    std::vector<wchar_t> wide(static_cast<size_t>(wideLength));
    if (MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), inputLength,
                            wide.data(), wideLength) != wideLength) {
        throw std::runtime_error("Unable to decode text");
    }
    return wideToUtf8(wide.data(), wideLength);
}

static std::string utf16LeDecode(const std::string& bytes) {
    static_assert(sizeof(wchar_t) == 2, "Windows wchar_t must contain one UTF-16 code unit");
    if ((bytes.size() & 1u) != 0) throw std::runtime_error("Invalid UTF-16LE byte count");
    if (bytes.empty()) return {};
    const size_t unitCount = bytes.size() / 2;
    if (unitCount > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Encoded text is too large");
    std::vector<wchar_t> wide(unitCount);
    for (size_t i = 0; i < unitCount; ++i) {
        const auto lo = static_cast<unsigned char>(bytes[i * 2]);
        const auto hi = static_cast<unsigned char>(bytes[i * 2 + 1]);
        wide[i] = static_cast<wchar_t>(static_cast<unsigned int>(lo) |
                                       (static_cast<unsigned int>(hi) << 8u));
    }
    return wideToUtf8(wide.data(), static_cast<int>(unitCount));
}
#endif

static std::string decodeUtf16Le(const std::string& bytes) {
#ifdef _WIN32
    return utf16LeDecode(bytes);
#else
    return iconvDecode(bytes, "UTF-16LE");
#endif
}

static std::string decodeCp932(const std::string& bytes) {
#ifdef _WIN32
    return windowsCodePageDecode(bytes, 932);
#else
    try { return iconvDecode(bytes, "CP932"); }
    catch (...) { return iconvDecode(bytes, "SHIFT-JIS"); }
#endif
}

std::string decodeText(const std::string& bytes, TextEncoding* detected) {
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        if (detected) *detected = TextEncoding::Utf8Bom;
        return bytes.substr(3);
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        if (detected) *detected = TextEncoding::Utf16Le;
        return decodeUtf16Le(bytes.substr(2));
    }
    if (isValidUtf8(bytes)) {
        if (detected) *detected = TextEncoding::Utf8;
        return bytes;
    }
    if (detected) *detected = TextEncoding::Cp932;
    return decodeCp932(bytes);
}

std::string readDecodedText(const std::filesystem::path& path, TextEncoding* detected) {
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (sizeError) throw std::runtime_error("Unable to inspect " + path.string());
    if (size > 64u * 1024u * 1024u) throw std::runtime_error("Text file exceeds the 64 MiB limit: " + path.string());
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Unable to read " + path.string());
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decodeText(bytes, detected);
}

}  // namespace vocalrack

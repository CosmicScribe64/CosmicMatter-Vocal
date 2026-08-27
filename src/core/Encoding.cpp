#include "Encoding.hpp"

#include <fstream>
#include <iconv.h>
#include <stdexcept>
#include <vector>

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

static std::string iconvDecode(const std::string& bytes, const char* from) {
    iconv_t cd = iconv_open("UTF-8", from);
    if (cd == reinterpret_cast<iconv_t>(-1)) throw std::runtime_error("iconv does not support requested encoding");
    std::vector<char> output(bytes.size() * 4 + 16);
    char* input = const_cast<char*>(bytes.data());
    size_t inLeft = bytes.size();
    char* out = output.data();
    size_t outLeft = output.size();
    const size_t result = iconv(cd, &input, &inLeft, &out, &outLeft);
    iconv_close(cd);
    if (result == static_cast<size_t>(-1) || inLeft != 0) throw std::runtime_error("Invalid or ambiguous text encoding");
    return std::string(output.data(), static_cast<size_t>(out - output.data()));
}

std::string decodeText(const std::string& bytes, TextEncoding* detected) {
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        if (detected) *detected = TextEncoding::Utf8Bom;
        return bytes.substr(3);
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        if (detected) *detected = TextEncoding::Utf16Le;
        return iconvDecode(bytes.substr(2), "UTF-16LE");
    }
    if (isValidUtf8(bytes)) {
        if (detected) *detected = TextEncoding::Utf8;
        return bytes;
    }
    if (detected) *detected = TextEncoding::Cp932;
    try { return iconvDecode(bytes, "CP932"); }
    catch (...) { return iconvDecode(bytes, "SHIFT-JIS"); }
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

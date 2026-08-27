#pragma once

#include <filesystem>
#include <string>

namespace vocalrack {

enum class TextEncoding { Utf8, Utf8Bom, Utf16Le, Cp932 };

bool isValidUtf8(const std::string& bytes) noexcept;
std::string decodeText(const std::string& bytes, TextEncoding* detected = nullptr);
std::string readDecodedText(const std::filesystem::path& path, TextEncoding* detected = nullptr);

}  // namespace vocalrack


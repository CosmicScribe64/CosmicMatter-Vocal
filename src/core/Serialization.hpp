#pragma once

#include "VocalScore.hpp"
#include <string>

namespace vocalrack {

std::string scoreToJson(const VocalScore& score, bool pretty = false);
VocalScore scoreFromJson(const std::string& jsonText, std::string* migrationNote = nullptr);

}  // namespace vocalrack


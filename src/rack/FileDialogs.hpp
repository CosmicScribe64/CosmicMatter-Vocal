#pragma once

namespace vocalrack {

// osdialog uses commas and semicolons as filter grammar separators, so the
// human-readable label itself must not contain either character.
inline constexpr const char* kVocalScoreDialogFilterSpec =
    "Vocal project or score:vocalrack,ust,ustx,mid,midi";

inline constexpr const char* kVocalProjectDialogFilterSpec =
    "VocalRack project:vocalrack";

inline constexpr const char* kUstxExportDialogFilterSpec =
    "OpenUtau project:ustx";

} // namespace vocalrack

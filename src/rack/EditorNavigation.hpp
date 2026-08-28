#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vocalrack {

struct EditorScrollIntent {
    float timelinePixels = 0.f;
    float pitchPixels = 0.f;
    float zoomSteps = 0.f;
    bool zoomTimeline = false;
    bool zoomPitches = false;
};

inline int64_t editorTimelineTailTicks(int beatsPerBar, int beatUnit) {
    constexpr int64_t ticksPerQuarter = 480;
    constexpr int64_t editingTailBars = 8;
    const int64_t beatTicks = beatUnit > 0
        ? std::max<int64_t>(1, ticksPerQuarter * 4 / beatUnit)
        : ticksPerQuarter;
    return beatTicks * std::max(1, beatsPerBar) * editingTailBars;
}

// Rack reports both mouse-wheel steps and native two-axis trackpad deltas
// through HoverScrollEvent. Keep the mapping independent from Rack widgets so
// the exact navigation contract remains unit-testable on every build target.
inline EditorScrollIntent editorScrollIntent(float horizontal, float vertical,
                                              bool control, bool shift) {
    // Native trackpads can report a burst of several wheel units per frame.
    // Keep both the multiplier and the per-frame cap deliberately gentle so
    // navigating a dense lyric does not skip past the note being inspected.
    constexpr float maximumGesture = 1.5f;
    constexpr float timelinePixelsPerStep = 8.f;
    constexpr float pitchPixelsPerStep = 7.f;
    const float x = std::clamp(horizontal, -maximumGesture, maximumGesture);
    const float y = std::clamp(vertical, -maximumGesture, maximumGesture);

    EditorScrollIntent intent;
    if (control) {
        intent.zoomSteps = y;
        intent.zoomPitches = shift;
        intent.zoomTimeline = !shift;
        return intent;
    }

    // A real horizontal trackpad gesture always pans the timeline. Shift also
    // converts a conventional vertical wheel into a timeline gesture, but it
    // must not add a second pan when the device already supplied X movement.
    if (std::abs(x) > 0.001f)
        intent.timelinePixels = x * timelinePixelsPerStep;
    else if (shift)
        intent.timelinePixels = y * timelinePixelsPerStep;

    if (!shift)
        intent.pitchPixels = y * pitchPixelsPerStep;
    return intent;
}

}  // namespace vocalrack

#pragma once

#include "VocalModule.hpp"

#include <array>
#include <set>
#include <limits>
#include <utility>
#include <vector>

namespace vocalrack {

struct InlineLyricField;
struct InspectorValueField;
struct InspectorInteractionLayer;

struct VocalEditor : rack::widget::OpaqueWidget {
    explicit VocalEditor(VocalModule* module);
    ~VocalEditor() override;
    void step() override;
    void draw(const DrawArgs& args) override;
    void onButton(const ButtonEvent& e) override;
    void onDoubleClick(const DoubleClickEvent& e) override;
    void onHover(const HoverEvent& e) override;
    void onLeave(const LeaveEvent& e) override;
    void onDragMove(const DragMoveEvent& e) override;
    void onDragEnd(const DragEndEvent& e) override;
    void onHoverScroll(const HoverScrollEvent& e) override;
    void onSelectKey(const SelectKeyEvent& e) override;

private:
    enum class EditMode { Notes, Pitch, Dynamics };
    enum class NoteTool { Select, Draw, Erase, Slice };
    enum class TimingHandle { None, Position, Preutterance, Overlap, InternalBoundary };
    enum class InspectorField {
        None,
        Lyric,
        Alias,
        Tone,
        Start,
        Length,
        Position,
        Preutterance,
        Overlap,
        Attack,
        Release,
        VibratoEnabled,
        VibratoStart,
        VibratoDepth,
        VibratoRate,
    };
    struct ActionHit {
        int action = 0;
        rack::math::Rect rect;
    };
    struct InspectorControl {
        InspectorField field = InspectorField::None;
        rack::math::Rect row;
        rack::math::Rect value;
        rack::math::Rect slider;
        bool hasSlider = false;
    };

    VocalModule* module_ = nullptr;
    rack::math::Rect window_;
    rack::math::Rect toolbar_;
    rack::math::Rect inspector_;
    rack::math::Rect ruler_;
    rack::math::Rect sectionLane_;
    rack::math::Rect keyboard_;
    rack::math::Rect piano_;
    rack::math::Rect phonemeLane_;
    rack::math::Rect pitchLane_;
    rack::math::Rect dynamicsLane_;
    rack::math::Rect timelineScrollBar_;
    rack::math::Rect pitchModeToggle_;
    rack::math::Rect dynamicsModeToggle_;
    std::vector<ActionHit> actionHits_;
    std::vector<InspectorControl> inspectorControls_;
    int hoveredAction_ = -1;
    int pendingInlineLyricNote_ = -1;
    int pendingInlineLyricDelayFrames_ = 0;
    int lastDragAuditionMidi_ = -1;
    bool visualTooltipForced_ = false;
    std::set<size_t> selection_;
    std::vector<Note> clipboard_;
    EditMode mode_ = EditMode::Notes;
    NoteTool noteTool_ = NoteTool::Select;
    bool dragging_ = false, resizing_ = false, resizingStart_ = false;
    bool insertingLyric_ = false;
    bool curveDragging_ = false, curvePointPitch_ = false;
    bool timingDragging_ = false;
    bool timelineScrollDragging_ = false;
    bool inspectorSliderDragging_ = false;
    bool marqueeDragging_ = false;
    bool drawingNote_ = false;
    bool auditionHolding_ = false;
    bool auditionKeyboardDrag_ = false;
    bool viewInitialized_ = false;
    int initialFocusFrames_ = 3;
    rack::math::Vec dragPixels_;
    rack::math::Vec marqueeStart_;
    rack::math::Vec marqueeCurrent_;
    rack::math::Vec drawStart_;
    rack::math::Vec drawCurrent_;
    std::set<size_t> marqueeBaseSelection_;
    rack::math::Vec curveDragPixels_;
    std::string dragBefore_;
    // Note indices are not stable once a drag is normalized and sorted. Keep
    // the identity of the rigid selection so a group remains selected after
    // it crosses another note on the timeline.
    std::vector<std::string> dragSelectionIds_;
    std::string dragPrimaryNoteId_;
    std::string drawBefore_;
    std::string drawNoteId_;
    int64_t drawAnchorTick_ = 0;
    std::string curveDragBefore_;
    size_t curveNoteIndex_ = std::numeric_limits<size_t>::max();
    size_t curvePointIndex_ = std::numeric_limits<size_t>::max();
    CurvePoint curvePointStart_;
    TimingHandle timingHandle_ = TimingHandle::None;
    size_t timingNoteIndex_ = std::numeric_limits<size_t>::max();
    size_t timingPhoneIndex_ = std::numeric_limits<size_t>::max();
    int64_t timingStartPositionTick_ = 0;
    int64_t timingStartBoundaryTick_ = 0;
    std::vector<int64_t> timingBoundaryAutomaticTicks_;
    rack::math::Vec timingDragPixels_;
    std::string timingDragBefore_;
    float timingStartActualMs_ = 0.f;
    float timingOtoMs_ = 0.f;
    InlineLyricField* inlineLyricField_ = nullptr;
    size_t inlineLyricNoteIndex_ = std::numeric_limits<size_t>::max();
    std::string inlineLyricBefore_;
    size_t lastClickedNoteIndex_ = std::numeric_limits<size_t>::max();
    double lastNoteClickTime_ = -1000.0;
    float timelineScrollDragX_ = 0.f;
    float inspectorSliderX_ = 0.f;
    float auditionPointerY_ = 0.f;
    InspectorField inspectorSliderField_ = InspectorField::None;
    std::string inspectorSliderBefore_;
    InspectorValueField* inspectorValueField_ = nullptr;
    InspectorInteractionLayer* inspectorInteractionLayer_ = nullptr;
    InspectorField inspectorEditField_ = InspectorField::None;
    InspectorField pendingInspectorEditField_ = InspectorField::None;
    int pendingInspectorEditFrames_ = 0;
    size_t inspectorEditNoteIndex_ = std::numeric_limits<size_t>::max();
    std::string inspectorEditNoteId_;
    std::string inspectorEditBefore_;
    std::string inspectorError_;
    int pendingMenuAction_ = -1;
    GLFWcursor* horizontalResizeCursor_ = nullptr;
    GLFWcursor* handCursor_ = nullptr;
    GLFWcursor* textCursor_ = nullptr;
    GLFWcursor* crosshairCursor_ = nullptr;
    GLFWcursor* currentCursor_ = nullptr;
    std::array<std::shared_ptr<rack::window::Svg>, 4> editToolIcons_;

    void layout();
    void toolbarAction(int index);
    void importUstx();
    void selectSinger();
    void editLyric(bool alias);
    void beginInlineLyric(size_t noteIndex);
    void finishInlineLyric(bool commit, int advance = 0);
    void updateInlineLyricLayout();
    void beginInspectorEdit(InspectorField field);
    void finishInspectorEdit(bool commit, int advance = 0);
    void updateInspectorFieldLayout();
    std::string inspectorValue(InspectorField field) const;
    bool applyInspectorValue(InspectorField field, const std::string& value);
    float inspectorNumericValue(InspectorField field) const;
    void setInspectorNumericValue(InspectorField field, float value);
    std::pair<float, float> inspectorRange(InspectorField field) const;
    void setTimelineScrollFromX(float x);
    bool handleInspectorPress(rack::math::Vec pos);
    void handleInspectorDrag(rack::math::Vec delta);
    void handleInspectorDragEnd();
    void editVibrato();
    void editPhonemeTiming();
    void resetPhonemeTiming();
    void resetSelectedVoiceShaping();
    void openFileMenu();
    void openEditMenu();
    void openScoreMenu();
    void openSectionMenu();
    void openViewMenu();
    void openSnapMenu();
    void insertLyricAt(rack::math::Vec pos);
    void sliceNoteAt(size_t noteIndex, int64_t tick);
    void addSection();
    void editSectionBounds();
    void editNoteTone();
    void editNoteTiming();
    void renameSection();
    void deleteSection();
    void fitPitchRange();
    void ensureNoteVisible(size_t noteIndex);
    void zoomFull();
    void zoomSelection();
    void zoomSection();
    void openContextMenu(rack::math::Vec pos);
    void addNoteAt(rack::math::Vec pos);
    void beginDrawNote(rack::math::Vec pos);
    void updateDrawNote();
    void finishDrawNote();
    void copySelectedNotes();
    void deleteSelectedNotes();
    void pasteClipboardAtTick(int64_t tick);
    size_t noteAt(rack::math::Vec pos) const;
    size_t noteAtTick(int64_t tick) const;
    std::pair<size_t, size_t> phonemeAt(rack::math::Vec pos) const;
    rack::math::Rect noteRect(const Note& note) const;
    bool addCurvePoint(rack::math::Vec pos);
    bool startCurvePointDrag(rack::math::Vec pos);
    bool startPhonemeTimingDrag(rack::math::Vec pos);
    void clearCurvePointSelection();
    int64_t xToTick(float x) const;
    int yToMidi(float y) const;
    int64_t snapTick(int64_t tick) const;
    void setMouseCursor(GLFWcursor* cursor);
};

void openVocalEditor(VocalModule* module);
void importVocalScoreFile(VocalModule* module);
void saveVocalProjectFile(VocalModule* module);
void exportVocalUstxFile(VocalModule* module);

}  // namespace vocalrack

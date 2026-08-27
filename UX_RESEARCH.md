# VocalRack V1 UX research and design contract

Research date: 2026-08-26. These references informed the V1 panel and editor. The design decisions below state how the implementation applies them.

## Compared products

- [VCV Rack panel guide](https://vcvrack.com/manual/Panel): fixed 3U hardware metaphor, HP-based width, succinct labels, strong input/output grouping, and text readable at 100% zoom.
- [VCV Rack plugin API guide](https://vcvrack.com/manual/PluginGuide) and [menu/tooltips guide](https://vcvrack.com/manual/MenuBar): parameter/port descriptions belong in native tooltips; secondary settings belong in the module context menu.
- [Entrian Sequencers](https://entrian.com/audio/entrian-sequencers.html): the panel acts as a status viewport and opens a larger, resizable song editor with a settings column and direct timeline manipulation.
- [Voxglitch Piano Roll](https://github.com/clone45/voxglitch/blob/master/docs/modules/piano-roll/user_manual.md): a wide Rack-native piano roll, compact mode controls, track color, contextual operations, and wheel behavior that cooperates with Rack.
- RCM Piano Roll: a visually sparse reference for making the timeline dominant and isolating I/O in a supporting strip.
- MindMeld ShapeMaster Pro: strong rectangular grouping, side-column I/O, a single dominant graph, restrained color, and labels placed next to the hardware they explain.
- [OpenUtau](https://github.com/openutau/OpenUtau): lyrics inside notes, phoneme timing beneath the piano roll, expression data in dedicated lower lanes, and explicit edit modes.
- [WCAG 2.2 contrast minimum](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html): text targets at least 4.5:1 contrast (3:1 for large text), with thicker strokes and redundant labels where color alone would be ambiguous.

## V1 design decisions

1. VOCAL is 64 HP. Readable score information takes priority over the original 40-50 HP target; Rack permits any 15 px/HP multiple.
2. The compact module uses three columns: transport/clocking, dominant score, and performance/expression routing. This removes the low horizontal control strip that previously starved the score of height.
3. The score uses a piano roll with a labeled keyboard. C-octave names establish pitch, lyrics use high-contrast 15 px text in 20-26 px blocks, and the authored pitch trace stays on the note.
4. Resolved phonemes have their own aligned compact lane. Dynamics has its own labeled lane with +12, 0, and -24 dB references, a strong zero line, a 2 px curve, and visible authored control points.
5. The expand affordance is an icon in the score corner; the whole score is also a hit target and tooltip. It opens one Rack-native full-screen overlay rather than a private OS window, avoiding unsupported multi-window plumbing and packaging/crash risk.
6. The full editor uses one large piano roll plus dedicated section, phoneme, pitch-cents, and dynamics-dB lanes. Curve editing requires a visible mode; clicks elsewhere leave expression data unchanged.
7. Primary authoring actions live in the editor sidebar/toolbar. Secondary transport, singer, phonemizer, and diagnostics choices are grouped in the native module context menu.
8. Parameters, ports, lights, and custom buttons have concise tooltips. Labels use plain units and names; values such as "-12 / +12" include "semitones," "cents," or "dB."
9. The editor auto-fits and vertically centers the score on first open, while subsequent scroll/zoom state remains patch-persistent.
10. Pink marks authored vocal expression and active modes, cyan marks notes/phonemes, yellow marks pitch, green marks healthy readiness, and red is reserved for errors/missing aliases.

## Visual verification checklist

- Readable at ordinary Rack zoom without overlapping text or ports.
- Score, keyboard, lyric, pitch, and dynamics remain visually aligned.
- Expand icon, whole-score hit target, Escape/Close, tooltips, and context menu work in real Rack.
- Editor note selection and each mode produce immediate visible feedback.
- Panel remains a normal 3U Rack module and leaves patch navigation to Rack.

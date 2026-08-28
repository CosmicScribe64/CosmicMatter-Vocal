# Cosmic Matter: Vocal V1 Manual

Vocal is a monophonic UTAU voicebank instrument for VCV Rack. Write or import a vocal score, choose a singer and pronunciation mode, then control playback and expression from the rack.

Each Vocal module contains one singer and one vocal line. Add more instances for harmonies, doubles, or call-and-response parts.

This manual covers normal use. Renderer design, file mappings, compatibility tests, and development commands are documented in [ARCHITECTURE.md](ARCHITECTURE.md).

## Contents

- [Quick start](#quick-start)
- [Common patches](#common-patches)
- [Panel guide](#panel-guide)
- [Score editor](#score-editor)
- [Singers and pronunciation](#singers-and-pronunciation)
- [Importing, saving, and exporting](#importing-saving-and-exporting)
- [Clocking and transport](#clocking-and-transport)
- [Control and port reference](#control-and-port-reference)
- [Troubleshooting](#troubleshooting)
- [Singer Plate](#singer-plate)
- [Glossary](#glossary)

## Quick start

### Hear the included singer

1. Add **Cosmic Matter: Vocal** to a patch.
2. Connect **VOICE** to a mixer, VCA, or audio output.
3. Wait for the **READY** light. The included Adachi Rei singer must render the score before playback begins.
4. Press **PLAY/PAUSE** if the transport is paused.

The new module opens with an English example phrase. The center display follows the playhead and shows the lyrics, performed pitch, resolved phonemes, and dynamics.

### Change the phrase

1. Click the center display to open the score editor.
2. Double-click a note and type a new lyric.
3. Press `Return` to commit it.
4. Drag the note vertically to change pitch, horizontally to change timing, or from either edge to change duration.
5. Wait for **READY** after the edit.

The default **English to Japanese** phonemizer lets Adachi Rei sing ordinary English words with the bank's Japanese recordings. The result keeps a Japanese accent.

### Save your work

Rack saves the complete module state inside the `.vcv` patch. To move the vocal score between patches or module instances, open the editor and choose **File > Save lossless VocalRack project...**.

## Common patches

The templates in the module menu and editor File menu provide useful starting points in English and Japanese.

### Play a phrase from Rack's clock

1. Choose **Score templates > English > Looping phrase + one-beat rest**.
2. Open **Transport & timing > Clock resolution** and match the PPQN setting to the clock source. The default is 24 PPQN.
3. Patch the clock to **CLOCK**.
4. Leave **RUN** unpatched and use **PLAY/PAUSE**, or patch a gate to **RUN**.
5. Patch **END** to another module if the end of each loop should trigger an event.

The clock sets musical timing but does not start or stop the singer. Use **RUN**, **TRIG**, or **PLAY/PAUSE** for that.

### Trigger a word

1. Choose the English or Japanese **Triggered word** template.
2. Leave **LOOP** off.
3. Patch a trigger to **TRIG**.
4. Patch **END** to a sequencer, logic module, or another Vocal instance if another event should follow the word.

With **RUN** unpatched, each rising **TRIG** starts the active range. With **RUN** patched, **TRIG** restarts only while **RUN** is high.

### Use a voice like a sustained oscillator

1. Choose a **Sustained vowel instrument** template.
2. Patch a gate to **RUN**.
3. Patch modulation to **PITCH**, **DYN**, **VIB**, or **FORM** and raise the matching attenuverter.
4. Send **VOICE** through the same filters, VCAs, delays, and reverbs used for other Rack voices.

The template enables looping and creates one long vowel. Edit the lyric to choose another vowel. Add empty time after the note if the loop should contain a tempo-synchronized pause.

The module menu setting **When RUN rises** controls whether a new gate resumes from the held position or restarts the active range.

### Switch between sections

1. Open the editor and select the notes for a verse, word, or phrase.
2. Choose **Edit > Create section from selected...** and name it.
3. Set the playback range to **SECTION**.
4. Select a section with the **SECTION** knob or **SECTION** CV input.
5. Choose the change boundary under **Transport & timing > Section changes**.

The **SECTION** input uses integer voltages: 0 V selects section 0, 1 V selects section 1, and so on. Values are rounded and clamped to the sections in the score.

### Import a lead and harmony

Use one Vocal module per monophonic part:

1. Import the lead track into the first module.
2. Add a second Vocal module and import the harmony track.
3. Route both **VOICE** outputs to a mixer.
4. Send the same **CLOCK**, **RUN**, and **RESET** signals to both modules.

The importer lists usable vocal or melody tracks before replacing the current score.

## Panel guide

The 64 HP panel is divided into three working areas.

| Area | Purpose |
|---|---|
| Left | Playback, looping, range selection, tempo, and transport inputs |
| Center | Compact score, playhead, lyrics, phonemes, pitch, and dynamics |
| Right | Transpose, section selection, expression CV, audio output, and END trigger |

Click the center score or its expand icon to open the full editor. Hover over a control or port for its Rack tooltip.

### Transport controls

| Control | Use |
|---|---|
| **PLAY/PAUSE** | Starts or pauses playback when **RUN** is not patched. |
| **RESET** | Returns to the start of the active song or section. It does not change the state of **RUN**. |
| **LOOP** | Repeats the active range. With looping off, playback stops after one pass. |
| **SONG / SECTION** | Chooses whether playback uses the complete score or the selected section. |
| **BPM** | Sets the internal tempo from 20 to 300 BPM when **CLOCK** is not patched. |

### Performance controls

| Control | Use |
|---|---|
| **TRANSPOSE** | Shifts the rendered score from -36 to +36 semitones without changing the written notes. |
| **SECTION** | Selects a section by index. The **SECTION** input takes priority when patched. |
| **PITCH**, **DYN**, **VIB**, **FORM** attenuverters | Set the direction and depth of the matching CV input. Center is no modulation. |

### Status lights

| Light | Meaning |
|---|---|
| **READY** | The current score is rendered and can play. |
| **RENDER** | Vocal is loading the singer or rendering an edit. |
| **ERROR** | A singer, alias, import, or render problem needs attention. |
| **UNDER** | Playback ran out of prepared audio. |

Open the module menu and choose **Diagnostics** to read the current status and last error.

## Score editor

The expanded editor is arranged around the piano roll.

| Region | Contents |
|---|---|
| Top bar | File, Edit, View, Score, Snap, and Section menus; note tools; transport; and range controls |
| Piano keyboard | Pitch names, octave landmarks, audition, and pitch/dynamics lane toggles |
| Piano roll | Notes, lyrics, performed pitch, and playhead |
| Phoneme lane | Resolved aliases and pronunciation timing |
| Pitch and dynamics lanes | Per-note expression curves |
| Note inspector | Typed values for the selected note |
| View bar | Visible position and width within the complete score |

### Add and arrange notes

The score is monophonic. Notes may touch but the saved score never contains an
overlap. Drawing, moving, resizing, or pasting into occupied time gives the
edited notes priority: fully covered notes are removed, while a partly covered
note is trimmed to its earliest remaining span. This follows OpenUtau's
pencil/fix-overlap convention and the whole operation is undoable.

| Tool | Shortcut | Action |
|---|---:|---|
| Select | `V` | Select, move, and resize notes. |
| Draw | `D` | Drag in empty piano-roll space to draw a note. |
| Erase | `E` | Click a note to remove it. |
| Slice | `S` | Split a note. The right side receives the continuation lyric `+`. |

In Select mode:

- Click a note to select it.
- Shift-click to add or remove a note from the selection.
- Drag across empty space to box-select notes.
- Drag a selected note to move the complete selection.
- Drag either note edge to resize it.
- Press `Delete` or `Backspace` to remove the selection.
- Use `Ctrl/Cmd+C` and `Ctrl/Cmd+V` to copy and paste notes.

Click **INSERT LYRIC** or press `I` to create a lyric note in a gap. Clicking inside an existing note splits it at the snapped position and opens lyric entry. Press `Escape` to cancel insertion.

### Edit lyrics and pronunciation

Double-click a note, click its **EDIT** target, or select it and press `Return` to edit the lyric.

| Key | During text entry |
|---|---|
| `Return` | Commit the value. |
| `Tab` | Commit and edit the next note or field. |
| `Shift+Tab` | Commit and edit the previous note or field. |
| `Escape` | Cancel the edit. |

For Japanese phonemizers, common romaji is converted when the lyric is committed. For example, `da` becomes `だ` and `adachirei` becomes `あだちれい`.

The inspector's **PHONEMES** field overrides automatic pronunciation:

- Enter a pipe-separated sequence such as `su | ta | u` to set several aliases.
- Enter `EXACT:` followed by one voicebank alias to require that exact alias.
- Enter `AUTO`, `AUTO: ...`, the original lyric, or an empty value to return to automatic pronunciation.
- Aliases containing spaces or hyphens are kept intact for VCV and CVVC banks.

An automatic value is displayed with an `AUTO:` prefix. A missing alias appears in red. Open **Diagnostics** or inspect the note to see which aliases Vocal attempted.

### Edit pitch, dynamics, and vibrato

Select a note before editing its expression.

- Click **PITCH EDIT** or press `2` to edit pitch points in cents.
- Click **DYN EDIT** or press `3` to edit dynamics points in dB.
- Click an empty position in the active lane to add a point.
- Drag a point to change its time and value.
- Select a point and press `Delete` to remove it.
- Click the active lane button again, or press `1`, to return to note editing.

The yellow pitch line shows the performed contour. Touching notes receive normal portamento unless a rest separates them. **Connect pitch from previous touching note** controls whether a custom pitch curve begins from the previous note's tone.

The inspector's vibrato controls set start position, depth, and rate for the selected note. Live **VIB** CV adds modulation on top of the written vibrato.

### Adjust phoneme timing

Most notes should use the timing supplied by the voicebank. Use these controls when a consonant arrives too early, too late, or crosses a note boundary poorly.

The phoneme lane shows the resolved alias and its envelope:

- Adjacent phonemes form one continuous strip. A straight seam means they meet;
  an X means they crossfade.
- Drag any envelope body horizontally to move all resolved phonemes for that note.
- Drag a white internal divider (or its cyan grip) to retime only that phoneme.
  The divider cannot cross its neighbours.
- Drag **START** to move preutterance.
- Drag **XFADE** to change overlap with the previous phoneme.
- Use the note inspector or the note's context menu for exact values.

The timing fields are:

| Field | Effect |
|---|---|
| **POSITION** | Moves every resolved phoneme for the note in score ticks. |
| **INTERNAL DIVIDER** | Moves one resolved phoneme boundary without shifting the word. |
| **PREUTTER** | Starts the sample before or after its inherited preutterance time. |
| **OVERLAP** | Changes its crossfade with the preceding phoneme. |
| **ATTACK** | Changes the start fade. |
| **RELEASE** | Changes the end fade. |

Timing values are offsets from the selected singer's `oto.ini`. Zero uses the voicebank value. Choose **Reset to voicebank timing** to clear the offsets.

Choose **Restore selected voice shaping to defaults** to clear pronunciation overrides, pitch and dynamics curves, vibrato, and phoneme timing. Note pitch, lyric, position, and duration are preserved. Undo can restore the removed shaping.

### Snap and navigate

The dedicated **SNAP** menu offers quarter, eighth, triplet, sixteenth, and thirty-second-note divisions. Turn snap off for tick-level timing. The timeline uses 480 ticks per quarter note and keeps eight empty bars available after the final note for navigation and full-song continuation. **Fit complete song** still frames the authored notes rather than this editing buffer.

| Gesture | Action |
|---|---|
| Mouse wheel | Scroll through pitches. |
| Shift-wheel | Pan along the timeline. |
| Ctrl/Cmd-wheel | Zoom horizontally. |
| Ctrl/Cmd+Shift-wheel | Change note-row height. |
| Click or drag the **VIEW** bar | Move through the song. |

**View > Fit complete song**, **Fit active section**, and **Fit selected notes** restore useful views. **Follow playhead** is enabled initially. Manual scrolling, zooming, or dragging the View bar turns it off.

### Create and manage sections

Select one or more notes and choose **Section > Create from selected notes...**. Sections are named, non-overlapping playback ranges.

Use the dedicated **Section** menu or right-click inside a section to:

- Fit the section in the editor.
- Rename it.
- Enter exact start and end positions as `bar:beat`, such as `1:1 3:1`.
- Delete it.

Set **RANGE: SECTION** to play the selected section. Section changes can occur immediately, on the next beat, on the next bar, or when the active section ends.

### Preview notes and undo edits

Clicking a piano key or note plays a short reference tone through **VOICE**. Dragging a note to another pitch previews the new pitch. This reference tone is not rendered into the score.

The editor keeps up to 100 score edits in its undo history. Use `Ctrl/Cmd+Z` to undo and `Ctrl/Cmd+Shift+Z` to redo.

### Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Space` | Play or pause when **RUN** is unpatched. |
| `V`, `D`, `E`, `S` | Select, Draw, Erase, or Slice tool. |
| `1`, `2`, `3` | Note, pitch-curve, or dynamics-curve editing. |
| `I` | Toggle Insert Lyric mode. |
| `Return` | Edit the selected note's lyric. |
| `Delete` / `Backspace` | Delete the selected notes or curve point. |
| `Ctrl/Cmd+A` | Select all notes. |
| `Ctrl/Cmd+C`, `Ctrl/Cmd+V` | Copy and paste notes. |
| `Ctrl/Cmd+Z` | Undo. |
| `Ctrl/Cmd+Shift+Z` | Redo. |
| `Ctrl/Cmd+S` | Save a lossless VocalRack project. |
| `Ctrl/Cmd+Shift+S` | Export OpenUtau USTX. |
| `Shift+I` | Import UTAU, OpenUtau, or MIDI. |
| `Escape` | Cancel the current entry mode, or close the editor. |

## Singers and pronunciation

A singer is an UTAU voicebank. A phonemizer converts lyrics into the alias names recorded by that bank. The two must use compatible alias conventions.

### Choose a singer

The bundled Adachi Rei bank is ready without setup. To use another bank:

1. Open the module menu.
2. Choose **Singer & phonemizer > Select or relink voicebank folder...**.
3. Select the singer's top-level folder.
4. Choose a compatible phonemizer.
5. Wait for **READY**.

The folder should contain singer metadata, one or more `oto.ini` files, and the referenced WAV samples. Vocal reads `oto.ini` files in subfolders and applies `prefix.map` when present.

External singer paths are saved as references. After loading a Rack patch or `.vocalrack` file, select the folder again to confirm access on the current computer.

### Choose a phonemizer

| Phonemizer | Intended input | Compatible bank |
|---|---|---|
| **English to Japanese** | Ordinary English words | Japanese CV, VCV, or CVVC; default for Adachi Rei |
| **EN X-SAMPA** | English words or bracketed X-SAMPA pronunciation | Delta/OpenUtau-style English X-SAMPA |
| **English VCCV** | English words | Cz-style English VCCV |
| **Japanese Auto** | Hiragana, katakana, or common romaji | Japanese CV or VCV |
| **Japanese CVVC** | Hiragana, katakana, or common romaji | Japanese CVVC; falls back to CV aliases when needed |
| **Direct Alias** | Exact alias text | Any bank when the score already contains bank aliases |

For EN X-SAMPA, a word can include an exact pronunciation in brackets, such as `read [r i d]`.

### Continue a vowel across notes

Use a continuation lyric when one syllable should span several pitches without repeating its consonant.

- `+` continues the previous vowel.
- OpenUtau variants such as `+1`, `+~`, and `+*` are accepted.
- `ー` is the Japanese long-vowel continuation.
- `-` is accepted for legacy UTAU material.

A continuation note may have its own pitch, duration, dynamics, and vibrato. It must follow a sounding note; it cannot begin a score or follow a rest.

Japanese Auto normally expects one mora per note. To sing あだちれい across five notes, enter `あ`, `だ`, `ち`, `れ`, and `い`, or the romaji equivalents.

## Importing, saving, and exporting

Import replaces the current score after you choose a usable track. Save a copy first if the current score should be kept.

| Format | Use | Important behavior |
|---|---|---|
| `.vocalrack` | Lossless transfer between Vocal modules or Rack patches | Keeps the score, singer reference, phonemizer, sections, transport settings, CV amounts, and editor view. |
| OpenUtau `.ustx` | Exchange with OpenUtau | Imports supported score data and reports settings that Vocal does not use. Export omits Rack-only controls. |
| UTAU `.ust` | Import an older UTAU part | Reads common Japanese encodings and treats existing lyric aliases as direct aliases where appropriate. |
| Standard MIDI `.mid` / `.midi` | Start from a melody | Lists monophonic melody tracks. Imports note timing and pitch, plus lyrics and the first tempo and meter when present. |

### Import a score

Choose **Load VocalRack / UTAU / OpenUtau / MIDI...** from the module menu, or **File > Import UTAU / OpenUtau / MIDI...** in the editor.

- USTX import lists nonempty vocal tracks and voice parts.
- MIDI import lists nonempty monophonic melody tracks. Chord and drum tracks are omitted.
- MIDI notes without lyric events receive an `a` placeholder.
- Import keeps the singer and phonemizer already selected in Vocal.
- A malformed or unusable file leaves the current score unchanged.

After import, read the report for approximated, ignored, or unsupported data. Check the singer and phonemizer before troubleshooting missing aliases.

### Save a lossless project

Choose **File > Save lossless VocalRack project...** or press `Ctrl/Cmd+S`. Use this format when another Vocal module should reproduce the same setup.

Rack also stores the module state in the containing `.vcv` patch. Rendered audio caches are rebuilt when needed and are not stored in project files.

### Export to OpenUtau

Choose **File > Export OpenUtau USTX...** or press `Ctrl/Cmd+Shift+S`.

USTX carries compatible notes, lyrics, pitch and dynamics curves, vibrato, tempo, meter, phonetic hints, and supported timing values. Internal divider edits use OpenUtau's indexed phoneme-position offsets and survive import/export in either direction.

It does not carry Rack cables, CV depths, playback latches, Vocal sections, or every Vocal timing offset. Keep the `.vocalrack` file beside the USTX if the Rack performance must remain reproducible.

## Clocking and transport

### Choose the timing source

- With **CLOCK** unpatched, Vocal uses the **BPM** knob.
- With **CLOCK** patched, Vocal follows the incoming pulse interval and phase.
- Set **Clock resolution** to the PPQN produced by the source. The default is 24 PPQN.

Changing the clock or tempo may start a background render. Existing ready audio fades safely while the new timing is prepared.

### Start and stop playback

Control priority depends on the patched ports.

| Patch state | Playback behavior |
|---|---|
| **RUN** unpatched | **PLAY/PAUSE** controls playback. **TRIG** starts or restarts the range. |
| **RUN** patched and high | Playback advances. A **TRIG** restarts the range. |
| **RUN** patched and low | Playback holds its position and the voice fades to silence. |
| **RESET** receives a trigger | The playhead returns to the active range start without changing **RUN**. |

Under **Transport & timing > When RUN rises**, choose:

| Setting | Behavior |
|---|---|
| **Resume** | Continue from the held position. |
| **Restart active range** | Begin from the range start on every rising gate. |

### One-shot, loop, and END

With **LOOP** off, Vocal plays the active range once, emits **END**, returns to the range start, and stops. A new **TRIG**, rising **RUN**, or panel play action starts another pass.

With **LOOP** on, Vocal emits **END** and repeats the active range. **END** is a 1 ms, 10 V pulse.

## Control and port reference

### Inputs

| Input | Signal and range |
|---|---|
| **CLOCK** | Rising clock pulses at 1, 2, 4, 8, 12, 16, 24, or 48 PPQN. |
| **RESET** | Rising trigger returns to the active range start and clears a pending section change. |
| **RUN** | High gate advances transport; low gate holds transport and fades the voice. |
| **TRIG** | Rising trigger starts or restarts the active range, subject to **RUN** priority. |
| **PITCH** | At full attenuverter depth, -5 to +5 V applies about -12 to +12 semitones. |
| **DYN** | At full attenuverter depth, -5 to +5 V applies about -12 to +12 dB. |
| **VIB** | Adds live vibrato depth. Notes without written vibrato use a 5.5 Hz base rate. |
| **FORM** | Applies a bounded live formant and timbre shift. |
| **SECTION** | Integer-voltage section selection: 0 V for index 0, 1 V for index 1, and so on. |

Live expression CV does not alter the written score. Center the attenuverters or unplug the four expression inputs to hear the authored render.

### Outputs

| Output | Signal |
|---|---|
| **VOICE** | Continuous mono vocal audio with a nominal 5 V peak. |
| **END** | 1 ms, 10 V pulse at the end of every completed song or section range, including loops. |

### Module menu

| Menu | Settings |
|---|---|
| **Score templates** | English and Japanese phrases, sustained vowels, triggered words, looping phrases, and a blank score. |
| **Load / Save / Export** | VocalRack project, UTAU UST, OpenUtau USTX, and MIDI file operations. |
| **Transport & timing** | Clock PPQN, rising-RUN behavior, and section-change boundary. |
| **Singer & phonemizer** | Bundled or external singer and pronunciation mode. |
| **Diagnostics** | Current status, last render error, underruns, and renderer activity. |

## Troubleshooting

### No sound

Check these in order:

1. Confirm that **VOICE** is connected to an audible path.
2. Check whether **RUN** is patched low. A patched **RUN** overrides the panel button.
3. Press **PLAY/PAUSE** or send **TRIG**.
4. Wait for **READY**.
5. Open **Diagnostics** and read the status and last error.
6. Confirm that the selected singer and phonemizer use the same alias convention.

### Status messages

| Status | What to do |
|---|---|
| **Ready** | The current score can play. |
| **Rendering** | Wait for the edit, singer, tempo, or transpose change to finish. |
| **Waiting for buffer** | Playback started before the first rendered audio was ready. Wait for **READY** and trigger again if needed. |
| **Singer missing** | Choose the bundled singer or relink the external voicebank folder. |
| **Voicebank invalid** | Check the folder for valid metadata, `oto.ini`, and referenced WAV files. |
| **Alias missing** | Inspect the red note or phoneme and choose a compatible phonemizer, correct the lyric, or enter an exact alias. |
| **Import warning** | Read the import report. Supported score data was loaded, but some source settings were changed or omitted. |
| **Render error** | Read the last error in **Diagnostics**, correct the score or singer problem, then make an edit or reselect the singer to retry. |
| **Buffer underrun** | Playback requested audio that was not ready. Reduce rapid edits or tempo changes and check Rack's CPU load. |

### An external singer does not load after reopening a patch

External paths require confirmation after restore. Choose **Singer & phonemizer > Select or relink voicebank folder...** and select the bank again. The score remains in the module while the singer is missing.

### Lyrics render as missing aliases

The usual cause is a convention mismatch. Examples:

- Use **English to Japanese**, **Japanese Auto**, or **Japanese CVVC** with a Japanese bank.
- Use **EN X-SAMPA** only with a compatible X-SAMPA bank.
- Use **English VCCV** only with a compatible VCCV bank.
- Use **Direct Alias** when the lyrics already contain the bank's aliases.

If only one word fails, edit its **PHONEMES** field instead of changing the complete score's phonemizer.

### Imported timing or pronunciation differs from another editor

Vocal imports score data, not another editor's renderer, resampler, wavtool, flags, voice colors, or installed plugins. Read the import report, then check the selected singer and phonemizer. Use the lossless `.vocalrack` format when moving a complete Vocal setup between Rack patches.

## Singer Plate

**Cosmic Matter: Singer Plate** displays the portrait and name from a voicebank. It has no ports and does not produce or control audio.

Open its module menu to:

- Use bundled Adachi Rei or select an external singer folder.
- Fit the complete image or fill and crop the plate.
- Show or hide the singer name.

An invalid or missing portrait is replaced with a generic placeholder. Singer Plate can display the same bank used by Vocal, but the two modules do not need to be connected.

## Glossary

| Term | Meaning |
|---|---|
| **Alias** | The text name of a recorded sound in an UTAU voicebank. |
| **Phonemizer** | A rule set that converts lyrics and note context into voicebank aliases. |
| **Voicebank / singer** | A folder of recorded samples, timing metadata, and character information. |
| **Preutterance** | How far a phoneme begins before its musical position. |
| **Overlap** | How much a phoneme crossfades with the sound before it. |
| **PPQN** | Pulses per quarter note. It tells Vocal how many clock pulses make one beat. |
| **Section** | A named, non-overlapping range that can be selected and looped independently. |
| **Continuation** | A note that sustains the previous vowel without repeating its consonant. |

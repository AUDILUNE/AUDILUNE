# AUDILUNE v1.0 — User Manual

**Turn the M-VAVE FM-1 into an unrestricted 6-track synthesizer, sampler, and generative groovebox.**

---

## Contents

1. [Overview & Core Philosophy](#1-overview--core-philosophy)
2. [Visual Identity, Screen Bars & 8 Themes](#2-visual-identity-screen-bars--8-themes)
3. [Hardware Control Map](#3-hardware-control-map)
4. [The 16 Dedicated Pages](#4-the-16-dedicated-pages)
5. [Effects Page, 2D Chaos Pads & 110% Musical Overflow](#5-effects-page-2d-chaos-pads--110-musical-overflow)
6. [Chop Shop & User Samples (USR1–USR6)](#6-chop-shop--user-samples-usr1usr6)
7. [Scale Library & Wild Card Scales](#7-scale-library--wild-card-scales)
8. [Synthesis Engines, Analog Emulations, Klatt Speech & Sound Landscaper](#8-synthesis-engines-analog-emulations-klatt-speech--sound-landscaper)
9. [MIDI, USB-C Audio & Web Editor](#9-midi-usb-c-audio--web-editor)
10. [Flashing, Rescue & Stock Firmware Recovery](#10-flashing-rescue--stock-firmware-recovery)

---

## 1. Overview & Core Philosophy

> **"If you can hear it on the FM-1, you can record it."** — Primary performance knobs, **Full Keyboard**, multi-timbral track layers, live knob sweeps, and audio effects stay active and audible across all working pages, live playing, and recording—and everything audible can be captured into patterns or resampled on-device.

**AUDILUNE** transforms the **M-VAVE FM-1** into a standalone **6-track × 128-step** studio and live performance instrument featuring universal controls, zero two-button combo grammar, single-button page navigation, multi-timbral track layering, and real-time motion and step locking.

---

## 2. Visual Identity, Screen Bars & 8 Themes

* **High-Contrast Cosmological UI:** Pure `#000000` black canvas with `#FFFFFF` text, inverted highlight boxes, astronomical reticles, and lunar corner motifs.
* **Top Header, 14px Strip & Bottom HUD:** Persistent header showing Tempo (`BPM`), Transport state (`PLAY`/`STOP`/`REC`), Master Volume, and Clock Sync; a **14px Persistent Active-Selection Strip** highlighting the active track/parameter; and a bottom HUD displaying live parameter values.
* **8 Color Themes:** Configurable via **Global Page** (**GLO Button**), allowing users to switch live between **Teal** (default), **Orange**, **Burgundy**, **Purple**, **Pink**, **Blue**, **Mint**, and **Grey**.

---

## 3. Hardware Control Map

* **Master Knob & Select Knob:** **Master Knob** controls global output volume (`0%`–`100%`). **Select Knob** uses velocity-adaptive ballistic acceleration to scroll through lists, tracks, Chop Shop rows, or visualizer styles.
* **Presets Knob & Algorithm Knob:** **Presets Knob** cycles base synth, analog emulation, Klatt speech (`DECTALK`, `VOX-SING`, `VOX-BENT`, `PHRASE 1`–`PHRASE 8`), and sample presets (`USR1`–`USR6`), switching out of Drum Kit mode back to Synth mode. **Algorithm Knob** steps `0–5` morph active track presets across 6 genre variations (`AMBIENT`, `DOWNTEMPO`, `GLITCH HOP`, `PUNCHY`, `EXPERIMENTAL`, `INDUSTRIAL`), while steps `6–37` switch any track (`1–6`) into Drum Kit mode across all **32 Drum Kits**.
* **Octave Down / Octave Up & Full Keyboard:** Shifts keyboard range (`-3` to `+3` octaves) and triggers synth notes, ergonomic **16-key left-to-right drum pads** + **11 black keys**, multi-timbral layered tracks across all **SEL Button**-locked tracks simultaneously, chromatic samples, or chopped kits (`CHOP 01`–`16`).
* **Knobs 1–4:** Global Tempo (`Knob 1`), Metronome Volume (`Knob 2`, automatically active when **REC Button** is pressed), zipper-free exponential slewing Parameter/Overflow & ARP Morph adjustment (`Knob 3`), and Column/Sub-type/Step navigation (`Knob 4`).
* **SEL Button & Play/Stop / REC Buttons:** **SEL Button** locks/unlocks tracks (for multi-timbral keyboard/MIDI layering and simultaneous multi-track recording), steps, effects, or edits. **Play/Stop Button** starts/stops playback immediately. **REC Button** arms Wait-for-Note recording with audible metronome click across the active track and all **SEL Button**-locked tracks simultaneously (`1.5s` hold to erase).

---

## 4. The 16 Dedicated Pages

Pressing **Home Button** returns to **Live Play Page** from anywhere and cycles cleanly through **Visualizer Page** and all dedicated pages (**no two-button holds are ever required**):

1. **Startup Screen:** Displays the crescent moon logo (`assets/audilune_logo.jpg`) and 3D-embossed `AUDILUNE v1.0` title for `3,000 ms`.
2. **Studio Setup:** Pre-performance song & track workspace setup.
3. **Live Play Page (Home):** 6-track horizontal performance view displaying active Preset + Genre variation (or `DRUM KIT: [Name]`), Pattern (`A`–`F`), 14px Persistent Active-Selection Strip, and level meters.
4. **Visualizer Page:** Full-screen audio-reactive art (Header/HUD hidden) cycling 4 styles via **Select Knob** (*Lunar Oscilloscope*, *Orbital Spectrum Bars*, *Eclipse Moon Loop*, *Starfield Cascade*) with 60s idle screensaver trigger.
5. **Algorithm & Drum Kits:** Genre morphing (`AMBIENT`..`INDUSTRIAL`) & all **32 Drum Kits** mapped across the ergonomic 16-key left-to-right drum layout.
6. **Effects Page (FX Button):** Categorized effects browser (`0%–110%` overflow), **2D Chaos Pads**, and **Precision FX** active during live play and **REC Button** capture with **SEL Button** multi-locking.
7. **Punch-In FX:** Instant performance stutter, tape stop, reverse, sub-slam, and shimmer wash.
8. **Envelope Page (ENV Button):** `Attack`, `Decay`, `Sustain`, and `Release` controls with **SEL Button** multi-locking.
9. **Sound Landscaper:** **10 blendable environmental/drone layers** plus utilitarian psychoacoustic frequency layers.
10. **LFO Page (LFO Button):** `Waveform`, `Rate`, `Depth`, and `Destination` controls with **SEL Button** multi-locking.
11. **Edit Page (Tap Edit Button):** Surgical step and track operations (`Copy`, `Paste`, `Duplicate`, `Clear`, `Quantize`, `Humanize`, `Nudge`, `Transpose`, `Velocity`, `Gate`, `Probability`, `Ratchet`, `Reverse`, `Root Note`, `Scale Type`, `Chop Mode`).
12. **SEQ Page (SEQ Button):** **128-step polymetric overview** and pattern-chain (`A`–`F`) builder.
13. **Chop Shop Page (Hold Edit Button 1.2s):** Two-stage `#FFFFFF` waveform slicer and on-device sampler (`USR1`–`USR6`).
14. **Global Page (GLO Button):** System configuration (`Color Theme`, `MIDI Channels`, `MIDI Routing`, `Clock Sync`, `USB Audio I/O`).
15. **Save Page (Save Button):** 6 non-destructive SPI NOR Flash Vaults (`SLOT 1`–`6`).
16. **ARP Page (ARP Button):** Unified generative engine supporting **SONG**, **TRACK** (single or **SEL Button**-locked), and **STEP**
(single or **SEL Button**-locked) scopes, with **Knob 3** controlling Morph Amount (`0%–100%`) away from the original composition and resolving cleanly back at `0%`.
12. **SEQ Page (SEQ Button):** Pattern-chain (`A`–`F`) and step-length (`16`–`128`) builder.

---

## 5. Effects Page, 2D Chaos Pads & 110% Musical Overflow

Includes categorized processing across **Time, Pitch & Groove**, **Granular, Glitch & Rhythmic**, **Spatial & Ambient (`0%`–`110%` Overflow)**, **Modulation & Stereo Movement**, **Filters & EQ**, and **Saturation, Lo-Fi & Dynamics**, plus interactive **2D Chaos Pads** and **Precision FX**. Sweeping past `100%` into `101%–110%` unlocks fixed-buffer shimmer feedback loops, harmonic delay cascades, and rich stereo bloom without digital clipping.

---

## 6. Chop Shop & User Samples (`USR1`–`USR6`)

* **Stage 1 & Stage 2 Slicer:** Source selection (`Chop Song`, `Chop Track`, `Chop Sample`) followed by `#FFFFFF` waveform editing with slice modes (`4 EQUAL`, `8 EQUAL`, `16 EQUAL`, `AUTO TRANSIENT`, `MANUAL`) and nudge/trim controls (`Start [` / `End ]`).
* **Playback & Direct Recording:** Chromatic playback across `USR1`–`USR6` or slice mapping across `CHOP 01`–`16`. Direct Wait-for-Note audio recording up to `5.0s` (`40 KiB` per slot) from USB-C audio input.

---

## 7. Scale Library & Wild Card Scales

* **Core Scales (12):** Chromatic, Major, Natural Minor, Harmonic Minor, Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, Major Pentatonic, Minor Pentatonic, Blues.
* **Atmospheric & World Scales (6):** Hirajoshi, In-Sen, Phrygian Dominant, Double Harmonic Major, Whole Tone, Diminished Half-Whole.
* **Wild Card Scales (4):** Prometheus (Mystic), Enigmatic, Tritone Hexatonic, and Lunar Eclipse (Custom 5-Note).

---

## 8. Synthesis Engines, Analog Emulations, Klatt Speech & Sound Landscaper

* **13 Unrestricted Synthesis Engines:** Simultaneous access across all 6 tracks to `ANALOG` (including architectural emulations of the **Minimoog Model D** and **EMS VCS 3 / Synthi AKS**), `DIGITAL`, `PHASE`, `LOFI`, `SAMPLE`, `VOICE` (featuring the zero-SRAM **Klatt Formant Speech Engine** with `DECTALK`, `VOX-SING`, `VOX-BENT`, and `PHRASE 1`–`PHRASE 8` genre-locked combinatorial speech), `TRIO`, `WHEEL`, `GRAIN`, `FM6`, `PHYS`, `NOISE`, and `DRUMS` (**32 Drum Kits**).
* **Sound Landscaper & Psychoacoustic Layers:** Blend up to **10 environmental & drone layers** alongside utilitarian psychoacoustic frequency layers.
* **Isolated Kick & Bass Pools & Sub-Bass Centering:** Isolated Kick & Bass voice pools prevent low-end voice stealing while phase-clean sub-bass centering anchors low frequencies cleanly in mono.

---

## 9. MIDI, USB-C Audio & Web Editor

* **MIDI & USB Audio:** Mapped to MIDI Channels 1–6 (plus GM Drum Channel 10), class-compliant USB-C audio (`44.1 kHz` / `48 kHz`), and cross-platform web editor support (`web/editor.html`) for desktop and **Android Chrome USB-C OTG** with **offline PWA** caching, **5-Second Smart Auto-Fit** (`USR1`–`USR6`), **6 flash vault** management (`SLOT 1`–`6`), and SysEx backup/restore (`OBJ_VOXBANK`).

---

## 10. Flashing, Rescue & Stock Firmware Recovery

* **First-Time Preparation & One-Click Flashing:** Install **M-Flasher** (safety recovery bootloader) and revert to official **Version 15** via **M-UPGRADE** first (recommended for both older and newer FM-1 revisions). Then power on normally, connect over USB-C, and flash `AUDILUNE-v1.0.fwsc` in one click via **The Firmware Hub** / WebMIDI with **no buttons required**.
* **Future Updates & Recovery:** Flash newer AUDILUNE updates straight over the top without reverting to V15 (preserving `SLOT 1`–`6` and `USR1`–`6`). For manual recovery only, hold **Octave Down + Octave Up** for `5 seconds` while stopped to enter `UBOOT`, hold **Octave Down** on boot for USB Rescue, or return to official firmware via **M-UPGRADE** (`FM-1.fwsc` V16).

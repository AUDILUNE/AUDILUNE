# AUDILUNE v1.0 — User Manual

**Turn the M-VAVE FM-1 into an unrestricted 6-track synthesizer, sampler, and generative groovebox.**

---

## Contents

1. [Overview & Core Philosophy](#1-overview--core-philosophy)
2. [Visual Identity, Screen Bars & 8 Themes](#2-visual-identity-screen-bars--8-themes)
3. [Hardware Control Map](#3-hardware-control-map)
4. [The 12 Dedicated Pages](#4-the-12-dedicated-pages)
5. [Effects Page & 110% Musical Overflow](#5-effects-page--110-musical-overflow)
6. [Chop Shop & User Samples (USR1–USR6)](#6-chop-shop--user-samples-usr1usr6)
7. [Scale Library & Wild Card Scales](#7-scale-library--wild-card-scales)
8. [Synthesis Engines, Macro-Genres & Smart Voicing](#8-synthesis-engines-macro-genres--smart-voicing)
9. [MIDI, USB-C Audio & Web Editor](#9-midi-usb-c-audio--web-editor)
10. [Flashing, Rescue & Stock Firmware Recovery](#10-flashing-rescue--stock-firmware-recovery)

---

## 1. Overview & Core Philosophy

> **"If you can hear it on the FM-1, you can record it."** — Primary performance knobs, **Full Keyboard**, multi-timbral track layers, live knob sweeps, and audio effects stay active and audible across all working pages, live playing, and recording—and everything audible can be captured into patterns or resampled on-device.

**AUDILUNE** transforms the **M-VAVE FM-1** into a standalone **6-track × 128-step** studio and live performance instrument featuring universal controls, single-button page navigation, multi-timbral track layering, and real-time motion and step locking.

---

## 2. Visual Identity, Screen Bars & 8 Themes

* **High-Contrast Cosmological UI:** Pure `#000000` black canvas with `#FFFFFF` text, inverted highlight boxes, astronomical reticles, and lunar corner motifs.
* **Top Header & Bottom HUD:** Persistent header showing Tempo (`BPM`), Transport state (`PLAY`/`STOP`/`REC`), Master Volume, and Clock Sync; a bottom HUD displaying live parameter values.
* **8 Color Themes:** Configurable via **Global Page** (**GLO Button**), allowing users to switch live between **Teal** (default), **Orange**, **Burgundy**, **Purple**, **Pink**, **Blue**, **Mint**, and **Grey**.

---

## 3. Hardware Control Map

* **Master Knob & Select Knob:** Controls global output volume (`0%`–`100%`) and scrolls through lists, tracks, Chop Shop rows, or visualizer styles.
* **Presets Knob & Algorithm Knob:** **Presets Knob** cycles base synth and sample presets (`USR1`–`USR6` samples and chop kits), switching out of Drum Kit mode back to Synth mode. **Algorithm Knob** steps `0–5` morph active track presets across 6 genre variations (`AMBIENT`, `DOWNTEMPO`, `GLITCH HOP`, `PUNCHY`, `EXPERIMENTAL`, `INDUSTRIAL`), while steps `6–37` switch any track (`1–6`) into Drum Kit mode across all **32 Drum Kits**.
* **Octave Down / Octave Up & Full Keyboard:** Shifts keyboard range (`-3` to `+3` octaves) and triggers synth notes, multi-timbral layered tracks across all **SEL Button**-locked tracks simultaneously, chromatic samples, or chopped kits (`CHOP 01`–`16`).
* **Knobs 1–4:** Global Tempo (`Knob 1`), Metronome Volume (`Knob 2`, automatically active when **REC Button** is pressed), Parameter/Overflow & ARP Morph adjustment (`Knob 3`), and Column/Sub-type/Step navigation (`Knob 4`).
* **SEL Button & Play/Stop / REC Buttons:** **SEL Button** locks/unlocks tracks (for multi-timbral keyboard/MIDI layering and simultaneous multi-track recording), steps, effects, or edits. **Play/Stop Button** starts/stops playback immediately. **REC Button** arms Wait-for-Note recording with audible metronome click across the active track and all **SEL Button**-locked tracks simultaneously (`1.5s` hold to erase).

---

## 4. The 12 Dedicated Pages

Pressing **Home Button** toggles cleanly between **Live Play Page** and **Visualizer Page** (and exits any dedicated page straight to **Live Play Page**):

1. **Startup Screen:** Displays the crescent moon logo and `AUDILUNE v1.0` title for `3,000 ms`.
2. **Live Play Page (Home):** 6-track horizontal performance view displaying active Preset + Genre variation (or `DRUM KIT: [Name]`), Pattern (`A`–`F`), and level meters.
3. **Visualizer Page:** Full-screen audio-reactive art (Header/HUD hidden) cycling 4 styles via **Select Knob** (*Lunar Oscilloscope*, *Orbital Spectrum Bars*, *Eclipse Moon Loop*, *Starfield Cascade*) with 60s idle screensaver trigger.
4. **Effects Page (FX Button):** Categorized effects browser (`0%–110%` overflow) active during live play and **REC Button** capture with **SEL Button** multi-locking.
5. **Envelope Page (ENV Button):** `Attack`, `Decay`, `Sustain`, and `Release` controls with **SEL Button** multi-locking.
6. **LFO Page (LFO Button):** `Waveform`, `Rate`, `Depth`, and `Destination` controls.
7. **Edit Page (Tap Edit Button):** Surgical step and track operations (`Copy`, `Paste`, `Duplicate`, `Clear`, `Quantize`, `Humanize`, `Nudge`, `Transpose`, `Velocity`, `Gate`, `Probability`, `Ratchet`, `Reverse`, `Root Note`, `Scale Type`, `Chop Mode`).
8. **Chop Shop Page (Hold Edit Button 1.2s):** Two-stage waveform slicer and on-device sampler.
9. **Global Page (GLO Button):** System configuration (`Color Theme`, `MIDI Channels`, `MIDI Routing`, `Clock Sync`, `USB Audio I/O`).
10. **Save Page (Save Button):** 6 non-destructive SPI NOR Flash Vaults (`SLOT 1`–`6`).
11. **ARP Page (ARP Button):** Unified generative engine supporting **SONG**, **TRACK** (single or **SEL Button**-locked), and **STEP** (single or **SEL Button**-locked) scopes, with **Knob 3** controlling Morph Amount (`0%–100%`) away from the original composition and resolving cleanly back at `0%`.
12. **SEQ Page (SEQ Button):** Pattern-chain (`A`–`F`) and step-length (`16`–`128`) builder.

---

## 5. Effects Page & 110% Musical Overflow

Includes categorized processing across **Time, Pitch & Groove**, **Granular, Glitch & Rhythmic**, **Spatial & Ambient (`0%`–`110%` Overflow)**, **Modulation & Stereo Movement**, **Filters & EQ**, and **Saturation, Lo-Fi & Dynamics**. Sweeping past `100%` into `101%`–`110%` unlocks shimmer feedback loops, harmonic delay cascades, and rich stereo bloom without clipping.

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

## 8. Synthesis Engines, Macro-Genres & Smart Voicing

Provides simultaneous access to **13 synthesis engines** (`ANALOG`, `DIGITAL`, `PHASE`, `LOFI`, `SAMPLE`, `VOICE`, `TRIO`, `WHEEL`, `GRAIN`, `FM6`, `PHYS`, `NOISE`, `DRUMS`), environmental/animal soundscapes, user samples, and automatic voice allocation.

---

## 9. MIDI, USB-C Audio & Web Editor

* **MIDI & USB Audio:** Mapped to MIDI Channels 1–6 (plus GM Drum Channel 10), class-compliant USB-C audio (`44.1 kHz` / `48 kHz`), and web editor support with **5-Second Smart Auto-Fit**.

---

## 10. Flashing, Rescue & Stock Firmware Recovery

* **Flashing & Recovery:** Hold **Octave Down + Octave Up** for `5 seconds` to enter `UBOOT` via WebMIDI, hold **Octave Down** on boot for USB Rescue, or return to stock firmware using **M-UPGRADE** (`FM-1.fwsc` V16).


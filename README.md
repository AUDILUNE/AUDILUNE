<p align="center"><img src="assets/audilune_logo.jpg" alt="AUDILUNE" width="220"></p>

<h1 align="center">AUDILUNE v1.0</h1>

<p align="center"><b>Turn the M-VAVE FM-1 into an unrestricted 6-track synthesizer, sampler, and generative groovebox.</b><br>
Created and designed by <b>David MacArthur</b> (AUDILUNE). Free and open source (GPL-3.0), built on the M-VAVE FM-1 engine foundation by Leo Kuroshita / Hügelton Instruments and Sloop.</p>

<p align="center">
<a href="MANUAL.md"><b>User Manual</b></a> ·
<a href="../../releases"><b>Download Releases (.fwsc)</b></a> ·
<a href="https://github.io"><b>FM-1 Browser Emulator</b></a> ·
<a href="https://thefirmwarehub.com"><b>The Firmware Hub</b></a> ·
<a href="../../issues"><b>Report an Issue</b></a>
</p>
---

> **Core Philosophy:** *"If you can hear it on the FM-1, you can record it."* — Primary performance knobs, **Full Keyboard**, multi-timbral track layers, live knob sweeps, and audio effects stay active and audible across all working pages, live playing, and recording—and everything audible can be captured into patterns or resampled on-device.

**AUDILUNE** transforms the **M-VAVE FM-1** into a fast, immediate **6-track × 128-step** standalone studio and live performance instrument. Every track has full, unrestricted access to all **13 synthesis engines**, classic analog and FM synth emulators, atmospheric drones, natural & animal soundscapes, **6 floating user sample slots** (`USR1`–`USR6`), poly-wavetables, and all **32 Drum Kits**—with **zero track-role locks**, **zero two-button holds**, real-time **Algorithm Knob** genre & drum-kit morphing, multi-timbral track layering, an on-device **Chop Shop** waveform slicer, and **Smart Automatic Voice Allocation** that keeps sub-basses tight while letting chords and shimmer delays bloom wide.

---

## Contents

1. [Core Features](#core-features)
2. [Dedicated Pages & Navigation](#dedicated-pages--navigation)
3. [Hardware Control Map](#hardware-control-map)
4. [Installation & Flashing Guide](#installation--flashing-guide)
5. [Chop Shop & Personal Samples (USR1–USR6)](#chop-shop--personal-samples-usr1usr6)
6. [MIDI & USB-C Audio](#midi--usb-c-audio)
7. [Rescue & Returning to Stock Firmware](#rescue--returning-to-stock-firmware)
8. [Credits & Licence](#credits--licence)

---

## Core Features

* **If You Can Hear It on the FM-1, You Can Record It:** Live **Full Keyboard** play, multi-timbral layered tracks, active effects, and live knob sweeps remain audible across every working page and record directly into step automation or on-device samples when **REC Button** is pressed.
* **High-Contrast Cosmological UI & 8 Themes:** Pure `#000000` black canvas with auto-dimming disabled, inverted white highlight boxes, stippled pixel level meters, astronomical grid reticles, and **8 selectable themes** (`Teal` default, `Orange`, `Burgundy`, `Purple`, **Pink**, `Blue`, `Mint`, `Grey`) on **Global Page**.
* **12 Dedicated Pages & Clean Home Toggling:** Instant single-button toggling between **Live Play Page**, **Visualizer Page** (4 audio-reactive styles + 60s idle screensaver), **Effects Page**, **Envelope Page**, **LFO Page**, **Edit Page**, **Chop Shop Page**, **Global Page**, **Save Page**, **ARP Page**, and **SEQ Page**—with **Home Button** toggling cleanly between **Live Play Page** and **Visualizer Page**.
* **Unrestricted 6-Track Engine & 32 Drum Kits Everywhere:** Zero track-role locks. All **6 tracks** have simultaneous access to all **13 synthesis engines**, classic synth emulators, drones, environmental & animal soundscapes (`STORM WIND`, `THUNDER`, `CREEK`, `BIRDSONG`, `CICADAS`, `WHALE SONG`, `FOLEY CRK`), user samples (`USR1`–`USR6`), poly-wavetables, and all **32 Drum Kits** (selected via steps `6–37` of **Algorithm Knob**).
* **Multi-Timbral Track Layering & Simultaneous Recording:** Lock multiple tracks (up to all 6) with **SEL Button** to trigger and layer them 
simultaneously from **Full Keyboard**, and press **REC Button** to record multi-timbral performances across all **SEL Button**-locked tracks at once with automatic tempo-synced metronome click.
* **Preset-Aware Genre Morphing & 110% Musical Overflow:** Steps `0–5` of **Algorithm Knob** morph any preset in real time across `AMBIENT`, `DOWNTEMPO`, `GLITCH HOP`, `PUNCHY`, `EXPERIMENTAL`, and `INDUSTRIAL`. Sweeping spatial effects past `100%` into `101%–110%` engages musical shimmer feedback, delay cascades, and stereo bloom without digital clipping.
* **Unified SONG / TRACK / STEP Generative Morphing (`0%–100%`):** On **ARP Page**, select **SONG**, **TRACK** (single or **SEL Button**-locked), or **STEP** (single or **SEL Button**-locked) scope and sweep **Knob 3** (`0%–100%`) to generate or progressively morph away from your original composition—returning **Knob 3** to `0%` or turning ARP off resolves cleanly back to the original unmorphed composition.
* **On-Device Chop Shop & 5-Second Smart Auto-Fit:** Slice songs, tracks, or samples directly on the `#FFFFFF` waveform display (`4/8/16 EQUAL`, `AUTO TRANSIENT`, `MANUAL`), play single samples chromatically across all keys or 16 chops across the white keys, and upload files up to 5.0 seconds per slot (`USR1`–`USR6`) with automatic silence trimming and adaptive sample-rate fitting in the web editor.
* **22 Scales & 6 Non-Destructive Flash Vaults:** Quantize tracks or external MIDI to **22 scales** on **Edit Page** (including 4 Wild Card Scales: *Prometheus*, *Enigmatic*, *Tritone Hexatonic*, and *Lunar Eclipse*), and save/recall complete projects across 6 SPI NOR flash vaults (`SLOT 1`–`6`) on **Save Page**.

---

## Dedicated Pages & Navigation

Pressing **FX Button**, **ENV Button**, **LFO Button**, **Edit Button**, **GLO Button**, **Save Button**, **ARP Button**, or **SEQ Button** opens its page; pressing it again returns immediately to **Live Play Page**. Pressing **Home Button** toggles cleanly between **Live Play Page** and **Visualizer Page** (and exits any dedicated page straight to **Live Play Page**). **No two-button holds are ever required.**

| Control / Button | Function |
| :--- | :--- |
| **Live Play Page (Home)** | 6-track horizontal performance matrix showing active Preset + Genre variation (or `DRUM KIT: [Name]`) and Pattern (`A`–`F`) |
| **Visualizer Page** | Full-screen audio-reactive art (*Lunar Oscilloscope*, *Orbital Spectrum Bars*, *Eclipse Moon Loop*, *Starfield Cascade*) |
| **FX Button** | Full categorized **Effects Page** (`0%–110%` overflow, active during live play & recording with **SEL Button** multi-lock) |
| **ENV Button** | **Envelope Page** (`Attack`, `Decay`, `Sustain`, `Release` with **SEL Button** multi-lock) |

| **LFO Button** | **LFO Page** (`Waveform`, `Rate`, `Depth`, `Destination` with **SEL Button** multi-lock) |
| **Tap Edit Button** | **Edit Page** (Copy, Paste, Duplicate, Clear, Quantize, Humanize, Nudge, Transpose, Probability, Ratchet, Reverse, **22-Scale Library** including *Prometheus*, *Enigmatic*, *Tritone Hexatonic* & *Lunar Eclipse*, plus **Chop Mode**: Kit / Chromatic) |
| **Hold Edit Button (1.2s)** | **Chop Shop Page** (2-stage `#FFFFFF` waveform slicer & on-device sampler) |
| **GLO Button** | **Global Page** (8 Color Themes, MIDI Channels, MIDI Routing, Clock Sync, USB Audio I/O) |
| **Save Button** | **Save Page** (6 SPI NOR Flash Vaults `SLOT 1`–`6`; toggle Save/Load with **Knob 4**, press **Save Button** to execute) |
| **ARP Button** | **ARP Page** (Unified **SONG / TRACK / STEP** generative engine with **Knob 3** `0%–100%` Morph Amount) |
| **SEQ Button** | **SEQ Page** (Pattern-chain `A`–`F` and step-length `16`–`128` builder for selected tracks) |
| **Home Button** | Toggles cleanly between **Live Play Page** and **Visualizer Page** (and exits any dedicated page straight to **Live Play Page**) |

---

## Hardware Control Map

* **Master Knob:** Global master output volume (`0%` mute to `100%` full).
* **Select Knob:** Scrolls Up/Down through list items, tracks, or chop rows, and cycles the 4 **Visualizer Page** styles.
* **Presets Knob:** Cycles sequentially through all base synth and sample presets (including `USR1`–`USR6` samples and chop kits) on the active track, switching out of Drum Kit mode back to Synth mode.
* **Algorithm Knob:** Steps `0–5` morph the active track's selected preset in real time across 6 genre variations (`AMBIENT`, `DOWNTEMPO`, `GLITCH HOP`, `PUNCHY`, `EXPERIMENTAL`, `INDUSTRIAL`); steps `6–37` switch any track (`1–6`) into Drum Kit mode and cycle through all **32 Drum Kits**.
* **Octave Down / Octave Up Buttons:** Shifts **Full Keyboard** octave range (`-3` to `+3` octaves) on the active track.
* **Full Keyboard:** Pure musical triggering across all working pages with zero secondary key combos—notes on synth/drum tracks, multi-timbral layering across all **SEL Button**-locked tracks simultaneously, chromatic pitch-tracking across all white and black keys on single `USR1`–`USR6` samples, and `CHOP 01`–`16` across the white keys on chopped kits.
* **Knob 1:** Global Tempo (`BPM`) across all working pages.
* **Knob 2:** Metronome Volume (`0%` mute to `100%` full) synced to Tempo and audible when **REC Button** is pressed.
* **Knob 3:** Adjusts value/amount (`0%–110%` musical overflow) of highlighted or **SEL Button**-locked parameters, and controls Generative Morph Amount (`0%–100%`) on **ARP Page**.
* **Knob 4:** Navigates Left/Right across columns, sub-types, options, steps, or Chop Shop nudge/trim positions.
* **SEL Button:** Locks/unlocks highlighted tracks (for multi-timbral keyboard/MIDI layering and simultaneous multi-track recording), steps, effects, or edits into a multi-selection group (and cycles `Whole Chop` → `Start [` → `End ]` trim modes in **Chop Shop**).
* **Play/Stop Button:** Starts/stops playback across all active 6 tracks immediately and disarms recording on stop.
* **REC Button:** Arms **Wait-for-Note** recording from Step 1 with audible metronome click across the active track and all **SEL Button**-locked tracks simultaneously (or punches in live during playback); hold `1.5s` to erase active track, selected steps, or highlighted `USR` sample slot.

---

## Installation & Flashing Guide

1. **Charge & Connect Your FM-1:** Make sure your M-VAVE FM-1 is charged and connect it directly using a **data-rated USB-C cable** (avoid unpowered USB hubs). Open **Google Chrome** or **Microsoft Edge** with only one MIDI tab open.
2. **First-Time Safety Prep (Recommended for Both Older & Newer FM-1 Revisions):**
   * **Install M-Flasher:** Run **M-Flasher** first to install the safety recovery bootloader on your unit.
   * **Revert to Version 15 Baseline:** Use **M-UPGRADE** to revert the unit to official **Version 15** (`FM-1.fwsc` V15) so the partition table matches the baseline expected by custom firmwares. *(You only need to do this once—skip straight to Step 3 if you are already running AUDILUNE, Sloop, or Felucca.)*
3. **Download `.fwsc` (or Preview in Emulator):** Download the latest `AUDILUNE-v1.0.fwsc` binary from [Releases](../../releases) (or test it first in the [FM-1 Browser Emulator](https://github.io)).
4. **One-Click USB-C Flash (No Buttons Required):** With the FM-1 powered on normally, open **The Firmware Hub** (or WebMIDI flasher) in Chrome/Edge, select `AUDILUNE-v1.0.fwsc`, and click **Flash**—the browser automatically reboots the FM-1 into update mode and writes the firmware.
   * *Note:* If Windows or Android prompts you to format a newly mounted drive, click **Cancel**. **Do not unplug or power off the unit while writing is in progress.**
5. **Future Updates & Switching Custom Firmwares:**
   * **Updating AUDILUNE:** Flash newer `AUDILUNE` `.fwsc` builds straight over the top via **The Firmware Hub** without reverting to V15—your saved projects (`SLOT 1`–`6`) and user samples (`USR1`–`6`) stay intact.
   * **Switching Between Different Custom Firmwares:** Reverting to **Version 15** via **M-UPGRADE** before switching between completely different custom firmwares is good practice to ensure a clean settings slate.

---

## Chop Shop & Personal Samples (`USR1`–`USR6`)

* **6 Dynamic Sample Slots (`40 KiB` Each):** AUDILUNE provides **6 floating user sample slots (`USR1`–`USR6`)** in dedicated flash (`0xA0000`–
`0xDBFFF`), each holding up to **5.0 seconds** of audio usable or stackable across all 6 tracks.
* **On-Device Chop Shop (`Hold Edit Button 1.2s`):** Bounce the current song, a saved project (`SLOT 1`–`6`), an individual track (`TRACK 1`–`6`), or an existing `USR1`–`USR6` sample directly into the `#FFFFFF` waveform slicer (`4/8/16 EQUAL`, `AUTO TRANSIENT`, `MANUAL`), trim start/end points, and save straight to `USR1`–`USR6` or load onto any track via **Save Button**.
* **Web Sample Editor & 5-Second Smart Auto-Fit:** Open `web/editor.html` in Chrome or Edge over USB-C and drop in WAV/MP3/AIFF/FLAC/OGG files. **5-Second Smart Auto-Fit** automatically trims silence, caps length at `5.0s` with an anti-click fade, and adapts the sample rate (`11.025 kHz`–`44.1 kHz`) so any sample up to 5 seconds fits `USR1`–`USR6` cleanly.
* **Direct USB-C Audio Sampling:** Select an empty `USR1`–`USR6` slot and press **REC Button** to arm Wait-for-Note recording directly from USB-C audio input; hold **REC Button (`1.5s`)** to clear the slot.

---

## MIDI & USB-C Audio

* **Plug-and-Play MIDI Defaults:** Boots with **Tracks 1–6 mapped to MIDI Channels 1–6** (plus **GM Channel 10** drum support), automatic USB-C clock/transport pass-through, and live scale quantization.
* **Global Page (`GLO Button`):** Turn **Select Knob** to choose Track 1–6 and use **Knob 4** to configure Send/Receive/Internal routing. Fixed track CCs: `CC 10` (Pan), `CC 74` (Filter), `CC 7` (Level), `CC 93` (Chorus).
* **USB-C Class-Compliant Audio:** Streams stereo audio over USB-C (`44.1 kHz` / `48 kHz`) with no drivers required on Windows, macOS, Linux, or Android.

---

## Rescue & Returning to Stock Firmware

* **Built-In Boot-Loop Protection:** If startup is ever interrupted, AUDILUNE's boot guard automatically enters recovery/bootloader mode after repeated failed boots.
* **Manual Update Mode:** Hold **Octave Down + Octave Up** for 5 seconds while stopped, or hold **Octave Down** at power-on to enter USB Rescue.
* **Returning to Official Firmware:** Use M-VAVE's **M-UPGRADE** utility with the official `FM-1.fwsc` (V16) package.

> Custom firmware is installed at your own risk.

---

## Credits & Licence

* **AUDILUNE v1.0 Concept, UI/UX & Direction** — Created by **AUDILUNE** (`@AUDILUNE`), designed around a cosmological `#000000` aesthetic, immediate 12-page navigation, and an unrestricted 6-track workflow.
* **AI Pair-Programming & Code Assistance** — Developed with step-by-step firmware engineering, DSP calibration, and documentation assistance from **Google Gemini**.
* **Platform, Tooling & Hardware Ecosystem:**
  * **GitHub & GitHub Actions** — Source hosting, version control, and automated cloud firmware builds.
  * **M-VAVE (Sinco Intelligent Technology)** — Hardware platform for the **M-VAVE FM-1** pocket synthesizer and groovebox.
  * **Community Tools** — Browser emulation via [fm1-emu](https://github.io) (`@joakimeriksson`) and community firmware distribution via [The Firmware Hub](https://thefirmwarehub.com).
* **Upstream Open-Source Firmware Foundation:**
  * **[Felucca](https://github.com)** by **Leo Kuroshita (`@kurogedelic` / Hügelton Instruments)** — Core DSP synthesis engines, sequencer clock, USB-C audio/MIDI stack, and hardware abstraction layer.
  * **[Sloop](https://github.com)** by **`@isod89`** — Extended FM-1 firmware architecture, web editor protocol, and UI framework.
  * **Community Contributors** — Dual-core audio processing, boot-guard safety, and MIDI enhancements by `@andrerezemini` (`sloop-lab`), `@charlesvestal` (`X0X`), `@keremimo` (`Melodee`), `@renebohne`, `@juanjiant-bit` (`JIANT FM`), and `@ordenador`.
* **Licence:** Released under **GPL-3.0-only** (see [LICENSE](LICENSE)). M-VAVE and FM-1 are trademarks of their respective owners; AUDILUNE is an independent open-source project.

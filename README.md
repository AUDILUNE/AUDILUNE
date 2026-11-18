<p align="center"><img src="assets/audilune_logo.jpg" alt="AUDILUNE" width="220"></p>

<h1 align="center">AUDILUNE v1.0</h1>

<p align="center"><b>Turn the M-VAVE FM-1 into an unrestricted 6-track synthesizer, sampler, and generative groovebox.</b><br>
Free and open source (GPL-3.0), built on the M-VAVE FM-1 engine foundation by Leo Kuroshita / Hügelton Instruments and Sloop.</p>

<p align="center">
<a href="MANUAL.md"><b>User Manual</b></a> ·
<a href="../../releases"><b>Download Releases (.fwsc)</b></a> ·
<a href="https://github.io"><b>FM-1 Browser Emulator</b></a> ·
<a href="https://thefirmwarehub.com"><b>The Firmware Hub</b></a> ·
<a href="../../issues"><b>Report an Issue</b></a>
</p>

---

**AUDILUNE** transforms the **M-VAVE FM-1** into a fast, immediate **6-track × 128-step** standalone studio and live performance instrument. Every track has full, unrestricted access to all **13 synthesis engines**, classic analog and FM synth emulators, atmospheric drones, natural & animal soundscapes, **6 floating user sample slots** (`USR1`–`USR6`), poly-wavetables, and 16-lane drum kits—with **zero track-role locks**, **zero two-button holds**, real-time **Algorithm Knob** genre morphing, an on-device **Chop Shop** waveform slicer, and **Smart Automatic Voice Allocation** that keeps sub-basses tight while letting chords and shimmer delays bloom wide.

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

* **High-Contrast Cosmological UI & 8 Themes:** Pure `#000000` black canvas with auto-dimming disabled, inverted white highlight boxes, stippled pixel level meters, astronomical grid reticles, and **8 selectable themes** (`Teal`, `Orange`, `Burgundy`, `Purple`, `Pink`, `Blue`, `Mint`, `Grey`) on **Global Page**.
* **12 Dedicated Pages:** Instant single-button toggling between **Live Play Page**, **Visualizer Page** (4 audio-reactive styles + idle screensaver), **Effects Page**, **Envelope Page**, **LFO Page**, **Edit Page**, **Chop Shop Page**, **Global Page**, **Save Page**, **ARP Page**, and **SEQ Page**—with universal **Knob 1** (BPM), **Knob 2** (Metronome Volume), and live **Full Keyboard** responsiveness across every working page.
* **Unrestricted 6-Track Engine:** Zero track-role locks. All **6 tracks** have simultaneous access to all **13 synthesis engines**, classic synth emulators, drones, environmental & animal soundscapes (`STORM WIND`, `THUNDER`, `CREEK`, `BIRDSONG`, `CICADAS`, `WHALE SONG`, `FOLEY CRK`), user samples (`USR1`–`USR6`), poly-wavetables, and 16-lane drum kits.
* **Preset-Aware Genre Morphing & 110% Musical Overflow:** Turn **Algorithm Knob** to morph any preset in real time across `AMBIENT`, `DOWNTEMPO`, `GLITCH HOP`, `PUNCHY`, `EXPERIMENTAL`, and `INDUSTRIAL`. Sweeping spatial effects past `100%` into `101%–110%` engages musical shimmer feedback, delay cascades, and stereo bloom without digital clipping.
* **On-Device Chop Shop & 5-Second Smart Auto-Fit:** Slice songs, tracks, or samples directly on the `#FFFFFF` waveform display (`4/8/16 EQUAL`, `AUTO TRANSIENT`, `MANUAL`), play single samples chromatically across all keys or 16 chops across the white keys, and upload files up to 5.0 seconds per slot (`USR1`–`USR6`) with automatic silence trimming and adaptive sample-rate fitting in the web editor.
* **22 Scales & Role-Aware Generative ARP:** Quantize tracks or external MIDI to **22 scales** on **Edit Page** (including 4 Wild Card Scales: *Prometheus*, *Enigmatic*, *Tritone Hexatonic*, and *Lunar Eclipse*), or multi-lock tracks/steps on **ARP Page** to generate evolving music anchored to your active tracks.
* **6 Non-Destructive Flash Save Vaults:** Save and recall complete projects across 6 dedicated SPI NOR flash vaults (`SLOT 1`–`6`) on **Save Page**.

---

## Dedicated Pages & Navigation

Pressing **FX Button**, **ENV Button**, **LFO Button**, **Edit Button**, **GLO Button**, **Save Button**, **ARP Button**, or **SEQ Button** opens its page; pressing it again returns immediately to **Live Play Page**. Pressing **Home Button** from any dedicated page returns first to **Live Play Page**, and subsequent presses cycle sequentially through **Visualizer Page** and all remaining pages. **No two-button holds are ever required.**

| Control / Button | Function |
| :--- | :--- |
| **Live Play Page (Home)** | 6-track horizontal performance matrix showing active Preset, Algorithm variation, and Pattern (`A`–`F`) |
| **Visualizer Page** | Full-screen audio-reactive art (*Lunar Oscilloscope*, *Orbital Spectrum Bars*, *Eclipse Moon Loop*, *Starfield Cascade*) |
| **FX Button** | Full categorized **Effects Page** (6 categories, `0%–110%` overflow, **SEL Button** multi-lock) |
| **ENV Button** | **Envelope Page** (`Attack`, `Decay`, `Sustain`, `Release` with **SEL Button** multi-lock) |

| **Tap `EDIT` → `PIT`** | Transpose active step pitch with Select Encoder |
| **Tap `HOME`** | Toggle between Play Screen and Full-Screen Visualizer |
| **Hold `HOME` (3s)** | Toggle **Infinite Algorithmic Compositions Engine** |
| **Tap `RECORD`** | Arm **Wait-for-Note** capture (auto-starts from Step 1 on first key or MIDI note) |
| **Hold `RECORD` (1.5s)** | Erase active track (Play Screen) or active step (Edit Screen) |
| **Tap `STOP`** | Halt playback/recording and unarm |
| **Tap `FX` → `OP1`–`OP6` (Play)** | Engage Master Punch-In FX (`OP1` Stutter, `OP2` Tape Stop, `OP3` Lo-Fi, `OP4` Reverse, `OP5` Shimmer Wash, `OP6` Sub-Octave Slam) |
| **Tap `FX` → `OP1`–`OP6` (Edit)** | Lock FX to step; tap `Octave Up/Down` to paint FX across continuous steps |
| **Tap `ARP` → `OP1`–`OP6`** | Toggle **Generative Scale Anchor** drift on selected tracks (disabling triggers 1-bar Back-Home recovery) |
| **Tap `SEQUENCE` → `OP1`–`OP6` → `SEQUENCE`** | Queue pattern chain (up to 16 blocks) at end of 128-step pattern |
| **Hold `SEQUENCE` (blink) → `OP1`–`OP6`** | Snap to pattern at next 16-step bar boundary |
| **Tap `SAVE` → `OP1`–`OP6`** | Save project to Flash Vault 1–6 |
| **Hold `SAVE` (3s blink) → `OP1`–`OP6`** | Load project from Flash Vault 1–6 |

---

## Knobs & Layers

* **Master Knob:** Global output volume (mutes cleanly at `0%`).
* **Main Layer:** `K1` Tempo | `K2` Track DJ Filter | `K3` Track Volume (mutes at `0%`) | `K4` Metronome Volume.
* **FX Layer:** `K1` Spatial Delay (`0%–110%` Shimmer) | `K2` Reverb | `K3` Warm Harmonic Drive | `K4` Stereo Chorus.
* **Edit Note Layer:** `K1` Pitch Offset | `K2` Velocity (`ENV` = Decay) | `K3` Gate (`LFO` = Speed) | `K4` Micro-Timing.

---

## Installation & Flashing Guide

1. **Download `.fwsc`:** Download the latest `AUDILUNE-v1.0.fwsc` binary from [Releases](../../releases) (or the latest green GitHub Actions artifact).
2. **Preview in Emulator (Optional):** Load the `.fwsc` file into the [FM-1 Browser Emulator](https://github.com) to test the UI and controls first.
3. **Prepare Hardware:** Charge your M-VAVE FM-1 and connect it directly using a **data-rated USB-C cable** (avoid unpowered USB hubs). Open **Chrome** or **Edge** with only one MIDI tab open.
4. **Enter Update Mode (`UBOOT`):**
   * On the FM-1, hold **`Octave Down` + `Octave Up`** together for **5 seconds** while stopped until `UBOOT` appears on screen (or connect via the WebMIDI installer).
   * *Note:* If Windows or Android prompts you to format a newly mounted drive, click **Cancel**.
5. **Write Firmware:** Select `AUDILUNE-v1.0.fwsc` in the WebMIDI flasher and wait until writing finishes. **Do not unplug or power off the unit while writing is in progress.**

---

## Loading Personal Samples (`USR1`–`USR6`)

* **6 Dynamic Sample Slots:** AUDILUNE provides **6 floating user sample slots (`USR1`–`USR6`)** in dedicated flash (`0xA0000`–`0xDBFFF`) that can be loaded or stacked across any of the 6 tracks.
* **Web Sample Uploader:** Connect via USB-C in Chrome or Edge, open the FM-1 Web Sample Editor on [The Firmware Hub](https://thefirmwarehub.com), drop in WAV files (converted automatically to mono 16-bit PCM), and send them to `USR1`–`USR6`.
* **Best Practices for Audio Files:** Trim silence and keep individual samples concise (roughly **1–2 seconds mono**, or **~40 KB per slot**) for fast transfers and crisp playback.

---

## MIDI & USB-C Audio

* **Plug-and-Play MIDI Defaults:** Boots with **Tracks 1–6 mapped to MIDI Channels 1–6** (plus **GM Channel 10** drum support), automatic USB-C clock/transport pass-through, and live scale quantization.
* **Global MIDI Menu (`Tap GLOBAL`):** Turn the Select Encoder to choose Track 1–6 and use the knobs to configure Send/Receive/Internal routing. Fixed track CCs: `K1 = CC 10` (Pan), `K2 = CC 74` (Filter), `K3 = CC 7` (Level), `K4 = CC 93` (Chorus).
* **USB-C Class-Compliant Audio:** Streams stereo audio over USB-C (`44.1 kHz` / `48 kHz`) with no drivers required on Windows, macOS, Linux, or Android.

---

## Rescue & Returning to Stock Firmware

* **Built-In Boot-Loop Protection:** If startup is ever interrupted, AUDILUNE's boot guard automatically enters recovery/bootloader mode after repeated failed boots.
* **Manual Update Mode:** Hold **`Octave Down` + `Octave Up`** for 5 seconds while stopped, or hold **`Octave Down`** at power-on to enter USB Rescue.
* **Returning to Official Firmware:** Use M-VAVE's **M-UPGRADE** utility with the official `FM-1.fwsc` (V16) package.

> Custom firmware is installed at your own risk.

---

## Credits & Licence

* **AUDILUNE v1.0 Concept, UI/UX & Direction** — Created by **AUDILUNE** (`@AUDILUNE`), designed around a cosmological `#000000` aesthetic, one-handed tap-to-latch ergonomics, and an unrestricted 6-track workflow.
* **AI Pair-Programming & Code Assistance** — Developed with step-by-step firmware engineering, DSP calibration, and documentation assistance from **Google Gemini**.
* **Platform, Tooling & Hardware Ecosystem:**
  * **GitHub & GitHub Actions** — Source hosting, version control, and automated cloud firmware builds.
  * **M-VAVE (Sinco Intelligent Technology)** — Hardware platform for the **M-VAVE FM-1** pocket synthesizer and groovebox.
  * **Community Tools** — Browser emulation via [fm1-emu](https://github.com) (`@joakimeriksson`) and community firmware distribution via [The Firmware Hub](https://thefirmwarehub.com).
* **Upstream Open-Source Firmware Foundation:**
  * **[Felucca](https://github.com)** by **Leo Kuroshita (`@kurogedelic` / Hügelton Instruments)** — Core DSP synthesis engines, sequencer clock, USB-C audio/MIDI stack, and hardware abstraction layer.
  * **[Sloop](https://github.com)** by **`@isod89`** — Extended FM-1 firmware architecture, web editor protocol, and UI framework.
  * **Community Contributors** — Dual-core audio processing, boot-guard safety, and MIDI enhancements by `@andrerezemini` (`sloop-lab`), `@charlesvestal` (`X0X`), `@keremimo` (`Melodee`), `@renebohne`, `@juanjiant-bit` (`JIANT FM`), and `@ordenador`.
* **Licence:** Released under **GPL-3.0-only** (see [LICENSE](LICENSE)). M-VAVE and FM-1 are trademarks of their respective owners; AUDILUNE is an independent open-source project.

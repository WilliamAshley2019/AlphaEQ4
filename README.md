# AlphaEQ4

> A four-band programme equaliser inspired by vintage analogue hardware EQ units — built with JUCE.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![JUCE](https://img.shields.io/badge/Built%20with-JUCE%208.0.12-blue)](https://juce.com)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20-lightgrey)]()
[![Format](https://img.shields.io/badge/Format-VST3%20%7C%20Standalone-orange)]()

---

## About

AlphaEQ4 is the fourth iteration of the AlphaEQ series — a collection of audio plugins inspired by the character and workflow of classic analogue programme equalisers such as the API 550B. The goal is not a forensic emulation, but a plugin that *feels* like vintage hardware: stepped frequency and gain controls, proportional Q behaviour, warm saturation, and a no-nonsense four-band layout that encourages fast, musical decisions rather than endless tweaking.

This version adds parameter smoothing to eliminate zipper noise, 2× oversampling around the saturation stage, a real-time FFT spectrum analyser display, and a reworked metallic faceplate aesthetic that more closely echoes the gunmetal look of the original hardware units.

---

## Features

### EQ Section
- **Four independent bands** — Low, Low-Mid, High-Mid, and High
- **Stepped frequency selection** — seven positions per band, matching classic switched-inductor hardware behaviour
- **Stepped gain** — ±12 dB in 3 dB steps (nine positions) per band
- **Low Shelf** — switches the Low band from a peak filter to a low-shelf curve
- **High Shelf** — switches the High band from a peak filter to a high-shelf curve
- **Proportional Q mode** — when active, the filter Q narrows as gain increases, matching the behaviour of vintage inductor-based designs; when inactive, a fixed Q of 1.5 is used
- **Per-band Mute** — removes that band's EQ contribution (ramps smoothly via parameter smoothing; no click)
- **Per-band Bypass** — removes the band from the signal path entirely, leaving phase completely unaffected

### Saturation Section
- **Drive knob** — continuously variable `0–10`, placed in the Low-Mid column
- **2× oversampled** — the `tanh` waveshaper runs at double the host sample rate to suppress aliasing fold-back above ~5 kHz at high drive settings; the signal is decimated back to native rate before leaving the plugin

### Signal Processing
- **Stereo** — all four EQ bands use `ProcessorDuplicator` to correctly process both channels; the original mono-only filter bug from early iterations is fully resolved
- **100 ms parameter smoothing** — all gain and drive values ramp linearly on parameter changes, eliminating zipper noise on every knob move
- **Per-block coefficient updates** — filter coefficients are recalculated once per audio block during smoothing rather than per-sample, keeping CPU overhead minimal
- **State persistence** — all parameters save and restore correctly via the host's preset/session system

### Spectrum Analyser
- **2048-point windowed FFT** — Hann window applied before transform to reduce spectral leakage
- **Logarithmic frequency axis** — 20 Hz to 20 kHz, consistent with how we hear
- **Asymmetric ballistics** — fast attack, slow release, matching the feel of a hardware spectrum bridge
- **Grid overlays** — dBFS reference lines at 0, −12, −24, and −48 dBFS; frequency markers at 100 Hz, 1 kHz, and 10 kHz
- **Lock-free FIFO** — the audio thread pushes samples without blocking; the GUI thread pulls at 30 Hz with no shared mutex

---

## Controls Reference

| Control | Location | Function |
|---|---|---|
| **FREQ knob** | Each band | Selects the centre/corner frequency from seven stepped positions |
| **GAIN knob** | Each band | Boosts or cuts ±12 dB in 3 dB steps |
| **MUTE LED** | Each band | Flattens that band's response (smooth ramp, amber off / red on) |
| **BYPASS LED** | Each band | Removes band from signal path; no phase contribution |
| **SHELF LED** | Low & High bands | Toggles peak ↔ shelf response; illuminates amber when active |
| **DRIVE knob** | Low-Mid column | Sets saturation amount (0 = clean, 10 = heavy drive) |
| **PROP Q LED** | High-Mid column | Enables proportional Q mode; illuminates amber when active |

### Frequency Positions

**Low / Low-Mid bands:** 40 Hz · 75 Hz · 150 Hz · 300 Hz · 600 Hz · 1.2 kHz · 2.4 kHz

**High-Mid / High bands:** 800 Hz · 1.5 kHz · 3 kHz · 5 kHz · 7 kHz · 10 kHz · 12.5 kHz

---

## Installation

### Requirements
- Windows 10/11 (x64) or macOS 10.13+
- A VST3-compatible DAW (Ableton Live, Reaper, Cubase, Studio One, etc.)  Tested in FL Studio 25. 
- No additional dependencies or runtime libraries required

### From a Release Build
1. Download the release `.zip` from the [Releases](https://github.com/WilliamAshley2019/AlphaEQ4/AlphaEQ4.vst3.zip) file. 
2. Unzip `AlphaEQ4.vst3` into your VST3 folder:
   - **Windows:** `C:\Program Files\Common Files\VST3\`
   
3. Rescan plugins in your DAW

### Building from Source

**Prerequisites:** Built in Windows
- [JUCE 8.0.12](https://juce.com/get-juce/) or later
- Visual Studio 2022 or 2026 (Windows) 
- Projucer (included with JUCE)

**Steps:**
```
1. Clone this repository
   git clone https://github.com/WilliamAshley2019/AlphaEQ4.git

2. Open AlphaEQ4.jucer in Projucer (create one not included, basic plugin modules + dsp module. 

3. Set your JUCE path in Projucer → File → Global Paths

4. Click "Save Project and Open in IDE"

5. Build the AlphaEQ4_VST3 target in Release configuration
```

The built `.vst3` bundle will appear in `Builds/VisualStudio2026/x64/Release/VST3/`. after setting project build  configuration settings to release, obviously.

---

## Project Structure

```
AlphaEQ4/
├── Source/
│   ├── PluginProcessor.h      — DSP, parameter definitions, spectrum FIFO
│   ├── PluginProcessor.cpp    — Audio processing, filter updates, oversampling
│   ├── PluginEditor.h         — Editor class declaration
│   └── PluginEditor.cpp       — UI rendering, spectrum analyser component, LAF
├── AlphaEQ4.jucer             — Projucer project file (not included just make it)
└── README.md
```

---

## The AlphaEQ Series

AlphaEQ4 is part of an ongoing series of EQ plugin experiments, each iteration adding signal processing improvements and UI refinements.
 
Older versions are in their own repositories at [github.com/WilliamAshley2019](https://github.com/WilliamAshley2019).

---

## Roadmap

Ideas being considered for future iterations:

- [ ] Output level trim (±4 dB pad, matching the original hardware's output section)
- [ ] Asymmetric transformer saturation model (even-order harmonics in addition to the current odd-order `tanh`)
- [ ] EQ curve overlay drawn on the spectrum display
- [ ] Different EQ model not just proportional to match with different design models of older EQs from the 1970s.
---

## Contributing

Issues and pull requests are welcome. If you find a bug or have a suggestion, please open an issue on the [GitHub repository](https://github.com/WilliamAshley2019/AlphaEQ4) with as much detail as possible — DAW name and version, OS, sample rate, and a description of what you expected vs. what happened.
Issues may eventually be responded to. I don't currently interact with stuff on github but someday I might.
---

## Support the Project

If you find AlphaEQ4 useful and want to help keep development going, a coffee goes a long way:

**[☕ Buy Me a Coffee — William Ashley](https://buymeacoffee.com/williamashley)**

You can also find more of my work and occasional articles through:

**[williamashley.wixsite.com/williamashley](https://12264447666william.wixsite.com/williamashley)**
a mostly delinquint website that will likely someday be ported to 1$ 1and1 website at some point in the future when I feel like hosting a website again.
---

## Legal

### VST® Trademark Notice
VST is a registered trademark of **Steinberg Media Technologies GmbH**. The VST3 plugin format used by this project is implemented via the JUCE framework under its respective licence terms. This project is not affiliated with, endorsed by, or sponsored by Steinberg Media Technologies GmbH.
They make me say this its not my personal belief but someday they may care if it is here or not so its there, happy? Now to get a paper tissue. 
### JUCE
This project is built using the [JUCE framework](https://juce.com), © 2026 Raw Material Software Limited. JUCE is used under the [JUCE Personal / Educational licence](https://juce.com/juce-7-licence/) for non-commercial distribution. If you intend to use this code commercially, ensure your JUCE licence covers commercial deployment.
Hopefully I got that right. 
### Inspiration
AlphaEQ4 is inspired by the character and workflow of the API 550B hardware equaliser but in no way is a clone or copy or trademark infringement it is simply AlphaEQ4. It is not an official product of, affiliated with, or endorsed by **Automated Processes, Inc. (API)**. No proprietary circuit designs, schematics, or intellectual property belonging to API have been used. The plugin is an original software implementation inspired by the general concept of stepped-inductor programme equalisers.

### Licence
This project is released under the [MIT Licence](https://opensource.org/licenses/MIT). You are free to use, modify, and distribute the source code with attribution.
Its actually GPLv3 same dif. 
---

*AlphaEQ4 — William Ashley · 2026

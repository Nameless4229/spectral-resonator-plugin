# Spectral Foundry 2.0 — rack-based rebuild

This Windows x64 VST3 audio effect uses the **three actual embedded wavetables** from your `NAMELESS - Spectral Resonator.adg`. Version 1 used invented spectral shapes and guessed mappings; version 2 replaces that core.

## Install / upgrade in FL Studio

1. Close FL Studio and any other DAW using the plugin.
2. Run **SpectralFoundry-Setup-2.0.exe**, accept the Windows administrator prompt, and keep `C:\Program Files\Common Files\VST3`.
3. The installer updates the existing Spectral Foundry bundle and backs it up under `%LOCALAPPDATA%\Spectral Foundry\Backups`.
4. In FL Studio, open **Options > Manage plugins**. Enable **Verify plugins**, **Rescan previously verified plugins**, and **Rescan plugins with errors**, then click **Find installed plugins**.
5. Load **Spectral Foundry** in a **Mixer effect slot**, then select **01 ADG - saved rack** or **02 Video - Wump 70**.

The editor must say **VERSION 2 / ORIGINAL RACK TABLES + MACRO ROUTING**. If it does not, FL is loading an older installed copy. The plugin identifier is retained so projects can find it, but version 2 deliberately changes its sound. Existing projects restore their old knob values; select a new preset to hear the new defaults. Keep the previous installer/backup for projects that need the old sound.

This is an audio effect and needs an incoming sound. Use a harmonically rich bass/growl/saw/drum loop. A pure sine has little upper-frequency content to filter. The plugin is Windows VST3, not an AAX plugin or macOS binary. FL's VST3 locations are fixed; a custom VST2 search path is not a substitute.

## What changed

- Three serial filters now derive their shapes from the three FLAC tables embedded in the ADG: 256 frames of 2048 samples each.
- Main cutoff uses the rack's logarithmic 20–20480 Hz range; resonance uses 0.25–4. Cutoff is the frequency of wavetable harmonic 24, following the Filter Table manual.
- **Wump** scans the second table at fixed 440 Hz cutoff and also controls Shaper's manual ramp.
- **Chord Depth** scans the third table, rather than blending in an invented chord comb. **Chord Pitch** moves its cutoff logarithmically from 600 to 1200 Hz.
- **Spectral Depth** changes modulation of the first table position; it is not a dry/wet control. The video shows positive modulation at 50%; the saved Shaper state has a straight ramp, Manual trigger mode, target Spectral Pos, and polarity value 1. Positive modulation is implemented as `position/127 + 0.5 * (wump/127) * (depth/100)`, clamped to 0–1.
- The filtered branch has a 48 dB/octave high-pass crossover and +3 dB high shelf at 4.5 kHz; the parallel clean branch has a 48 dB/octave low-pass crossover. Crossover range is 10–300 Hz.
- Three-band dynamics use 120/2500 Hz splits, +9.14 dB mid/high input, +6 dB output, -20 dB upper threshold, and the saved attack/release times: 50/300, 10/200, 5/100 ms. The limiting/knee implementation is independent.
- Full-wet sine folding follows the summed rack. The old upper-band-only fold blend has been removed.
- The filter engine is minimum-phase FIR convolution rather than linear-phase STFT masking. Fixed delay is now **256 samples** (5.33 ms at 48 kHz), reported to the host and applied to the dry/bypass path.

## Using the controls

Start with **02 Video - Wump 70** and sweep **Wump** between 40 and 120. Then adjust **Position**, **Cutoff**, and **Resonance**. For pronounced pitched notches, sweep **Chord Depth** and **Chord Pitch**. These macros are intentionally interdependent, as in the rack. At maximum depth/position the first table position can hit its upper limit; moving Position downward or reducing Depth restores room for the Shaper offset.

**Movement** is an optional extra LFO and defaults to zero; the reference uses Manual Shaper mode. **Rate** only affects this extra movement. **Space** defaults to the rack's 21% wet setting. **Compression** at 100% applies the recovered multiband settings; lowering it reduces compression and its associated makeup/input boosts. **Peak Taming** controls an independent adaptive de-harsh stage centered at the saved soothe2 high-frequency focus bands. **Fold Drive** is extra drive before the final sine fold; the rack setting is 0 dB. **Dry / Wet** is the true overall effect blend.

Preset 01 uses the ADG values: Depth 100%, Position 67, Cutoff 65, Resonance 96, Wump 127, Chord Depth 79, Chord Pitch 91, crossover 180.36 Hz. Presets 02–04 use visible settings from the video. Preset 05 adds movement/drive, and 06 is a latency-aligned dry comparison.

## Accuracy and remaining differences

The source waveforms, parameter ranges, ordering, and listed settings come from the supplied rack. The conversion of waveform spectra to filter responses is an **independent implementation**: harmonic magnitudes are referenced to a unit saw spectrum, interpolated between frames/harmonics, shaped by resonance, and converted to a 4096-tap minimum-phase FIR. Deep notches are preserved; combined response is bounded to -100/+30 dB. This is not Kilohearts' proprietary algorithm, so its exact normalization, interpolation, and transient response may differ.

Hybrid Reverb remains an algorithmic approximation. The ADG references a separate Ableton Studio Backwards stereo impulse response, which is not embedded in the rack; that convolution sound is not included. The saved hall decay is 514 ms, but the current substitute uses a different reverb topology and is not calibrated to identical decay. Soothe2 is also approximated, not embedded or cloned; its saved depth is 7.92, mix 76.55%, sharpness 8.4, selectivity 7.48. The multiband detector, knee, crossover phase, and Sinoid Fold transfer curve may differ from Ableton. The fold stage is not oversampled. A final 0.98 full-scale ceiling bounds output.

No exact/null-test match to an isolated render of the original rack has been established. This build is materially closer to the supplied rack than version 1 because it uses its actual table content and mappings. It does not require Ableton, Kilohearts, or soothe2 to run.

## Demonstration and validation

`Demo-Dry.wav` and `Demo-Rack-Wump-Sweep.wav` use the same synthetic test bass; the second runs through version 2 while Wump sweeps from 35 to 125. These are a demonstration of this build, not audio extracted from the friend's video.

See `Validation.md`, `DSP-test-report.txt`, and `Pluginval-report.txt` for actual results and limits. Each macro is tested with output-level normalization and a separate magnitude-spectrum comparison, so added volume or phase change alone cannot satisfy the test.

## Source and assets

The full project, original embedded FLAC tables, derived spectrum assets, and pinned JUCE dependency are supplied in `SpectralFoundry-2.0-Source.zip`. CMake 3.22+ and Visual Studio C++ build tools are needed:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target SpectralFoundry_VST3 FoundryTests --parallel 1
```

Run `FoundryTests.exe` for checks, `--snapshot` to render the interface, or `--demo` to produce the demonstration WAV files. JUCE is pinned to `be29c81492b6151c8ea8d14c840e1311963b3a83` (9.0.3).

Code is supplied under AGPL-3.0; see `LICENSE.txt`. User-supplied wavetable assets retain their original ownership and are not relicensed by this project. Their extracted SHA-256 values and saved settings are in `Assets/provenance.json`. JUCE and its dependencies retain their original license notices. A commercial JUCE license may be needed for closed-source distribution. This project is independent of Ableton, Kilohearts, oeksound, and Steinberg.

## References

- [Kilohearts Filter Table](https://kilohearts.com/docs/filter_table)
- [Ableton Shaper](https://www.ableton.com/en/manual/max-for-live-devices/#shaper)
- [Ableton audio effects](https://www.ableton.com/en/manual/live-audio-effect-reference/)
- [FL Studio plugin installation](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm)

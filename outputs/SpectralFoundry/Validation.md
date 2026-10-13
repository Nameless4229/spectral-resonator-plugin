# Version 2 validation — 2026-10-12

Final Windows x64 Release binary SHA-256: `41aede186540918eac269f68781d9e7f2258cdf45ced18712387be071750c8ef`.

## Automated DSP checks: PASS

- Dry reconstruction at 44.1, 48, and 96 kHz: maximum error 2.98023e-08.
- Host latency and bypass alignment: 256 samples, unity dry/bypass gain.
- State save/restore, all six presets, finite bounded output, stereo/channel isolation.
- Mono, extreme parameter automation, irregular buffers, and zero-length buffers.
- Minimum-phase convolution engine identity impulse.
- All eight primary macros change normalized waveforms and spectral magnitudes. Spectral-shape differences after removing mean level range from 2.99 to 20.30 dB RMS in this test. These are endpoint comparisons using a fixed broadband test signal, not loudness ratings or perceptual similarity scores.
- 19.2 seconds of stereo test audio processed in approximately 0.47 seconds on this machine. This is an offline test, not a guarantee for every DAW or machine.

See DSP-test-report.txt for full output.

## VST3 host validation: PASS

Tracktion pluginval 1.0.4, strictness level 5, in-process, on the final release bundle. Includes editor opening, sample-rate/buffer variations, parameter automation, state restoration, and bus checks. Full output: Pluginval-report.txt.

The optional separate Steinberg validator was not configured. An actual FL Studio session with this updated binary has not been verified here; use the system VST3 folder and rescan, then check that the editor says VERSION 2.

## Reference limits

Actual embedded table content and recovered macro mappings are used. Proprietary Filter Table response conversion, Hybrid Reverb, soothe2, multiband dynamics, and Sinoid Fold are independently approximated. No isolated original-rack render/null comparison has been performed. See README.md for the exact settings and remaining differences.

The two demo WAVs use the same synthetic bass, dry versus a processed Wump sweep. They are not audio from the reference video.

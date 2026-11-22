#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "RackEngine.h"

// Chord Pitch (0..127) maps to a table cutoff of 600..1200 Hz, and the table's root is cutoff / 24 = 25 * 2^(v/127) Hz,
// so one octave of root notes (G#0 .. G1, MIDI 20..31) lies inside the knob's range, 127 / 12 = 10.58 units apart.
inline float chordPitchForMidi(int midi) { return 127.f * std::log2(440.f * std::pow(2.f, float(midi - 69) / 12.f) / 25.f); }
inline float snapChordPitch(float v) {
    const float hz = 25.f * std::pow(2.f, v / 127.f);
    const int midi = int(std::lround(69.f + 12.f * std::log2(hz / 440.f)));
    return chordPitchForMidi(std::max(20, std::min(31, midi)));
}

class SpectralFoundry final : public juce::AudioProcessor
{
public:
    SpectralFoundry();
    const juce::String getName() const override { return "Spectral Resonator"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 5.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorValueTreeState state;
    std::array<std::atomic<float>,128> display {};
    std::atomic<float> meter {0}, motion {0}, inMeter {0};
    std::array<std::atomic<float>,128> response {};      // combined filter response, 20 Hz..20 kHz, 0..1 over -60..+30 dB
    std::array<std::atomic<float>,3> tablePos {};        // table frame positions, 0..1
    static constexpr int fftSize=256; // Fixed convolution block latency; retained name for test API.
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    enum { depth,position,cutoff,resonance,wump,chord,pitch,sub,rate,movement,space,drive,tame,punch,mix,output,input,count };
    std::array<std::atomic<float>*,count> raw {};
    std::atomic<float>* snapRaw=nullptr; // Snap Chord Pitch to notes switch
    std::atomic<float>* majorRaw=nullptr; // Chord quality: 0 = minor (the table as saved), 1 = major
    float target(int i) const {
        float v=raw[size_t(i)]->load(std::memory_order_relaxed);
        if(i==pitch && snapRaw!=nullptr && snapRaw->load(std::memory_order_relaxed)>.5f) v=snapChordPitch(v);
        return v;
    }
    std::array<juce::SmoothedValue<float>,count> smoothed;
    std::array<float,count> values {};
    RackEngine engine;
    std::array<std::array<float,fftSize>,2> dryDelay{};
    std::array<RackBiquad,4> lowCut,highCut;
    RackBiquad air,verbHigh,verbLow;
    // OTT-style stage (the former Peak Taming): 3-band upward + downward compression, depth = amount.
    juce::dsp::LinkwitzRileyFilter<float> ottLow,ottHigh,ottPhase;
    std::array<float,3> ottEnv{},ottGainDb{},ottAttack{},ottRelease{};
    float ottDetect=0.f;
    void ott(float* io,int channels,float depth);
    std::array<float,3> rms{},attack{},release{};
    juce::dsp::LinkwitzRileyFilter<float> splitLow,splitHigh,lowPhase;
    juce::dsp::FFT analyzer{11};
    std::array<float,4096> analyzerData{};
    std::array<float,2048> analyzerInput{};
    int analyzerCursor=0;
    std::array<float,8> lastDesign{};
    juce::Reverb reverb;
    double sr=48000, phase=0;
    int cursor=0, controlCounter=0;
    bool processingBypassed=false;
    void updateFilters(bool force=false);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFoundry)
};

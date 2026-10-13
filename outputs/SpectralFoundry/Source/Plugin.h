#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "RackEngine.h"

class SpectralFoundry final : public juce::AudioProcessor
{
public:
    SpectralFoundry();
    const juce::String getName() const override { return "Spectral Foundry"; }
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
    void loadPreset(int);
    juce::AudioProcessorValueTreeState state;
    std::array<std::atomic<float>,128> display {};
    std::atomic<float> meter {0}, motion {0};
    static constexpr int fftSize=256; // Fixed convolution block latency; retained name for test API.
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    enum { depth,position,cutoff,resonance,wump,chord,pitch,sub,rate,movement,space,drive,tame,punch,mix,output,count };
    std::array<std::atomic<float>*,count> raw {};
    std::array<juce::SmoothedValue<float>,count> smoothed;
    std::array<float,count> values {};
    RackEngine engine;
    std::array<std::array<float,fftSize>,2> dryDelay{};
    std::array<RackBiquad,4> lowCut,highCut;
    RackBiquad air,verbHigh,verbLow;
    std::array<RackBiquad,3> harsh;
    std::array<float,3> rms{},attack{},release{};
    juce::dsp::LinkwitzRileyFilter<float> splitLow,splitHigh,lowPhase;
    juce::dsp::FFT analyzer{10};
    std::array<float,2048> analyzerData{};
    std::array<float,1024> analyzerInput{};
    int analyzerCursor=0;
    std::array<float,8> lastDesign{};
    juce::Reverb reverb;
    double sr=48000, phase=0;
    int cursor=0, controlCounter=0;
    bool processingBypassed=false;
    void updateFilters(bool force=false);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFoundry)
};

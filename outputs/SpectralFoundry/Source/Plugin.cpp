#include "Plugin.h"

namespace {
constexpr const char* ids[] = {"depth","position","cutoff","resonance","wump","chord","pitch","sub","rate","movement","space","drive","tame","punch","mix","output"};
constexpr float pi=juce::MathConstants<float>::pi;
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectralFoundry::parameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    auto add=[&](const char* id,const char* name,float a,float b,float initial,float skew=1.0f,const char* unit="") {
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,
            juce::NormalisableRange<float>(a,b,0.001f,skew),initial,
            juce::AudioParameterFloatAttributes().withLabel(unit)));
    };
    add("depth","Spectral Depth",0,100,100,1,"%");
    add("position","Spectral Position",0,127,67);
    add("cutoff","Spectral Cutoff",0,127,65);
    add("resonance","Spectral Resonance",0,127,96);
    add("wump","Wump",0,127,127);
    add("chord","Chord Depth",0,127,79);
    add("pitch","Chord Pitch",0,127,91);
    add("sub","Sub Crossover",10,300,180.35765f,0.6f,"Hz");
    add("rate","Motion Rate",0.05f,8,0.5f,0.4f,"Hz");
    add("movement","Motion Amount",0,100,0,1,"%");
    add("space","Space",0,100,21,1,"%");
    add("drive","Fold Drive",0,24,0,1,"dB");
    add("tame","Peak Taming",0,100,76.55f,1,"%");
    add("punch","Compression",0,100,100,1,"%");
    add("mix","Dry / Wet",0,100,100,1,"%");
    add("output","Output",-24,6,0,1,"dB");
    return p;
}
SpectralFoundry::SpectralFoundry()
    : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      state(*this,nullptr,"SpectralFoundryState",parameters())
{
    for(int i=0;i<count;++i) raw[i]=state.getRawParameterValue(ids[i]);

    setLatencySamples(fftSize);
}
bool SpectralFoundry::isBusesLayoutSupported(const BusesLayout& b) const
{
    const auto out=b.getMainOutputChannelSet();
    return (out==juce::AudioChannelSet::mono() || out==juce::AudioChannelSet::stereo()) && out==b.getMainInputChannelSet();
}
void SpectralFoundry::prepareToPlay(double sampleRate,int maxBlock)
{
    sr=sampleRate;
    for(int i=0;i<count;++i) { smoothed[i].reset(sr,0.02); smoothed[i].setCurrentAndTargetValue(raw[i]->load()); values[i]=raw[i]->load(); }
    engine.prepare(sr); reverb.setSampleRate(sr);
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)std::max(1,maxBlock),2};
    splitLow.prepare(spec); splitHigh.prepare(spec); lowPhase.prepare(spec);
    splitLow.setCutoffFrequency(120); splitHigh.setCutoffFrequency(2500);
    lowPhase.setCutoffFrequency(2500); lowPhase.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
    const float attackMs[]={50,10,5}, releaseMs[]={300,200,100};
    for(int i=0;i<3;++i) { attack[i]=float(std::exp(-1000/(sr*attackMs[i]))); release[i]=float(std::exp(-1000/(sr*releaseMs[i]))); }
    air.set(juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf(sr,4500,0.7071f,juce::Decibels::decibelsToGain(3.f)));
    verbHigh.lowHigh(sr,311,.7071f,true); verbLow.lowHigh(sr,3360,.7071f,false);
    reset(); setLatencySamples(fftSize);
}
void SpectralFoundry::reset()
{
    engine.reset(); reverb.reset();
    for(auto& d:dryDelay)d.fill(0);
    for(auto& f:lowCut)f.reset(); for(auto& f:highCut)f.reset();
    for(auto& f:harsh)f.reset();
    air.reset();verbHigh.reset();verbLow.reset();
    splitLow.reset();splitHigh.reset();lowPhase.reset();
    rms.fill(0);analyzerInput.fill(0);analyzerData.fill(0);
    cursor=controlCounter=analyzerCursor=0;phase=0;
    for(auto& x:display)x.store(0);meter.store(0);
    updateFilters(true);
}
void SpectralFoundry::updateFilters(bool force)
{
    // ADG: Wump is connected to both Shaper Manual and table 2 frame.
    // Shaper contains a straight 0..1 ramp in Manual mode, 50% positive modulation.
    float manual=values[wump]/127.f;
    float pos=juce::jlimit(0.f,1.f,values[position]/127.f+manual*values[depth]*.005f
        +float(std::sin(phase))*values[movement]*.005f);
    motion.store(pos,std::memory_order_relaxed);
    std::array<float,8> next{pos*255,manual*255,values[chord]*255/127,
        20*std::pow(1024.f,values[cutoff]/127.f),440.f,600*std::pow(2.f,values[pitch]/127.f),
        .25f*std::pow(16.f,values[resonance]/127.f),values[sub]};
    bool changed=force;
    for(int i=0;i<8;++i)changed|=std::abs(next[i]-lastDesign[i])>(i<3?.05f:.01f);
    if(changed) {
        engine.design({next[0],next[1],next[2]},{next[3],next[4],next[5]},next[6]);
        const float q[]={.50979558f,.60134489f,.89997622f,2.56291545f};
        for(int i=0;i<4;++i) { lowCut[i].lowHigh(sr,values[sub],q[i],false);highCut[i].lowHigh(sr,values[sub],q[i],true); }
        lastDesign=next;
    }
    // Original adaptive de-harsh approximation, centered on the strongest soothe2 bands.
    const float freq[]={3391.78f,6535.06f,10052.91f};
    float hot=juce::jlimit(0.f,1.f,(std::sqrt(rms[2])-.03f)*8);
    for(int i=0;i<3;++i)harsh[i].peak(sr,freq[i],1.4f,-hot*values[tame]*.065f);
}
void SpectralFoundry::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard; midi.clear();
    const int channels=std::min(2,buffer.getNumChannels());
    for(int i=0;i<count;++i)smoothed[i].setTargetValue(raw[i]->load(std::memory_order_relaxed));
    juce::Reverb::Parameters rp;rp.roomSize=.23f;rp.damping=.5f;rp.width=1;rp.wetLevel=1;rp.dryLevel=0;rp.freezeMode=0;
    reverb.setParameters(rp);
    float peak=0;
    for(int s=0;s<buffer.getNumSamples();++s) {
        for(int i=0;i<count;++i)values[i]=smoothed[i].getNextValue();
        float dry[2]{},in[2]{},filtered[2]{},top[2]{},bottom[2]{},verb[2]{},bands[2][3]{};
        for(int ch=0;ch<channels;++ch) {
            float x=buffer.getSample(ch,s); if(!std::isfinite(x))x=0;
            in[ch]=juce::jlimit(-16.f,16.f,x);
            dry[ch]=dryDelay[ch][cursor];dryDelay[ch][cursor]=in[ch];
        }
        if(channels==1)in[1]=in[0];
        engine.tick(in[0],in[1],filtered[0],filtered[1]);
        for(int ch=0;ch<channels;++ch) {
            bottom[ch]=dry[ch];top[ch]=filtered[ch];
            for(int i=0;i<4;++i) { bottom[ch]=lowCut[i].tick(ch,bottom[ch]);top[ch]=highCut[i].tick(ch,top[ch]); }
            top[ch]=air.tick(ch,top[ch]);verb[ch]=top[ch];
        }
        if(channels==2)reverb.processStereo(&verb[0],&verb[1],1);else if(channels==1)reverb.processMono(&verb[0],1);
        const float amount=values[punch]*.01f;
        const float upperGain=juce::Decibels::decibelsToGain(9.14f*amount);
        for(int ch=0;ch<channels;++ch) {
            verb[ch]=verbLow.tick(ch,verbHigh.tick(ch,verb[ch]));
            float y=bottom[ch]+top[ch]*(1-values[space]*.01f)+verb[ch]*values[space]*.01f;
            float upper;splitLow.processSample(ch,y,bands[ch][0],upper);
            bands[ch][0]=lowPhase.processSample(ch,bands[ch][0]);
            splitHigh.processSample(ch,upper,bands[ch][1],bands[ch][2]);
            bands[ch][1]*=upperGain;bands[ch][2]*=upperGain;
        }
        float gain[3]{};
        for(int b=0;b<3;++b) {
            float power=0;for(int ch=0;ch<channels;++ch)power+=bands[ch][b]*bands[ch][b];
            power/=std::max(channels,1);
            float coeff=power>rms[b]?attack[b]:release[b];rms[b]=coeff*rms[b]+(1-coeff)*power;
            float level=juce::Decibels::gainToDecibels(std::sqrt(std::max(0.f,rms[b]))+1.e-12f);
            float over=level+20,reduce=over<-3?0:over>3?over:(over+3)*(over+3)/12;
            gain[b]=juce::Decibels::decibelsToGain(-reduce*amount);
        }
        for(int ch=0;ch<channels;++ch) {
            float y=0;for(int b=0;b<3;++b)y+=bands[ch][b]*gain[b];
            y*=juce::Decibels::decibelsToGain(6.f*amount);
            for(auto& f:harsh)y=f.tick(ch,y);
            // Full-wet sinoid folding follows the summed rack, as in the source chain.
            y=std::sin(y*juce::Decibels::decibelsToGain(values[drive]));
            float wet=values[mix]*.01f;
            y=((1-wet)*dry[ch]+wet*y)*juce::Decibels::decibelsToGain(values[output]);
            if(!std::isfinite(y))y=0;
            y=juce::jlimit(-.98f,.98f,y);
            if(processingBypassed)y=dry[ch];
            buffer.setSample(ch,s,y);peak=std::max(peak,std::abs(y));
        }
        if(channels>0)analyzerInput[analyzerCursor++]=buffer.getSample(0,s);
        if(analyzerCursor==1024) {
            for(int j=0;j<1024;++j)analyzerData[j]=analyzerInput[j]*(.5f-.5f*std::cos(2*pi*j/1024));
            std::fill(analyzerData.begin()+1024,analyzerData.end(),0.f);analyzer.performRealOnlyForwardTransform(analyzerData.data(),true);
            for(int b=0;b<128;++b){int k=juce::jlimit(1,512,int(std::pow(512.f,b/127.f)));float db=juce::Decibels::gainToDecibels(std::hypot(analyzerData[2*k],analyzerData[2*k+1])/512+1.e-9f);display[b].store(juce::jlimit(0.f,1.f,(db+75)/75));}
            analyzerCursor=0;
        }
        cursor=(cursor+1)%fftSize;
        phase+=2*juce::MathConstants<double>::pi*values[rate]/sr;
        if(phase>2*juce::MathConstants<double>::pi)phase-=2*juce::MathConstants<double>::pi;
        if(++controlCounter==512){controlCounter=0;updateFilters();}
    }
    meter.store(peak);
}
void SpectralFoundry::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi)
{
    processingBypassed=true;processBlock(buffer,midi);processingBypassed=false;
}
void SpectralFoundry::getStateInformation(juce::MemoryBlock& dest)
{ auto xml=state.copyState().createXml(); copyXmlToBinary(*xml,dest); }
void SpectralFoundry::setStateInformation(const void* data,int size)
{ if(auto xml=getXmlFromBinary(data,size)) if(xml->hasTagName(state.state.getType())) state.replaceState(juce::ValueTree::fromXml(*xml)); }
void SpectralFoundry::loadPreset(int index)
{
    static constexpr float presets[][count] = {
        {100,67,65,96,127,79,91,180.35765f,.5f,0,21,0,76.55f,100,100,0},
        {100,67,65,96,70,79,91,100,.5f,0,21,0,76.55f,100,100,0},
        {35.4f,29,75,54,74,65,91,100,.5f,0,21,0,76.55f,100,100,0},
        {37.8f,83,75,54,90,43,46,100,.5f,0,21,0,76.55f,100,100,0},
        {65,45,70,104,92,115,103,100,.35f,32,28,6,30,100,100,-3},
        {100,67,65,96,127,79,91,180.35765f,.5f,0,21,0,76.55f,100,0,0}
    };
    index=juce::jlimit(0,5,index);
    for(int i=0;i<count;++i) if(auto* p=state.getParameter(ids[i])) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(presets[index][i])); p->endChangeGesture();
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpectralFoundry(); }

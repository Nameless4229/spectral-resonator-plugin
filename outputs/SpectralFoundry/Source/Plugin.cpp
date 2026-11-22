#include "Plugin.h"

namespace {
constexpr const char* ids[] = {"depth","position","cutoff","resonance","wump","chord","pitch","sub","rate","movement","space","drive","tame","punch","mix","output","input"};
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
    // The starting values are the former "Blossom" settings.
    add("depth","Spectral Depth",0,100,56.376f,1,"%");
    add("position","Spectral Position",0,127,43.47f);
    add("cutoff","Spectral Cutoff",0,127,73.302f);
    add("resonance","Spectral Resonance",0,127,63.926f);
    add("wump","Wump",0,127,127);
    add("chord","Chord Depth",0,127,92.964f);
    add("pitch","Chord Pitch",0,127,91.546f);
    add("sub","Sub Crossover",10,300,10,0.6f,"Hz");
    add("rate","Motion Rate",0.05f,8,0.395f,0.4f,"Hz");
    add("movement","Motion Amount",0,100,34.4f,1,"%");
    add("space","Space",0,100,40,1,"%");
    add("drive","Fold Drive",0,24,0,1,"dB");
    add("tame","OTT",0,100,100,1,"%");
    add("punch","Compression",0,100,0,1,"%");
    add("mix","Dry / Wet",0,100,100,1,"%");
    add("output","Output",-24,6,0,1,"dB");
    add("input","Input",-24,12,0,1,"dB");
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"snap",1},"Snap Chord Pitch to Notes",true));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"major",1},"Chord Quality Major",false));
    return p;
}
SpectralFoundry::SpectralFoundry()
    : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      state(*this,nullptr,"SpectralFoundryState",parameters())
{
    for(int i=0;i<count;++i) raw[i]=state.getRawParameterValue(ids[i]);
    snapRaw=state.getRawParameterValue("snap");
    majorRaw=state.getRawParameterValue("major");

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
    for(int i=0;i<count;++i) { smoothed[i].reset(sr,0.02); smoothed[i].setCurrentAndTargetValue(target(i)); values[i]=target(i); }
    engine.prepare(sr); reverb.setSampleRate(sr);
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)std::max(1,maxBlock),2};
    splitLow.prepare(spec); splitHigh.prepare(spec); lowPhase.prepare(spec);
    splitLow.setCutoffFrequency(120); splitHigh.setCutoffFrequency(2500);
    lowPhase.setCutoffFrequency(2500); lowPhase.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
    const float attackMs[]={50,10,5}, releaseMs[]={300,200,100};
    for(int i=0;i<3;++i) { attack[i]=float(std::exp(-1000/(sr*attackMs[i]))); release[i]=float(std::exp(-1000/(sr*releaseMs[i]))); }
    ottLow.prepare(spec); ottHigh.prepare(spec); ottPhase.prepare(spec);
    ottLow.setCutoffFrequency(88.3f); ottHigh.setCutoffFrequency(2500.f);
    ottPhase.setCutoffFrequency(2500.f); ottPhase.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
    const float ottAttackMs[]={13.5f,5.3f,1.6f}, ottReleaseMs[]={132.f,282.f,140.f};
    for(int i=0;i<3;++i) { ottAttack[i]=float(std::exp(-1000/(sr*ottAttackMs[i]))); ottRelease[i]=float(std::exp(-1000/(sr*ottReleaseMs[i]))); }
    ottDetect=float(std::exp(-1000/(sr*4.0))); // 4 ms level detector
    air.set(juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf(sr,4500,0.7071f,juce::Decibels::decibelsToGain(3.f)));
    verbHigh.lowHigh(sr,311,.7071f,true); verbLow.lowHigh(sr,3360,.7071f,false);
    reset(); setLatencySamples(fftSize);
}
void SpectralFoundry::reset()
{
    engine.reset(); reverb.reset();
    for(auto& d:dryDelay)d.fill(0);
    for(auto& f:lowCut)f.reset(); for(auto& f:highCut)f.reset();
    ottLow.reset();ottHigh.reset();ottPhase.reset();ottEnv.fill(0);ottGainDb.fill(0);
    air.reset();verbHigh.reset();verbLow.reset();
    splitLow.reset();splitHigh.reset();lowPhase.reset();
    rms.fill(0);analyzerInput.fill(0);analyzerData.fill(0);
    cursor=controlCounter=analyzerCursor=0;phase=0;
    for(auto& x:display)x.store(0);for(auto& x:response)x.store(0);for(auto& x:tablePos)x.store(0);meter.store(0);inMeter.store(0);
    updateFilters(true);
}
void SpectralFoundry::updateFilters(bool force)
{
    // ADG: Wump is connected to both Shaper Manual and table 2 frame.
    // Shaper contains a straight 0..1 ramp in Manual mode, 50% positive modulation.
    float manual=values[wump]/127.f;
    // Shaper offset added to the table position. In the original rack the sum was clamped at 1, which left the top
    // of the Position knob dead and stopped the bottom at the offset. Here the knob always spans the whole 0..1 table:
    // knob 0 is table position 0 and knob 1 is 1, and the offset bends a smooth rational curve in between,
    //   position = k * (1 + g) / (1 + g * k),  g = 3 * offset.
    // It is strictly rising with finite slopes (1 + g at the bottom, 1 / (1 + g) at the top), so every part of the
    // knob changes the sound and nothing is abrupt; with no offset it is the original linear mapping.
    const float offset=manual*values[depth]*.005f;
    const float knob=values[position]/127.f, g=3.f*offset;
    float pos=juce::jlimit(0.f,1.f,knob*(1.f+g)/(1.f+g*knob)+float(std::sin(phase))*values[movement]*.005f);
    motion.store(pos,std::memory_order_relaxed);
    tablePos[0].store(pos,std::memory_order_relaxed);tablePos[1].store(manual,std::memory_order_relaxed);tablePos[2].store(values[chord]/127.f,std::memory_order_relaxed);
    // Major mode: the third table's emphasised notes are the minor pentatonic (0,3,5,7,10 above its root). Retuning
    // the same table down a minor third gives 0,2,4,7,9 above the SAME root, the major pentatonic (6/9 chord).
    const float quality=(majorRaw!=nullptr && majorRaw->load(std::memory_order_relaxed)>.5f)?std::pow(2.f,-3.f/12.f):1.f;
    std::array<float,8> next{pos*255,manual*255,values[chord]*255/127,
        20*std::pow(1024.f,values[cutoff]/127.f),440.f,600*std::pow(2.f,values[pitch]/127.f)*quality,
        .25f*std::pow(16.f,values[resonance]/127.f),values[sub]};
    bool changed=force;
    for(int i=0;i<8;++i)changed|=std::abs(next[i]-lastDesign[i])>(i<3?.05f:.01f);
    if(changed) {
        engine.design({next[0],next[1],next[2]},{next[3],next[4],next[5]},next[6]);
        const float q[]={.50979558f,.60134489f,.89997622f,2.56291545f};
        for(int i=0;i<4;++i) { lowCut[i].lowHigh(sr,values[sub],q[i],false);highCut[i].lowHigh(sr,values[sub],q[i],true); }
        lastDesign=next;
        for(int i=0;i<128;++i) {
            const float f=20.f*std::pow(1000.f,float(i)/127.f);
            const float k=juce::jlimit(0.f,float(RackEngine::size/2-1),f*RackEngine::size/float(sr));
            const int k0=int(k);const float db=engine.desired[k0]+(engine.desired[k0+1]-engine.desired[k0])*(k-k0);
            response[i].store(juce::jlimit(0.f,1.f,(db+60.f)/90.f),std::memory_order_relaxed);
        }
    }
}
// OTT-style multiband compression: three bands (88 Hz / 2.5 kHz), each with downward compression above a high
// threshold and upward compression below a low one, fast attack and slow release, plus a little makeup. Depth scales
// the gain change in dB, so 0 % is exactly bypassed and 100 % is the full effect.
void SpectralFoundry::ott(float* io,int channels,float depth)
{
    if(depth<=1.e-4f) {
        // Keep the splitter and followers clean, so engaging the effect starts without a click.
        ottLow.reset();ottHigh.reset();ottPhase.reset();ottEnv.fill(0);ottGainDb.fill(0);
        return;
    }
    constexpr float downThreshold=-24.f,upThreshold=-34.f,downRatio=8.f,upRatio=4.f,maxBoost=36.f;
    constexpr float makeup[3]={1.5f,0.f,2.5f};
    float band[2][3]{};
    for(int ch=0;ch<channels;++ch) {
        float low,upper,mid,high;
        ottLow.processSample(ch,io[ch],low,upper);
        low=ottPhase.processSample(ch,low);
        ottHigh.processSample(ch,upper,mid,high);
        band[ch][0]=low;band[ch][1]=mid;band[ch][2]=high;
    }
    float gain[3]{};
    for(int b=0;b<3;++b) {
        float power=0;for(int ch=0;ch<channels;++ch)power+=band[ch][b]*band[ch][b];
        power/=float(std::max(channels,1));
        ottEnv[size_t(b)]=ottDetect*ottEnv[size_t(b)]+(1-ottDetect)*power;
        const float level=10.f*std::log10(ottEnv[size_t(b)]+1.e-12f);
        float g=0;
        if(level>downThreshold) g-=(level-downThreshold)*(1.f-1.f/downRatio);
        else if(level<upThreshold) {
            // Upward compression, faded out for near-silence so the noise floor is not pumped up.
            const float fade=juce::jlimit(0.f,1.f,(level+90.f)/25.f);
            g+=std::min(maxBoost,(upThreshold-level)*(1.f-1.f/upRatio))*fade;
        }
        g+=makeup[b];
        // Gain is smoothed in dB: the attack time applies when the gain is falling, the release when it is rising.
        const float coeff=g<ottGainDb[size_t(b)]?ottAttack[size_t(b)]:ottRelease[size_t(b)];
        ottGainDb[size_t(b)]=coeff*ottGainDb[size_t(b)]+(1-coeff)*g;
        g=ottGainDb[size_t(b)];
        gain[b]=juce::Decibels::decibelsToGain(g*depth);
    }
    for(int ch=0;ch<channels;++ch) io[ch]=band[ch][0]*gain[0]+band[ch][1]*gain[1]+band[ch][2]*gain[2];
}
void SpectralFoundry::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard; midi.clear();
    const int channels=std::min(2,buffer.getNumChannels());
    for(int i=0;i<count;++i)smoothed[i].setTargetValue(target(i));
    juce::Reverb::Parameters rp;rp.roomSize=.23f;rp.damping=.5f;rp.width=1;rp.wetLevel=1;rp.dryLevel=0;rp.freezeMode=0;
    reverb.setParameters(rp);
    float peak=0,inPeak=0;
    for(int s=0;s<buffer.getNumSamples();++s) {
        for(int i=0;i<count;++i)values[i]=smoothed[i].getNextValue();
        float dry[2]{},in[2]{},filtered[2]{},top[2]{},bottom[2]{},verb[2]{},bands[2][3]{};
        for(int ch=0;ch<channels;++ch) {
            float x=buffer.getSample(ch,s); if(!std::isfinite(x))x=0;
            x*=juce::Decibels::decibelsToGain(values[input]);inPeak=std::max(inPeak,std::abs(x));
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
        float comp[2]{};
        for(int ch=0;ch<channels;++ch) {
            float y=0;for(int b=0;b<3;++b)y+=bands[ch][b]*gain[b];
            comp[ch]=y*juce::Decibels::decibelsToGain(6.f*amount);
        }
        ott(comp,channels,values[tame]*.01f);
        for(int ch=0;ch<channels;++ch) {
            float y=comp[ch];
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
        if(analyzerCursor==2048) {
            constexpr int N=2048;
            for(int j=0;j<N;++j)analyzerData[j]=analyzerInput[j]*(.5f-.5f*std::cos(2*pi*j/N));
            std::fill(analyzerData.begin()+N,analyzerData.end(),0.f);
            analyzer.performRealOnlyForwardTransform(analyzerData.data(),true);
            auto mag=[&](int k){k=juce::jlimit(0,N/2,k);return std::hypot(analyzerData[2*k],analyzerData[2*k+1])/(N/2);};
            const float binHz=float(sr)/N;
            for(int b=0;b<128;++b) {
                // Log-spaced display points from 20 Hz to 20 kHz: interpolate between bins at the low end,
                // average the power of the bins that share a display point at the high end.
                const float kLo=20.f*std::pow(1000.f,(b-.5f)/127.f)/binHz,kHi=20.f*std::pow(1000.f,(b+.5f)/127.f)/binHz;
                float m;
                if(kHi-kLo<1.f) { const float kc=.5f*(kLo+kHi);const int k0=int(kc);const float fr=kc-float(k0);m=mag(k0)*(1-fr)+mag(k0+1)*fr; }
                else { float acc=0;int n=0;for(int k=int(std::ceil(kLo));k<=int(kHi);++k){const float v=mag(k);acc+=v*v;++n;} m=std::sqrt(acc/float(std::max(n,1))); }
                const float db=juce::Decibels::gainToDecibels(m+1.e-9f);
                display[b].store(juce::jlimit(0.f,1.f,(db+75.f)/75.f));
            }
            // 50% overlap keeps the display responsive.
            std::copy(analyzerInput.begin()+N/2,analyzerInput.end(),analyzerInput.begin());
            analyzerCursor=N/2;
        }
        cursor=(cursor+1)%fftSize;
        phase+=2*juce::MathConstants<double>::pi*values[rate]/sr;
        if(phase>2*juce::MathConstants<double>::pi)phase-=2*juce::MathConstants<double>::pi;
        if(++controlCounter==512){controlCounter=0;updateFilters();}
    }
    meter.store(peak);inMeter.store(inPeak);
}
void SpectralFoundry::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi)
{
    processingBypassed=true;processBlock(buffer,midi);processingBypassed=false;
}
void SpectralFoundry::getStateInformation(juce::MemoryBlock& dest)
{ auto xml=state.copyState().createXml(); copyXmlToBinary(*xml,dest); }
void SpectralFoundry::setStateInformation(const void* data,int size)
{ if(auto xml=getXmlFromBinary(data,size)) if(xml->hasTagName(state.state.getType())) state.replaceState(juce::ValueTree::fromXml(*xml)); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpectralFoundry(); }

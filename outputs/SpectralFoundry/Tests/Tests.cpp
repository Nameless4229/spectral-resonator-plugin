#include "../Source/Plugin.h"
#include <iostream>
#include <random>
#include <chrono>
#include <fstream>

int main(int argc,char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p=std::make_unique<SpectralFoundry>();
    if(argc>1 && juce::String(argv[1])=="--demo") {
        const int size=48000*8;std::vector<float> source(size),wet(size);
        for(int n=0;n<size;++n){double t=n/48000.;float env=float(.3+.7*std::exp(-std::fmod(t,60./140)*10));float v=0;
            for(int h=1;h<=32;++h)v+=std::sin(float(2*juce::MathConstants<double>::pi*(55*h*t+.06*std::sin(t*3)*h)))/h;
            source[n]=v*.16f*env;}
        p->loadPreset(1);p->prepareToPlay(48000,256);juce::MidiBuffer midi;
        for(int n=0;n<size;n+=256){int len=std::min(256,size-n);juce::AudioBuffer<float>b(2,len);
            auto* param=p->state.getParameter("wump");float w=35+90*float(n)/size;param->setValueNotifyingHost(param->convertTo0to1(w));
            for(int c=0;c<2;++c)for(int s=0;s<len;++s)b.setSample(c,s,source[n+s]);p->processBlock(b,midi);
            for(int s=0;s<len;++s)wet[n+s]=b.getSample(0,s);}
        auto save=[](const char* filename,const std::vector<float>& audio){std::ofstream f(filename,std::ios::binary);auto u16=[&](unsigned v){char b[]={char(v),char(v>>8)};f.write(b,2);};auto u32=[&](unsigned v){char b[]={char(v),char(v>>8),char(v>>16),char(v>>24)};f.write(b,4);};
            f.write("RIFF",4);u32(36+unsigned(audio.size()*2));f.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(48000);u32(96000);u16(2);u16(16);f.write("data",4);u32(unsigned(audio.size()*2));
            for(float x:audio)u16(unsigned(short(juce::jlimit(-1.f,1.f,x)*32767)));};
        save("Demo-Dry.wav",source);save("Demo-Rack-Wump-Sweep.wav",wet);return 0;
    }
    if(argc>1 && juce::String(argv[1])=="--snapshot") {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        auto image=editor->createComponentSnapshot(editor->getLocalBounds());
        juce::FileOutputStream stream(juce::File::getCurrentWorkingDirectory().getChildFile("editor.png"));
        stream.setPosition(0); stream.truncate();
        juce::PNGImageFormat{}.writeImageToStream(image,stream); return 0;
    }
    int failed=0;
    auto check=[&](bool good,const char* name){std::cout<<(good?"PASS ":"FAIL ")<<name<<std::endl; if(!good)++failed;};
    juce::MidiBuffer midi;
    for(double sr : {44100.,48000.,96000.}) {
        p->loadPreset(5); p->prepareToPlay(sr,257);
        std::vector<float> source(20000), out(20000);
        std::mt19937 random(12); std::uniform_real_distribution<float> dist(-.15f,.15f);
        for(auto& x:source)x=dist(random);
        for(int n=0;n<20000;) {
            const int size=std::min(257,20000-n); juce::AudioBuffer<float> b(2,size);
            for(int c=0;c<2;++c)for(int s=0;s<size;++s)b.setSample(c,s,source[n+s]);
            p->processBlock(b,midi); for(int s=0;s<size;++s)out[n+s]=b.getSample(0,s); n+=size;
        }
        float error=0;
        for(int n=4096;n<20000;++n)error=std::max(error,std::abs(out[n]-source[n-SpectralFoundry::fftSize]));
        std::cout<<"sample rate "<<sr<<" neutral max error "<<error<<std::endl;
        check(error<1.e-5f,"dry path reconstruction and reported latency");
    }
    p->loadPreset(2); juce::MemoryBlock state; p->getStateInformation(state);
    auto restored=std::make_unique<SpectralFoundry>(); restored->setStateInformation(state.getData(),int(state.getSize()));
    check(std::abs(restored->state.getRawParameterValue("position")->load()-29)<.001f,"state round trip");
    bool finite=true; float maximum=0; double wetDifference=0;
    auto start=std::chrono::steady_clock::now();
    for(int preset=0;preset<6;++preset) {
        p->loadPreset(preset); p->prepareToPlay(48000,512);
        for(int block=0;block<300;++block) {
            juce::AudioBuffer<float> b(2,512);
            for(int c=0;c<2;++c)for(int s=0;s<512;++s) b.setSample(c,s,.15f*std::sin(float((block*512+s)*.023+c)));
            p->processBlock(b,midi);
            for(int c=0;c<2;++c)for(int s=0;s<512;++s){const float v=b.getSample(c,s); finite &=std::isfinite(v);maximum=std::max(maximum,std::abs(v)); if(preset==1&&block>10)wetDifference+=std::abs(v-.15f*std::sin(float((block*512+s-SpectralFoundry::fftSize)*.023+c)));}
        }
    }
    auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    check(finite&&maximum<=.981f,"all presets produce finite bounded audio");
    check(wetDifference>1,"effect changes the signal");
    std::cout<<"Processed 19.2 seconds of stereo audio in "<<elapsed<<" seconds; peak "<<maximum<<std::endl;
    // Exercise discontinuous host automation and unusual block sizes in mono.
    p->setPlayConfigDetails(1,1,48000,64); p->prepareToPlay(48000,64);
    bool automationFinite=true;
    for(int block=0;block<200;++block) {
        for(auto* param:p->getParameters()) param->setValueNotifyingHost((block%2)==0?0.0f:1.0f);
        const int size=1+(block*97)%1031;
        juce::AudioBuffer<float> b(1,size); b.clear();
        b.setSample(0,0,block%2==0?1.0f:-1.0f); p->processBlock(b,midi);
        for(int s=0;s<size;++s) automationFinite &=std::isfinite(b.getSample(0,s)) && std::abs(b.getSample(0,s))<=.981f;
    }
    juce::AudioBuffer<float> zero(1,0); p->processBlock(zero,midi);
    check(automationFinite,"mono, extreme automation, irregular and zero-sized blocks");
    p->loadPreset(1); p->prepareToPlay(48000,128);
    bool bypassCorrect=true;
    for(int block=0;block<40;++block) {
        juce::AudioBuffer<float> b(1,128); b.clear(); if(block==0)b.setSample(0,0,.25f);
        p->processBlockBypassed(b,midi);
        for(int s=0;s<128;++s) bypassCorrect &=std::abs(b.getSample(0,s)-((block*128+s)==SpectralFoundry::fftSize?.25f:0.0f))<1.e-7f;
    }
    check(bypassCorrect,"host bypass preserves latency and unity gain");
    {
        auto engine=std::make_unique<RackEngine>();engine->prepare(48000);engine->design({0,0,0},{440,440,440},1,true);
        float error=0;
        for(int n=0;n<10000;++n){float l,r;engine->tick(n==0?.25f:0,0,l,r);error=std::max(error,std::abs(l-(n==256?.25f:0)));error=std::max(error,std::abs(r));}
        check(error<1.e-6f,"partitioned minimum-phase engine identity impulse and channel isolation");
    }
    p->setPlayConfigDetails(2,2,48000,256);
    std::vector<float> noise(48000);std::mt19937 rng(7);std::uniform_real_distribution<float> rand(-.2f,.2f);for(auto& x:noise)x=rand(rng);
    const char* ids[]={"depth","position","cutoff","resonance","wump","chord","pitch","sub"};
    const float ranges[][2]={{0,100},{15,100},{45,95},{20,115},{30,115},{0,127},{0,127},{30,250}};
    for(int m=0;m<8;++m) {
        std::vector<float> result[2];
        for(int end=0;end<2;++end){p->loadPreset(1);auto set=[&](const char* id,float v){auto* q=p->state.getParameter(id);q->setValueNotifyingHost(q->convertTo0to1(v));};set("wump",90);set("space",0);set("tame",0);set(ids[m],ranges[m][end]);p->prepareToPlay(48000,256);
            for(int n=0;n<48000;n+=256){int len=std::min(256,48000-n);juce::AudioBuffer<float>b(2,len);for(int c=0;c<2;++c)for(int s=0;s<len;++s)b.setSample(c,s,noise[n+s]);p->processBlock(b,midi);if(n>12000)for(int s=0;s<len;++s)result[end].push_back(b.getSample(0,s));}}
        double aa=0,bb=0,ab=0;for(size_t i=0;i<result[0].size();++i){double a=result[0][i],b=result[1][i];aa+=a*a;bb+=b*b;ab+=a*b;}
        double correlation=ab/std::sqrt(std::max(1.e-30,aa*bb));double distance=std::sqrt(std::max(0.,2-2*correlation));
        std::cout<<"Macro "<<ids[m]<<" level-normalized difference "<<distance<<std::endl;
        check(distance>.05,"macro changes timbre after level normalization");
        // Also compare magnitude spectra, so a phase shift alone cannot pass.
        juce::dsp::FFT spectrumFFT(11);std::array<float,4096> data{};
        std::array<double,1025> powers[2]{};
        for(int end=0;end<2;++end)for(size_t n=0;n+2048<result[end].size();n+=1024) {
            data.fill(0);for(int j=0;j<2048;++j)data[j]=result[end][n+j]*(.5f-.5f*std::cos(2*juce::MathConstants<float>::pi*j/2048));
            spectrumFFT.performRealOnlyForwardTransform(data.data(),true);
            for(int k=1;k<=1024;++k)powers[end][k]+=data[2*k]*data[2*k]+data[2*k+1]*data[2*k+1];
        }
        double mean=0,variance=0;int used=0;
        for(int k=4;k<600;++k)if(powers[0][k]>1.e-6&&powers[1][k]>1.e-6){double d=10*std::log10(powers[1][k]/powers[0][k]);mean+=d;variance+=d*d;++used;}
        double spectralDifference=used>0?std::sqrt(std::max(0.,variance/used-std::pow(mean/used,2))):0;
        std::cout<<"Macro "<<ids[m]<<" spectral shape difference "<<spectralDifference<<" dB"<<std::endl;
        check(spectralDifference>.5,"macro changes spectral shape independently of level and phase");
    }
    return failed==0?0:1;
}

#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <complex>
#include <cstring>
#include <BinaryData.h>

// Original minimum-phase FIR implementation driven by the three ADG wavetables.
// Table conversion and the proprietary Filter Table implementation are not identical.
class RackEngine {
public:
    static constexpr int block=256, size=4096, parts=size/block, bins=block+1;
    using C=std::complex<float>;
    using Spectrum=std::array<C,bins>;
    RackEngine() {
        const char* data[]={BinaryData::table0_bin,BinaryData::table1_bin,BinaryData::table2_bin};
        for(int i=0;i<3;++i) std::memcpy(tables[i].data(),data[i],tables[i].size()*sizeof(float));
    }
    void prepare(double sampleRate) { sr=sampleRate; reset(); }
    void reset() {
        for(auto& ch:history)for(auto& s:ch)s.fill({});
        for(auto& ch:input)ch.fill(0);
        for(auto& ch:output)ch.fill(0);
        for(auto& ch:overlap)ch.fill(0);
        cursor=head=0; initial=true;
    }
    float harmonicDb(int table,float frame,float harmonic) const {
        frame=juce::jlimit(0.f,255.f,frame);
        harmonic=juce::jlimit(1.f,1024.f,harmonic);
        int f=int(frame),f1=std::min(255,f+1),h=int(harmonic),h1=std::min(1024,h+1);
        auto sample=[&](int fr) { const auto* a=tables[table].data()+fr*1025; return a[h]+(a[h1]-a[h])*(harmonic-h); };
        return sample(f)+(sample(f1)-sample(f))*(frame-f);
    }
    void design(const std::array<float,3>& frames,const std::array<float,3>& cutoffs,float resonance,bool flat=false) {
        for(int k=0;k<=size/2;++k) {
            float f=float(sr*k/size), db=0;
            if(!flat) for(int t=0;t<3;++t) {
                const float harmonic=f*24/cutoffs[t];
                float v=harmonicDb(t,frames[t],harmonic);
                if(harmonic>1024) v-=std::min(60.f,24.f*std::log2(harmonic/1024));
                db+=v*(t==0?resonance:1.f);
            }
            // Retain deep table notches; cap only extreme aggregate boosts.
            db=juce::jlimit(-100.f,30.f,db);
            desired[k]=db;
            workA[k]=C(db*(2.302585093f/20.f),0);
            if(k>0 && k<size/2)workA[size-k]=workA[k];
        }
        longFFT.perform(workA.data(),workB.data(),true);
        for(int n=1;n<size/2;++n)workB[n]*=2.f;
        for(int n=size/2+1;n<size;++n)workB[n]=C{};
        longFFT.perform(workB.data(),workA.data(),false);
        for(auto& v:workA)v=std::exp(v);
        longFFT.perform(workA.data(),workB.data(),true);
        for(int p=0;p<parts;++p) {
            shortData.fill(0);
            for(int j=0;j<block;++j) {
                const int n=p*block+j;
                const float taper=n<size-block?1.f:.5f+.5f*std::cos(juce::MathConstants<float>::pi*float(n-(size-block))/block);
                shortData[j]=workB[n].real()*taper;
            }
            shortFFT.performRealOnlyForwardTransform(shortData.data(),true);
            for(int k=0;k<bins;++k) {
                target[p][k]=C(shortData[2*k],shortData[2*k+1]);
                if(initial)current[p][k]=target[p][k];
            }
        }
        initial=false;
    }
    void tick(float left,float right,float& outLeft,float& outRight) {
        outLeft=output[0][cursor]; outRight=output[1][cursor];
        input[0][cursor]=left; input[1][cursor]=right;
        if(++cursor<block)return;
        cursor=0;
        for(int p=0;p<parts;++p)for(int k=0;k<bins;++k)current[p][k]+=.3f*(target[p][k]-current[p][k]);
        for(int ch=0;ch<2;++ch) {
            shortData.fill(0); std::copy(input[ch].begin(),input[ch].end(),shortData.begin());
            shortFFT.performRealOnlyForwardTransform(shortData.data(),true);
            for(int k=0;k<bins;++k)history[ch][head][k]=C(shortData[2*k],shortData[2*k+1]);
            shortData.fill(0);
            for(int k=0;k<bins;++k) {
                C sum{};
                for(int p=0;p<parts;++p)sum+=current[p][k]*history[ch][(head-p+parts)%parts][k];
                shortData[2*k]=sum.real(); shortData[2*k+1]=sum.imag();
            }
            shortFFT.performRealOnlyInverseTransform(shortData.data());
            for(int j=0;j<block;++j) { output[ch][j]=shortData[j]+overlap[ch][j]; overlap[ch][j]=shortData[j+block]; }
        }
        head=(head+1)%parts;
    }
    std::array<float,size/2+1> desired{};
private:
    std::array<std::array<float,256*1025>,3> tables{};
    juce::dsp::FFT longFFT{12},shortFFT{9};
    std::array<C,size> workA{},workB{};
    std::array<Spectrum,parts> target{},current{};
    std::array<std::array<Spectrum,parts>,2> history{};
    std::array<float,block*4> shortData{};
    std::array<std::array<float,block>,2> input{},output{},overlap{};
    int cursor=0,head=0;
    double sr=48000;
    bool initial=true;
};

struct RackBiquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0;
    std::array<float,2> z1{},z2{};
    void reset(){z1.fill(0);z2.fill(0);}
    float tick(int ch,float x){float y=b0*x+z1[ch];z1[ch]=b1*x-a1*y+z2[ch];z2[ch]=b2*x-a2*y;return y;}
    void set(const std::array<float,6>& c){b0=c[0]/c[3];b1=c[1]/c[3];b2=c[2]/c[3];a1=c[4]/c[3];a2=c[5]/c[3];}
    void lowHigh(double sr,float f,float q,bool high) {
        float w=float(2*juce::MathConstants<double>::pi*f/sr), c=std::cos(w),a=std::sin(w)/(2*q);
        float b=high?(1+c)/2:(1-c)/2;
        set({b,high?-2*b:2*b,b,1+a,-2*c,1-a});
    }
    void peak(double sr,float f,float q,float db) {
        float a=std::pow(10.f,db/40),w=float(2*juce::MathConstants<double>::pi*f/sr),c=std::cos(w),s=std::sin(w)/(2*q);
        set({1+s*a,-2*c,1-s*a,1+s/a,-2*c,1-s/a});
    }
};

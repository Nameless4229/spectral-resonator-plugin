#include "Plugin.h"

namespace {
const juce::Colour bg(0xff10131b), panel(0xff1a1e29), ink(0xffedf0f8), muted(0xff8791a8), pink(0xffff579c), cyan(0xff61dce4);
struct FoundryLook : juce::LookAndFeel_V4 {
    FoundryLook() {
        setColour(juce::Slider::textBoxTextColourId,ink);
        setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);
        setColour(juce::ComboBox::backgroundColourId,panel);
        setColour(juce::ComboBox::textColourId,ink);
        setColour(juce::ComboBox::outlineColourId,juce::Colour(0xff333a4b));
        setColour(juce::PopupMenu::backgroundColourId,panel);
        setColour(juce::PopupMenu::textColourId,ink);
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float value,float start,float end,juce::Slider& s) override {
        auto r=juce::Rectangle<float>(float(x),float(y),float(w),float(h)).reduced(9);
        const float radius=std::min(r.getWidth(),r.getHeight())*.5f;
        auto centre=r.getCentre();
        const float angle=start+value*(end-start);
        juce::Path base,arc;
        base.addCentredArc(centre.x,centre.y,radius,radius,0,start,end,true);
        arc.addCentredArc(centre.x,centre.y,radius,radius,0,start,angle,true);
        g.setColour(juce::Colour(0xff343b4e)); g.strokePath(base,juce::PathStrokeType(3.5f));
        auto c=s.findColour(juce::Slider::rotarySliderFillColourId);
        g.setColour(c); g.strokePath(arc,juce::PathStrokeType(3.5f));
        g.setColour(juce::Colour(0xff252c3c)); g.fillEllipse(centre.x-radius+7,centre.y-radius+7,2*radius-14,2*radius-14);
        g.setColour(ink);
        g.drawLine(centre.x+std::sin(angle)*(radius-17),centre.y-std::cos(angle)*(radius-17),
            centre.x+std::sin(angle)*(radius-8),centre.y-std::cos(angle)*(radius-8),2.0f);
    }
};
class Editor final : public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit Editor(SpectralFoundry& processor):AudioProcessorEditor(processor),p(processor) {
        setLookAndFeel(&look);
        const char* ids[]={"depth","position","cutoff","resonance","wump","chord","pitch","sub","rate","movement","space","drive","tame","punch","mix","output"};
        const char* names[]={"SPECTRAL DEPTH","POSITION","CUTOFF","RESONANCE","WUMP","CHORD DEPTH","CHORD PITCH","SUB CROSSOVER","RATE","MOVEMENT","SPACE","FOLD DRIVE","PEAK TAMING","COMPRESSION","DRY / WET","OUTPUT"};
        const char* tips[]={
            "Shaper modulation depth from the rack. It changes how Wump offsets the first table position; it is not an effect mix.",
            "Scans the first recovered wavetable, with the rack Shaper modulation added.",
            "First Filter Table cutoff: exponential 20 to 20480 Hz. Harmonic 24 is at the cutoff.",
            "First table resonance: exponential 0.25 to 4, using the ADG range.",
            "Scans the second recovered table AND drives the manual Shaper ramp. This is the original Wump mapping.",
            "Scans the third recovered wavetable from frame 0 to 255. It is not a dry/wet control.",
            "Third table cutoff: exponential 600 to 1200 Hz, as saved in the rack.",
            "48 dB/octave low and high branches, 10 to 300 Hz. The final dynamics and saturation process their sum.",
            "Free-running spectral motion in Hz. Not host tempo synced.",
            "Optional added motion. Zero matches the manual Shaper mode used in the video and rack.",
            "Adds a dark stereo algorithmic reverb to the upper band.",
            "Additional drive into the final full-wet sine-fold stage. Zero matches the rack drive setting.",
            "Adaptive high-frequency de-harsh stage. Uses the saved soothe2 focus frequencies, but is an approximation.",
            "At 100%, applies the rack's 120/2500 Hz three-band dynamics, +9.14 dB upper-band input and +6 dB output.",
            "Latency-aligned dry/wet blend.","Final level, followed by a full-scale safety ceiling."};
        for(int i=0;i<16;++i) {
            sliders[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            sliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,22);
            sliders[i].setColour(juce::Slider::rotarySliderFillColourId,i<4?pink:(i<8?cyan:juce::Colour(0xffa8b6da)));
            sliders[i].setTooltip(tips[i]); sliders[i].setName(names[i]);
            sliders[i].setNumDecimalPlacesToDisplay((i==8)?2:1);
            labels[i].setText(names[i],juce::dontSendNotification);
            labels[i].setJustificationType(juce::Justification::centred);
            labels[i].setFont(juce::FontOptions(11.0f)); labels[i].setColour(juce::Label::textColourId,muted);
            addAndMakeVisible(sliders[i]); addAndMakeVisible(labels[i]);
            attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,ids[i],sliders[i]);
            sliders[i].textFromValueFunction=[i](double v) {
                const auto unit=(i==0||i==9||i==10||i==12||i==13||i==14)?" %":(i==7||i==8)?" Hz":(i==11||i==15)?" dB":"";
                return juce::String(v,i==8?2:1)+unit;
            };
            sliders[i].valueFromTextFunction=[](const juce::String& text){return text.getDoubleValue();};
            sliders[i].updateText();
        }
        presets.addItemList({"01  ADG - saved rack","02  Video - Wump 70","03  Video - low position","04  Video - low chord","05  Extra motion / drive","06  Dry / compare"},1);
        presets.setTextWhenNothingSelected("STARTING POINTS");
        presets.onChange=[this]{if(presets.getSelectedId()>0)p.loadPreset(presets.getSelectedId()-1);};
        presets.setTooltip("Saved ADG values and video snapshots. Uses the three embedded original tables with an independent filter engine.");
        addAndMakeVisible(presets);
        setSize(960,700);
        startTimerHz(25);
    }
    ~Editor() override { stopTimer(); setLookAndFeel(nullptr); }
    void resized() override {
        presets.setBounds(650,25,280,34);
        for(int i=0;i<8;++i) {
            const int x=28+(i%4)*226,y=260+(i/4)*142;
            labels[i].setBounds(x,y,210,20);
            sliders[i].setBounds(x+45,y+18,120,114);
        }
        for(int i=8;i<16;++i) {
            const int x=25+(i-8)*114;
            labels[i].setBounds(x,572,112,20);
            sliders[i].setBounds(x+8,593,96,81);
        }
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(bg);
        g.setColour(pink); g.fillRoundedRectangle(28,27,4,29,2);
        g.setColour(ink); g.setFont(juce::FontOptions(26.0f,juce::Font::bold));
        g.drawText("SPECTRAL FOUNDRY",44,20,420,38,juce::Justification::centredLeft);
        g.setColour(muted); g.setFont(juce::FontOptions(11.0f));
        g.drawText("VERSION 2  /  ORIGINAL RACK TABLES + MACRO ROUTING",45,56,500,20,juce::Justification::centredLeft);
        g.setColour(panel); g.fillRoundedRectangle(28,96,904,140,12);
        g.setColour(juce::Colour(0xff303749));
        for(int i=1;i<8;++i) g.drawVerticalLine(45+i*107,123,215);
        for(int i=0;i<3;++i) g.drawHorizontalLine(138+i*29,45,910);
        juce::Path curve;
        for(int i=0;i<128;++i) {
            const float x=45+float(i)*865/127,y=216-levels[i]*96;
            if(i==0) curve.startNewSubPath(x,y); else curve.lineTo(x,y);
        }
        g.setColour(cyan); g.strokePath(curve,juce::PathStrokeType(1.6f));
        curve.lineTo(910,218); curve.lineTo(45,218); curve.closeSubPath();
        g.setGradientFill(juce::ColourGradient(cyan.withAlpha(.17f),0,130,cyan.withAlpha(.01f),0,218,false)); g.fillPath(curve);
        g.setColour(muted); g.setFont(juce::FontOptions(10.0f));
        g.drawText("OUTPUT SPECTRUM",45,102,200,18,juce::Justification::centredLeft);
        g.drawText("3 RACK TABLES  >  EQ / SPACE + SUB  >  MULTIBAND  >  FOLD",370,102,540,18,juce::Justification::centredRight);
        g.setColour(pink.withAlpha(.6f));
        const float mx=45+865*p.motion.load(); g.drawVerticalLine(int(mx),123,217);
        for(int i=0;i<8;++i) {
            g.setColour(panel); g.fillRoundedRectangle(float(28+(i%4)*226),float(256+(i/4)*142),210,134,10);
        }
        g.setColour(juce::Colour(0xff303749)); g.drawHorizontalLine(557,28,932);
        g.setColour(muted); g.setFont(juce::FontOptions(10.0f));
        g.drawText("ADG TABLES + MAPPINGS  /  INDEPENDENT DSP",28,680,410,16,juce::Justification::centredLeft);
        g.drawText("256 SAMPLE LATENCY   |   OUT "+juce::String(juce::Decibels::gainToDecibels(shownPeak,-90.0f),1)+" dB",570,680,361,16,juce::Justification::centredRight);
    }
private:
    SpectralFoundry& p;
    FoundryLook look;
    juce::TooltipWindow tooltip {this,500};
    std::array<juce::Slider,16> sliders;
    std::array<juce::Label,16> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,16> attachments;
    juce::ComboBox presets;
    std::array<float,128> levels {};
    float shownPeak=0;
    void timerCallback() override {
        for(int i=0;i<128;++i) levels[i]=std::max(p.display[i].load(),levels[i]*.92f);
        shownPeak=std::max(p.meter.load(),shownPeak*.9f); repaint();
    }
};
}
juce::AudioProcessorEditor* SpectralFoundry::createEditor() { return new Editor(*this); }

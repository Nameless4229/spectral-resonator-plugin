#include "Plugin.h"

// Spectral Resonator editor: a drawn-in-code "dusk city" chassis. The whole interface is laid out on a fixed
// 1100 x 700 canvas and scaled to the window, so the plugin is freely resizable (fixed aspect ratio).
namespace {
// A theme is every colour the interface uses. There is one look, the Blossom theme.
struct Theme {
    juce::Colour night, deep, violet, peri, magenta, orange, amber, cyan;   // chassis neutrals and the four accents
    juce::Colour ink, muted;                                                // text on plates
    juce::Colour inkOnDark, mutedOnDark;                                    // text on the dark display
    juce::Colour plateTop, plateBottom, bgTop, bgBottom, overlay, displayBg, glow, starA, starB;
    juce::Colour groove, curve, chordName;                                  // fader groove, filter curve, chord name
    juce::Colour metal;                                                     // light tint of the knobs' brushed metal
    juce::Colour value;                                                     // the numbers under every knob and fader
    juce::Colour accent;                                                    // fills, arcs, pointers and lit ticks of every control
    std::array<juce::Colour, 7> ramp;                                       // waterfall colours, low to high frequency
    int image = 0;                                                          // 1 = the Blossom photo behind the spectrum
};
Theme blossomTheme() {   // ivory body and plates, plum text, rose / coral / gold / sky accents; photo only behind the spectrum
    Theme t;
    t.night = juce::Colour(0xff3a1c33); t.deep = juce::Colour(0xfffffdfb); t.violet = juce::Colour(0xfff1c3d6);
    t.peri = juce::Colour(0xffc77d9f); t.magenta = juce::Colour(0xffd23a73); t.orange = juce::Colour(0xfff0714f);
    t.amber = juce::Colour(0xffd89a22); t.cyan = juce::Colour(0xff2f93c4);
    t.ink = juce::Colour(0xff3a1c33); t.muted = juce::Colour(0xff7d4a67);
    t.inkOnDark = juce::Colour(0xff3a1c33); t.mutedOnDark = juce::Colour(0xff7d4a67);   // the display is light in this theme
    t.plateTop = juce::Colour(0xfffcf9f6); t.plateBottom = juce::Colour(0xfff0e8e4);
    t.bgTop = juce::Colour(0xfff5f0ec); t.bgBottom = juce::Colour(0xffe7dfda); t.overlay = juce::Colours::transparentBlack;
    t.displayBg = juce::Colour(0xfffbf6f4).withAlpha(.5f); t.glow = juce::Colour(0xffd23a73).withAlpha(.16f);
    t.starA = t.magenta; t.starB = juce::Colour(0xfff0714f);
    t.ramp = {juce::Colour(0xffd23a73), juce::Colour(0xfff0714f), juce::Colour(0xffd89a22), juce::Colour(0xff3aa876),
              juce::Colour(0xff2f93c4), juce::Colour(0xff6a5ad6), juce::Colour(0xffa64fc8)};
    t.groove = juce::Colour(0xff7a716e); t.curve = juce::Colour(0xff3a1c33); t.chordName = juce::Colour(0xffb02a5a);
    t.metal = juce::Colour(0xfff4e9ec);
    t.value = juce::Colour(0xffb02a5a); t.accent = juce::Colour(0xffd23a73);
    t.image = 1;
    return t;
}

constexpr float W = 1100.f, H = 700.f;
constexpr int kSnapHint = 101, kModeHint = 102;
constexpr int kParams = 17, kRows = 40, kBins = 128;

// Parameter order matches the processor: 0-3 faders, 4-7 macros, 8-9 motion, 10-14 finish + mix, 15 output, 16 input.
const char* const ids[kParams] = {"depth","position","cutoff","resonance","wump","chord","pitch","sub","rate","movement",
    "space","drive","tame","punch","mix","output","input"};
const char* const fullNames[kParams] = {"SPECTRAL DEPTH","POSITION","CUTOFF","RESONANCE","WUMP","CHORD DEPTH","CHORD PITCH",
    "SUB X-OVER","RATE","MOVEMENT","SPACE","FOLD DRIVE","OTT","COMPRESSION","DRY / WET","OUTPUT","INPUT"};
const char* const shortNames[kParams] = {"DEPTH","POSITION","CUTOFF","RES","WUMP","CHORD DEPTH","CHORD PITCH",
    "SUB X-OVER","RATE","MOVEMENT","SPACE","FOLD DRIVE","OTT","COMPRESSION","DRY / WET","OUTPUT","INPUT"};
const char* const tips[kParams] = {
    "Shaper modulation depth from the rack. It changes how Wump offsets the first table position; it is not an effect mix. Position always keeps its full travel as Depth changes, so there is no dead zone.",
    "Scans the first recovered wavetable, with the rack Shaper modulation added.",
    "First Filter Table cutoff: exponential 20 to 20480 Hz. Harmonic 24 is at the cutoff.",
    "First table resonance: exponential 0.25 to 4, using the ADG range.",
    "Scans the second recovered table AND drives the manual Shaper ramp. This is the original Wump mapping.",
    "Scans the third recovered wavetable from frame 0 to 255. It is not a dry/wet control.",
    "Root note of the chord. Third table cutoff, 600 to 1200 Hz exponential; harmonic 24 sits at that cutoff, so the table root is cutoff / 24. The bracket shows the raw 0-127 value. With SNAP TO NOTES on it steps between the 12 semitones. MINOR / MAJOR switches the chord quality.",
    "48 dB/octave low and high branches, 10 to 300 Hz. The final dynamics and saturation process their sum.",
    "Free-running spectral motion in Hz. Not host tempo synced.",
    "Optional added motion. Zero matches the manual Shaper mode used in the video and rack.",
    "Adds a dark stereo algorithmic reverb to the upper band.",
    "Additional drive into the final full-wet sine-fold stage. Zero matches the rack drive setting.",
    "OTT-style multiband compression: three bands (88 Hz and 2.5 kHz), each compressed downward above a high threshold and upward below a low one, with fast attack. The amount is the depth: 0% is off, 100% is full OTT.",
    "At 100%, applies the rack's 120/2500 Hz three-band dynamics, +9.14 dB upper-band input and +6 dB output.",
    "Latency-aligned dry/wet blend. 100% is the full effect.",
    "Final level, followed by a full-scale safety ceiling.",
    "Level trim before the rack. It also changes the dry signal that Dry / Wet blends back in."};

int tabOf(int i) { return i < 4 ? -1 : i < 8 ? 0 : i < 10 ? 1 : i < 15 ? 2 : -1; }
// Chord Pitch maps 0..127 to a table cutoff of 600..1200 Hz; the table's harmonic 24 sits at the cutoff, so its root is cutoff / 24.
struct RootNote { juce::String name; int midi; float cents; };
RootNote rootNoteFor(float pitch) {
    static const char* const names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    const double hz = 25.0 * std::pow(2.0, double(pitch) / 127.0);
    const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
    const int n = int(std::lround(midi));
    return {juce::String(names[((n % 12) + 12) % 12]) + juce::String(n / 12 - 1), n, float((midi - n) * 100.0)};
}
juce::String rootText(float pitch) {
    const auto r = rootNoteFor(pitch);
    const int c = int(std::lround(r.cents));
    return r.name + (c == 0 ? juce::String() : (c > 0 ? "  +" : "  ") + juce::String(c) + "c") + "  (" + juce::String(pitch, 1) + ")";
}
juce::String unitOf(int i) {
    return (i == 0 || i == 9 || i == 10 || i == 12 || i == 13 || i == 14) ? " %" : (i == 7 || i == 8) ? " Hz"
         : (i == 11 || i == 15 || i == 16) ? " dB" : "";
}

// Slider whose drag can be quantised (used by Chord Pitch to step between notes).
struct FoundrySlider : juce::Slider {
    std::function<double(double)> snapper;
    double snapValue(double attempted, DragMode) override { return snapper ? snapper(attempted) : attempted; }
};

struct FoundryLook : juce::LookAndFeel_V4 {
    Theme T = blossomTheme();
    FoundryLook() { applyColours(); }
    void applyColours() {
        setColour(juce::Slider::textBoxTextColourId, T.ink);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, T.magenta.withAlpha(.5f));
        setColour(juce::ComboBox::backgroundColourId, T.deep);
        setColour(juce::ComboBox::textColourId, T.ink);
        setColour(juce::ComboBox::outlineColourId, T.peri.withAlpha(.45f));
        setColour(juce::ComboBox::arrowColourId, T.magenta);
        setColour(juce::PopupMenu::backgroundColourId, T.deep);
        setColour(juce::PopupMenu::textColourId, T.ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, T.magenta.withAlpha(.35f));
        setColour(juce::PopupMenu::highlightedTextColourId, T.ink);
        setColour(juce::TextButton::textColourOffId, T.muted);
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }
    int getSliderThumbRadius(juce::Slider&) override { return 11; }
    juce::Label* createSliderTextBox(juce::Slider& s) override {
        auto* l = LookAndFeel_V4::createSliderTextBox(s);
        l->setFont(juce::FontOptions(12.5f, juce::Font::bold));
        return l;
    }
    // Spun-metal knob: knurled dark bezel, brushed face, a short accent pointer and a fine tick ring that lights up to the
    // value. Metal tint, ticks and accent all come from the active theme.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float value, float start, float end,
                          juce::Slider& s) override {
        const auto area = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(3);
        const float ro = std::min(area.getWidth(), area.getHeight()) * .5f;
        const auto c = area.getCentre();
        const float angle = start + value * (end - start);
        const auto acc = s.findColour(juce::Slider::rotarySliderFillColourId);
        const bool big = ro > 26.f;
        const float pi2 = juce::MathConstants<float>::twoPi;

        // Tick ring.
        if(big) {
            const int ticks = 31;
            for(int i = 0; i < ticks; ++i) {
                const float a = start + (end - start) * float(i) / float(ticks - 1);
                const bool major = (i % 5) == 0;
                const float r0 = ro * .85f, r1 = ro * (major ? .99f : .93f);
                g.setColour(a <= angle + 1.e-3f ? acc : T.muted.withAlpha(major ? .7f : .45f));
                g.drawLine(c.x + std::sin(a) * r0, c.y - std::cos(a) * r0, c.x + std::sin(a) * r1, c.y - std::cos(a) * r1, major ? 1.5f : 1.f);
            }
        }

        // Body: soft shadow, knurled bezel, brushed face.
        const float fb = ro * (big ? .76f : .8f);
        g.setColour(juce::Colours::black.withAlpha(.12f)); g.fillEllipse(c.x - fb - 1.5f, c.y - fb + 3.f, 2 * fb + 3.f, 2 * fb + 3.f);
        g.setGradientFill(juce::ColourGradient(T.night.brighter(.45f), c.x - fb * .5f, c.y - fb * .7f,
                                               T.night.darker(.35f), c.x + fb * .6f, c.y + fb, false));
        g.fillEllipse(c.x - fb, c.y - fb, 2 * fb, 2 * fb);
        const float fr = fb - std::max(2.4f, fb * .12f);
        if(big) {   // knurling on the bezel
            g.setColour(juce::Colours::white.withAlpha(.2f));
            for(int i = 0; i < 64; ++i) {
                const float a = pi2 * float(i) / 64.f;
                g.drawLine(c.x + std::sin(a) * (fr + .6f), c.y - std::cos(a) * (fr + .6f),
                           c.x + std::sin(a) * (fb - .5f), c.y - std::cos(a) * (fb - .5f), .8f);
            }
        }
        {
            const juce::Colour light = T.metal, dark = light.interpolatedWith(T.night, .42f);
            juce::Path face;
            face.addEllipse(c.x - fr, c.y - fr, 2 * fr, 2 * fr);
            juce::Graphics::ScopedSaveState keep(g);
            g.reduceClipRegion(face);
            const juce::Colour base = dark.interpolatedWith(light, .56f);
            g.setColour(base); g.fillEllipse(c.x - fr, c.y - fr, 2 * fr, 2 * fr);   // solid base: no seams show through
            const int wedges = 96;
            for(int i = 0; i < wedges; ++i) {
                const float a0 = pi2 * float(i) / float(wedges), a1 = a0 + pi2 / float(wedges) * 1.08f, mid = (a0 + a1) * .5f;
                const float b = juce::jlimit(0.f, 1.f, .56f + .26f * std::cos(2.f * (mid - .8f)) + .07f * std::cos(6.f * (mid - .3f)));
                juce::Path wedge;
                wedge.startNewSubPath(c.x, c.y);
                wedge.lineTo(c.x + std::sin(a0) * fr * 1.1f, c.y - std::cos(a0) * fr * 1.1f);
                wedge.lineTo(c.x + std::sin(a1) * fr * 1.1f, c.y - std::cos(a1) * fr * 1.1f);
                wedge.closeSubPath();
                g.setColour(dark.interpolatedWith(light, b)); g.fillPath(wedge);
            }
            // Centre cap hides the point where the brushing wedges meet.
            const float cap = fr * .2f;
            g.setGradientFill(juce::ColourGradient(light.brighter(.1f), c.x - cap * .4f, c.y - cap * .5f, base.darker(.1f), c.x + cap * .5f, c.y + cap * .6f, true));
            g.fillEllipse(c.x - cap, c.y - cap, 2 * cap, 2 * cap);
            g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(.2f), c.x - fr * .45f, c.y - fr * .55f,
                                                   juce::Colours::transparentWhite, c.x + fr * .25f, c.y + fr * .35f, true));
            g.fillEllipse(c.x - fr, c.y - fr, 2 * fr, 2 * fr);
        }
        g.setColour(juce::Colours::black.withAlpha(.5f)); g.drawEllipse(c.x - fr, c.y - fr, 2 * fr, 2 * fr, 1.f);

        // Pointer.
        const float px = std::sin(angle), py = -std::cos(angle);
        juce::Path pointer;
        pointer.startNewSubPath(c.x + px * fr * .5f, c.y + py * fr * .5f);
        pointer.lineTo(c.x + px * fr * .9f, c.y + py * fr * .9f);
        g.setColour(acc); g.strokePath(pointer, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // Slim inset fader: scale ticks either side, a dark groove, an accent fill with fine ridges and a metal cap.
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                          juce::Slider::SliderStyle style, juce::Slider& s) override {
        if(style != juce::Slider::LinearVertical) return;
        const auto acc = s.findColour(juce::Slider::rotarySliderFillColourId);
        const float cx = float(x) + float(w) * .5f;
        const float top = float(y), bottom = float(y + h);

        for(int k = 0; k <= 20; ++k) {
            const float yy = top + (bottom - top) * float(k) / 20.f;
            const bool major = (k % 5) == 0;
            g.setColour(T.muted.withAlpha(major ? .55f : .3f));
            const float len = major ? 7.f : 4.f;
            g.drawLine(cx - 12.f - len, yy, cx - 12.f, yy, 1.f);
            g.drawLine(cx + 12.f, yy, cx + 12.f + len, yy, 1.f);
        }

        const juce::Rectangle<float> groove(cx - 6.5f, top - 8.f, 13.f, (bottom - top) + 16.f);
        g.setGradientFill(juce::ColourGradient(T.groove.darker(.35f), groove.getX(), 0, T.groove.brighter(.1f), groove.getRight(), 0, false));
        g.fillRoundedRectangle(groove, 6.5f);
        g.setColour(juce::Colours::black.withAlpha(.35f)); g.drawRoundedRectangle(groove, 6.5f, 1.f);

        const juce::Rectangle<float> fill(groove.getX() + 2.5f, pos, groove.getWidth() - 5.f, groove.getBottom() - 2.5f - pos);
        if(fill.getHeight() > 0) {
            g.setGradientFill(juce::ColourGradient(acc.brighter(.3f), fill.getX(), 0, acc.darker(.2f), fill.getRight(), 0, false));
            g.fillRoundedRectangle(fill, 3.5f);
            g.setColour(juce::Colours::white.withAlpha(.22f));
            for(float yy = fill.getBottom() - 3.f; yy > fill.getY() + 1.f; yy -= 4.f) g.drawHorizontalLine(int(yy), fill.getX() + .5f, fill.getRight() - .5f);
        }

        const juce::Rectangle<float> cap(cx - 17.f, pos - 9.5f, 34.f, 19.f);
        g.setColour(juce::Colours::black.withAlpha(.2f)); g.fillRoundedRectangle(cap.translated(0, 2.f), 5.f);
        g.setGradientFill(juce::ColourGradient(T.metal.brighter(.15f), 0, cap.getY(), T.metal.interpolatedWith(T.night, .45f), 0, cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 5.f);
        g.setColour(juce::Colours::black.withAlpha(.45f)); g.drawRoundedRectangle(cap, 5.f, 1.f);
        g.setColour(juce::Colours::white.withAlpha(.35f)); g.drawRoundedRectangle(cap.reduced(1.2f), 4.f, 1.f);
        g.setColour(acc); g.fillRoundedRectangle(cap.getCentreX() - 7.f, cap.getCentreY() - 1.2f, 14.f, 2.4f, 1.2f);
    }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override {
        const auto r = juce::Rectangle<float>(0.f, 0.f, float(w), float(h)).reduced(.5f);
        g.setColour(T.deep); g.fillRoundedRectangle(r, 8.f);
        g.setColour(T.peri.withAlpha(.55f)); g.drawRoundedRectangle(r, 8.f, 1.f);
        const float cx = float(w) - 20.f, cy = float(h) * .5f;
        juce::Path chevron;
        chevron.startNewSubPath(cx - 5.f, cy - 2.5f); chevron.lineTo(cx, cy + 2.5f); chevron.lineTo(cx + 5.f, cy - 2.5f);
        g.setColour(T.accent); g.strokePath(chevron, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(2.5f);
        if(bool(b.getProperties()["round"])) r = r.withSizeKeepingCentre(std::min(r.getWidth(), r.getHeight()), std::min(r.getWidth(), r.getHeight()));
        const float corner = r.getHeight() * .5f;
        const bool on = b.getToggleState();
        juce::Colour top, bottom;
        if(on) { top = T.accent.brighter(.25f); bottom = T.accent.darker(.15f); }
        else { top = T.plateTop.withAlpha(1.f).brighter(.12f); bottom = T.plateBottom.withAlpha(1.f).brighter(.04f); }
        if(hover) { top = top.brighter(.1f); bottom = bottom.brighter(.1f); }
        if(down) { top = top.darker(.12f); bottom = bottom.darker(.12f); }
        g.setColour(juce::Colours::black.withAlpha(.1f)); g.fillRoundedRectangle(r.translated(0, 1.2f), corner);
        g.setGradientFill(juce::ColourGradient(top, 0, r.getY(), bottom, 0, r.getBottom(), false));
        g.fillRoundedRectangle(r, corner);
        g.setColour(on ? T.accent.darker(.45f).withAlpha(.8f) : T.peri.withAlpha(.5f)); g.drawRoundedRectangle(r, corner, 1.f);
    }
};

class Panel final : public juce::Component, private juce::Timer {
public:
    explicit Panel(SpectralFoundry& processor) : p(processor) {
        buildChordProfiles();
        audiowide = juce::Typeface::createSystemTypefaceFor(BinaryData::AudiowideRegular_ttf, BinaryData::AudiowideRegular_ttfSize);
        addMouseListener(this, true);
        for(int i = 0; i < kParams; ++i) {
            const bool fader = i < 4;
            const bool top = i >= 15;
            auto& s = sliders[i];
            s.setSliderStyle(fader ? juce::Slider::LinearVertical : juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, fader ? 62 : 90, 20);
            s.getProperties().set("metal", top);
            s.setName(fullNames[i]);
            s.setNumDecimalPlacesToDisplay(i == 8 ? 2 : 1);
            labels[i].setText(shortNames[i], juce::dontSendNotification);
            labels[i].setJustificationType(juce::Justification::centred);
            labels[i].setFont(juce::FontOptions(fader || top ? 10.5f : 11.5f, juce::Font::bold).withKerningFactor(.06f));
            labels[i].setInterceptsMouseClicks(false, false);
            addAndMakeVisible(s); addAndMakeVisible(labels[i]);
            attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state, ids[i], s);
            const juce::String unit = unitOf(i);
            const int decimals = i == 8 ? 2 : 1;
            if(i == 6) s.textFromValueFunction = [this](double v) { return rootText(snapOn() ? snapChordPitch(float(v)) : float(v)); };
            else s.textFromValueFunction = [unit, decimals](double v) { return juce::String(v, decimals) + unit; };
            s.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
            if(auto* prm = p.state.getParameter(ids[i]))
                s.setDoubleClickReturnValue(true, prm->convertFrom0to1(prm->getDefaultValue()));
            s.updateText();
        }
        modeButton.setButtonText("MINOR");
        modeButton.setClickingTogglesState(true);
        addAndMakeVisible(modeButton);
        modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state, "major", modeButton);
        modeButton.onClick = [this] { modeButton.setButtonText(modeButton.getToggleState() ? "MAJOR" : "MINOR"); repaint(); };
        modeButton.setButtonText(p.state.getRawParameterValue("major")->load() > .5f ? "MAJOR" : "MINOR");
        snapButton.setButtonText("SNAP TO NOTES");
        snapButton.setClickingTogglesState(true);
        addAndMakeVisible(snapButton);
        snapAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state, "snap", snapButton);
        snapButton.onClick = [this] { sliders[6].updateText(); repaint(); };
        sliders[6].snapper = [this](double v) { return snapOn() ? double(snapChordPitch(float(v))) : v; };
        const char* tabNames[3] = {"MACROS", "MOTION", "FINISH + MIX"};
        for(int t = 0; t < 3; ++t) {
            tabs[t].setButtonText(tabNames[t]);
            tabs[t].setClickingTogglesState(true);
            tabs[t].setRadioGroupId(7001);
            tabs[t].onClick = [this, t] { setTab(t); };
            addAndMakeVisible(tabs[t]);
        }
        tabs[0].setToggleState(true, juce::dontSendNotification);
        setLookAndFeel(&look); // after the children exist, so their value boxes pick up the colours
        applyTheme(blossomTheme());
        setSize(int(W), int(H));
        setTab(0);
        startTimerHz(30);
    }
    ~Panel() override { stopTimer(); setLookAndFeel(nullptr); }

    void resized() override {
        // Faders: DEPTH / POSITION on the left rail, CUTOFF / RES on the right rail.
        const int fx[4] = {36, 108, 930, 1002};
        for(int i = 0; i < 4; ++i) {
            labels[i].setBounds(fx[i] - 6, 112, 72, 18);
            sliders[i].setBounds(fx[i], 132, 60, 340);
        }
        // Metallic master knobs, top right.
        const int cx[2] = {940, 1030};
        for(int k = 0; k < 2; ++k) {
            const int i = 16 - k;
            labels[i].setBounds(cx[k] - 40, 19, 80, 14);
            sliders[i].setBounds(cx[k] - 42, 33, 84, 62);
        }
        // Just right of the Chord Pitch knob (Macros tab): chord quality above, snap below.
        modeButton.setBounds(748, 566, 130, 26);
        snapButton.setBounds(748, 598, 130, 26);
        for(int t = 0; t < 3; ++t) tabs[t].setBounds(40 + t * 156, 500, 150, 30);
        layoutTab();
    }

    void paint(juce::Graphics& g) override {
        // Chassis.
        g.setGradientFill(juce::ColourGradient(T.bgTop, 0, 0, T.bgBottom, 0, H, false));
        g.fillAll();

        // Header: flat on the body, with the logo.
        juce::Path star;
        star.addStar({50.f, 56.f}, 4, 5.f, 16.f, 0.f);
        g.setColour(T.starA.withAlpha(.25f)); g.fillPath(star, juce::AffineTransform::scale(1.25f, 1.25f, 50.f, 56.f));
        g.setGradientFill(juce::ColourGradient(T.starA, 38, 42, T.starB, 62, 70, false)); g.fillPath(star);
        g.setColour(T.ink); g.setFont(juce::FontOptions(25.f, juce::Font::bold));
        g.drawText("SPECTRAL RESONATOR", 74, 24, 330, 32, juce::Justification::centredLeft);
        {
            juce::AttributedString by;
            by.append("by  ", juce::Font(juce::FontOptions(12.f)), T.muted);
            by.append("SOUND HAVEN", juce::Font(juce::FontOptions(audiowide).withHeight(14.f)), T.magenta);
            by.draw(g, juce::Rectangle<float>(75, 55, 330, 20));
            g.setColour(T.muted.withAlpha(.4f)); g.drawHorizontalLine(66, 212.f, 345.f);
        }

        // Rails.
        plate(g, {22, 104, 150, 374}, 16);
        plate(g, {928, 104, 150, 374}, 16);

        paintDisplay(g);

        // Bottom module: a quiet plate with thin dividers between the control groups.
        const juce::Rectangle<float> mod(22, 490, 1056, 186);
        plate(g, mod, 16);
        {
            const int n = activeTab == 0 ? 4 : activeTab == 1 ? 2 : 5;
            for(int k = 1; k < n; ++k) {
                const float xd = 22.f + 1056.f * float(k) / float(n);
                if(activeTab == 0 && std::abs(xd - 814.f) < 2.f) continue;   // the Minor / Snap buttons sit there
                g.setColour(T.muted.withAlpha(.28f)); g.drawVerticalLine(int(xd), 556.f, 664.f);
            }
        }

        // Footer: hover hint on the left, latency / level on the right.
        g.setColour(T.plateTop.withMultipliedAlpha(.75f)); g.fillRoundedRectangle(16.f, 678.f, W - 32.f, 18.f, 9.f);
        g.setColour(toastTicks > 0 ? T.magenta : hint != -1 ? T.ink : T.muted); g.setFont(juce::FontOptions(11.5f));
        const juce::String text = toastTicks > 0 ? toast : hint == kModeHint ? juce::String("MINOR / MAJOR  -  Minor is the chord table as saved in the rack (minor pentatonic, Em7(11)). Major retunes the same filter down a minor third so it emphasises the major pentatonic (6/9) on the same root note.")
                                : hint == kSnapHint ? juce::String("SNAP TO NOTES  -  Chord Pitch steps between the 12 semitones (G#0 to G1) so the filter's chord root always sits exactly on a note. Off = continuous, as in the original rack.")
                                : hint >= 0 ? juce::String(fullNames[hint]) + "  -  " + tips[hint]
                                            : juce::String("Hover a control for details.  Double-click any knob or fader to reset it.");
        g.drawText(text, 30, 680, 800, 16, juce::Justification::centredLeft, true);
        g.setColour(T.muted); g.setFont(juce::FontOptions(10.f));
        g.drawText("256 SAMPLE LATENCY   |   OUT " + juce::String(juce::Decibels::gainToDecibels(shownOut, -90.0f), 1) + " dB",
                   780, 680, 290, 16, juce::Justification::centredRight);
    }

    void mouseEnter(const juce::MouseEvent& e) override { setHint(hintFor(e.originalComponent)); }
    void mouseMove(const juce::MouseEvent& e) override { setHint(hintFor(e.originalComponent)); }
    void mouseExit(const juce::MouseEvent& e) override { if(hintFor(e.originalComponent) != -1 || e.originalComponent == this) setHint(-1); }

private:
    SpectralFoundry& p;
    FoundryLook look;
    Theme T = blossomTheme();
    juce::Image bgImage;
    int toastTicks = 0;
    juce::String toast;
    std::array<FoundrySlider, kParams> sliders;
    std::array<juce::Label, kParams> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, kParams> attachments;
    juce::TextButton snapButton, modeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> snapAttachment, modeAttachment;
    std::array<juce::TextButton, 3> tabs;
    std::array<std::array<float, kBins>, kRows> hist {};
    std::array<float, kBins> levels {};
    juce::Typeface::Ptr audiowide;
    std::array<std::array<float, 12>, 256> chordProfile {}; // mean dB of each pitch class above the table root, per frame
    int head = 0, tick = 0, activeTab = 0, hint = -1;
    float shownIn = 0, shownOut = 0;
    const juce::Rectangle<float> displayR {184, 104, 732, 374}, wf {214, 150, 672, 246};

    juce::Colour accentFor(int) const { return T.accent; }   // every fader and knob uses the theme's single accent
    void applyTheme(const Theme& t) {
        T = t; look.T = t; look.applyColours();
        for(int i = 0; i < kParams; ++i) {
            sliders[i].setColour(juce::Slider::rotarySliderFillColourId, accentFor(i));
            sliders[i].setColour(juce::Slider::textBoxTextColourId, T.value);
            labels[i].setColour(juce::Label::textColourId, T.muted);
        }
        // Display backdrop, pre-rendered at 2x: base colour, then the theme's picture (cover-fit), then the tint. The
        // same image fills the waterfall ridges, so they hide each other without painting over the picture.
        {
            const auto box = displayR.withZeroOrigin();
            const float bw = box.getWidth() * 2.f, bh = box.getHeight() * 2.f;
            bgImage = juce::Image(juce::Image::ARGB, int(bw), int(bh), true);
            juce::Graphics bg(bgImage);
            bg.fillAll(T.displayBg.withAlpha(1.f));
            if(T.image > 0) {
                const auto photo = juce::ImageFileFormat::loadFrom(BinaryData::blossom_jpg, size_t(BinaryData::blossom_jpgSize));
                if(photo.isValid()) {
                    bg.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
                    const float s = std::max(bw / float(photo.getWidth()), bh / float(photo.getHeight()));
                    const float w = float(photo.getWidth()) * s, h = float(photo.getHeight()) * s;
                    bg.drawImage(photo, juce::Rectangle<float>((bw - w) * .5f, (bh - h) * .5f, w, h));
                }
            }
            bg.fillAll(T.displayBg);
        }
        sendLookAndFeelChange();
        repaint();
    }
    bool snapOn() const { return p.state.getRawParameterValue("snap")->load() > .5f; }
    int hintFor(juce::Component* c) const {
        return c == &snapButton ? kSnapHint : c == &modeButton ? kModeHint : sliderIndexFor(c);
    }
    int sliderIndexFor(juce::Component* c) const {
        for(int i = 0; i < kParams; ++i) if(c == &sliders[i] || sliders[i].isParentOf(c)) return i;
        return -1;
    }
    void setHint(int i) { if(i != hint) { hint = i; repaint(0, 676, int(W), 24); } }
    void setTab(int t) {
        activeTab = t;
        for(int i = 0; i < kParams; ++i) {
            const int ti = tabOf(i);
            const bool vis = ti < 0 || ti == t;
            sliders[i].setVisible(vis); labels[i].setVisible(vis);
        }
        snapButton.setVisible(t == 0); // these belong to the Chord Pitch knob on the Macros tab
        modeButton.setVisible(t == 0);
        layoutTab(); repaint();
    }
    void layoutTab() {
        std::array<int, 5> idx {}; int n = 0;
        for(int i = 0; i < kParams; ++i) if(tabOf(i) == activeTab) idx[size_t(n++)] = i;
        for(int k = 0; k < n; ++k) {
            const int cx = int(22 + 1056.f * float(2 * k + 1) / float(2 * n));
            labels[idx[size_t(k)]].setBounds(cx - 66, 538, 132, 16);
            sliders[idx[size_t(k)]].setBounds(cx - 60, 554, 120, 116);
        }
    }
    void plate(juce::Graphics& g, juce::Rectangle<float> r, float corner) const {
        g.setColour(juce::Colours::black.withAlpha(.07f)); g.fillRoundedRectangle(r.translated(0, 1.5f), corner);
        g.setGradientFill(juce::ColourGradient(T.plateTop, 0, r.getY(), T.plateBottom, 0, r.getBottom(), false));
        g.fillRoundedRectangle(r, corner);
        g.setColour(T.peri.withAlpha(.35f)); g.drawRoundedRectangle(r, corner, 1.f);
        g.setColour(juce::Colours::white.withAlpha(.12f)); g.drawRoundedRectangle(r.reduced(1.5f), corner - 1.5f, 1.f);
    }
    void buildChordProfiles() {
        // Table 3 stores dB per harmonic of its root. Harmonic h lies 12*log2(h) semitones above the root, so
        // averaging the harmonics that fall on a semitone gives each frame's pitch-class emphasis relative to the root.
        std::vector<float> data(256 * 1025);
        std::memcpy(data.data(), BinaryData::table2_bin, sizeof(float) * data.size());
        for(int f = 0; f < 256; ++f) {
            std::array<float, 12> sum {}, count {};
            for(int h = 1; h <= 128; ++h) {
                const double semis = 12.0 * std::log2(double(h));
                if(std::abs(semis - std::round(semis)) > .3) continue;
                const int pc = int(std::lround(semis)) % 12;
                sum[size_t(pc)] += data[size_t(f * 1025 + h)]; count[size_t(pc)] += 1;
            }
            for(size_t k = 0; k < 12; ++k) chordProfile[size_t(f)][k] = count[k] > 0 ? sum[k] / count[k] : -100.f;
        }
    }
    struct ChordInfo { juce::String name, notes; };
    ChordInfo describeChord() const {
        static const char* const names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        const float depth = p.state.getRawParameterValue("chord")->load(), pitchRaw = p.state.getRawParameterValue("pitch")->load();
        const float pitch = snapOn() ? snapChordPitch(pitchRaw) : pitchRaw;
        const float frame = juce::jlimit(0.f, 255.f, depth * 255.f / 127.f);
        const int f0 = int(frame), f1 = std::min(255, f0 + 1);
        std::array<float, 12> prof {};
        for(size_t k = 0; k < 12; ++k)
            prof[k] = chordProfile[size_t(f0)][k] + (chordProfile[size_t(f1)][k] - chordProfile[size_t(f0)][k]) * (frame - float(f0));
        if(p.state.getRawParameterValue("major")->load() > .5f) {
            // The major filter is the minor table retuned down 3 semitones: class k above the root is old class k + 3.
            auto shifted = prof;
            for(size_t k = 0; k < 12; ++k) shifted[k] = prof[(k + 3) % 12];
            prof = shifted;
        }
        const float top = *std::max_element(prof.begin(), prof.end());
        std::array<float, 12> sorted = prof; std::sort(sorted.begin(), sorted.end());
        const float median = (sorted[5] + sorted[6]) * .5f;
        std::array<bool, 12> has {};
        int tones = 0;
        for(size_t k = 0; k < 12; ++k) { has[k] = prof[k] >= top - 10.f; tones += has[k] ? 1 : 0; }
        const auto root = rootNoteFor(pitch);
        const juce::String rootName = names[((root.midi % 12) + 12) % 12];
        if(tones > 8 || top - median < 3.f) return {"FLAT  (no chord at this depth)", {}};
        juce::String q;
        const bool minor = has[3], major = has[4];
        if(minor) q = "m"; else if(!major) q = has[2] ? "sus2" : has[5] ? "sus4" : "5";
        if(has[6] && !has[7] && minor) q = "dim";
        if(has[11]) q += "maj7"; else if(has[10]) q += "7";
        juce::String ext;
        const bool sixth = (minor || major) && has[9] && !has[10] && !has[11];
        if(sixth) q += has[2] ? "6/9" : "6";
        if(has[2] && (minor || major) && !(sixth && has[2])) ext += "9";
        if(has[5] && (minor || major)) ext += (ext.isEmpty() ? "" : ",") + juce::String("11");
        juce::String notes;
        for(int k = 0; k < 12; ++k)
            if(has[size_t(k)] || k == 0) notes += juce::String(names[(((root.midi + k) % 12) + 12) % 12]) + " ";
        return {rootName + q + (ext.isEmpty() ? "" : "(" + ext + ")"), notes.trim()};
    }
    static float xForFreq(const juce::Rectangle<float>& a, float f) { return a.getX() + a.getWidth() * std::log10(f / 20.f) / 3.f; }

    void paintDisplay(juce::Graphics& g) {
        const auto a = wf;
        const juce::FillType backdropFill(bgImage, juce::AffineTransform::scale(.5f).translated(displayR.getX(), displayR.getY()));
        g.setFillType(backdropFill); g.fillRoundedRectangle(displayR, 18);
        g.setGradientFill(juce::ColourGradient(T.glow, a.getCentreX(), a.getCentreY(), juce::Colours::transparentBlack,
                                               a.getX(), a.getBottom() + 40, true));
        g.fillRoundedRectangle(displayR.reduced(2), 16);
        g.setColour(T.peri.withAlpha(.35f)); g.drawRoundedRectangle(displayR, 18, 1.4f);

        // Frequency grid.
        g.setFont(juce::FontOptions(9.5f));
        const float marks[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
        for(float f : marks) {
            const float x = xForFreq(a, f);
            g.setColour(T.peri.withAlpha(.16f)); g.drawVerticalLine(int(x), a.getY() - 8, a.getBottom() + 4);
            if(f == 100 || f == 1000 || f == 10000) {
                g.setColour(T.mutedOnDark); g.drawText(f >= 1000 ? juce::String(int(f / 1000)) + "k" : juce::String(int(f)), int(x) - 16, int(a.getBottom()) + 6, 32, 12, juce::Justification::centred);
            }
        }

        // Perspective floor under the waterfall.
        const auto rowX0 = [&](float t) { return a.getX() + t * 80.f; };
        const auto rowW = [&](float t) { return a.getWidth() * (1.f - .12f * t); };
        const auto rowY = [&](float t) { return a.getBottom() - t * 64.f; };
        {
            juce::Path floor;
            floor.startNewSubPath(rowX0(0), rowY(0)); floor.lineTo(rowX0(0) + rowW(0), rowY(0));
            floor.lineTo(rowX0(1) + rowW(1), rowY(1)); floor.lineTo(rowX0(1), rowY(1)); floor.closeSubPath();
            g.setColour(T.peri.withAlpha(.07f)); g.fillPath(floor);
            g.setColour(T.peri.withAlpha(.22f)); g.strokePath(floor, juce::PathStrokeType(1.f));
            for(int k = 1; k < 4; ++k) {
                const float t = float(k) / 4.f;
                g.setColour(T.peri.withAlpha(.1f)); g.drawLine(rowX0(t), rowY(t), rowX0(t) + rowW(t), rowY(t), 1.f);
            }
        }

        // 3D waterfall of recent spectra: smooth ridges drawn back to front, each one filled so it hides the ones behind.
        const auto rainbow = [&](float x0, float x1, float alpha) {
            juce::ColourGradient grad(T.ramp[0], x0, 0, T.ramp[6], x1, 0, false);
            const double stops[5] = {.2, .38, .55, .74, .9};
            for(int s = 0; s < 5; ++s) grad.addColour(stops[s], T.ramp[size_t(s + 1)]);
            grad.multiplyOpacity(alpha);
            return grad;
        };
        for(int r = kRows - 1; r >= 0; --r) {
            const auto& raw = hist[size_t((head - r + kRows) % kRows)];
            const float t = float(r) / float(kRows - 1);
            const float x0 = rowX0(t), x1 = x0 + rowW(t), baseY = rowY(t), amp = a.getHeight() * .78f * (1.f - .28f * t);
            std::array<float, kBins> v {};
            for(int i = 0; i < kBins; ++i) {
                const float l = raw[size_t(std::max(0, i - 1))], m = raw[size_t(i)], rt = raw[size_t(std::min(kBins - 1, i + 1))];
                v[size_t(i)] = (l + 2 * m + rt) * .25f;
            }
            juce::Path line;
            std::array<juce::Point<float>, kBins> pts {};
            for(int i = 0; i < kBins; ++i)
                pts[size_t(i)] = {x0 + (x1 - x0) * float(i) / float(kBins - 1), baseY - std::pow(v[size_t(i)], 1.15f) * amp};
            line.startNewSubPath(pts[0]);
            for(int i = 1; i < kBins - 1; ++i) line.quadraticTo(pts[size_t(i)], (pts[size_t(i)] + pts[size_t(i + 1)]) * .5f);
            line.lineTo(pts[kBins - 1]);
            juce::Path area(line);
            area.lineTo(x1, baseY); area.lineTo(x0, baseY); area.closeSubPath();
            g.setFillType(backdropFill); g.fillPath(area);
            g.setGradientFill(rainbow(x0, x1, .26f * (1.f - .75f * t))); g.fillPath(area);
            if(r == 0) { g.setGradientFill(rainbow(x0, x1, .3f)); g.strokePath(line, juce::PathStrokeType(7.f)); }
            g.setGradientFill(rainbow(x0, x1, 1.f - .7f * t));
            g.strokePath(line, juce::PathStrokeType(r == 0 ? 2.f : 1.2f));
        }

        // Filter response: smoothed, thin white-cyan line so it reads as an overlay, not as part of the spectrum.
        {
            const float top = a.getY() + 6, bottom = a.getBottom();
            std::array<float, kBins> s {}, s2 {};
            for(int i = 0; i < kBins; ++i) s[size_t(i)] = p.response[size_t(i)].load(std::memory_order_relaxed);
            for(int pass = 0; pass < 2; ++pass) {
                for(int i = 0; i < kBins; ++i) {
                    const float l = s[size_t(std::max(0, i - 1))], rt = s[size_t(std::min(kBins - 1, i + 1))];
                    s2[size_t(i)] = (l + 2 * s[size_t(i)] + rt) * .25f;
                }
                s = s2;
            }
            juce::Path curve;
            std::array<juce::Point<float>, kBins> pts {};
            for(int i = 0; i < kBins; ++i)
                pts[size_t(i)] = {a.getX() + a.getWidth() * float(i) / float(kBins - 1), bottom - s[size_t(i)] * (bottom - top)};
            curve.startNewSubPath(pts[0]);
            for(int i = 1; i < kBins - 1; ++i) curve.quadraticTo(pts[size_t(i)], (pts[size_t(i)] + pts[size_t(i + 1)]) * .5f);
            curve.lineTo(pts[kBins - 1]);
            const float zeroY = bottom - (60.f / 90.f) * (bottom - top);
            g.setColour(T.curve.withAlpha(.12f)); g.drawHorizontalLine(int(zeroY), a.getX(), a.getRight());
            g.setColour(T.curve.withAlpha(.14f)); g.strokePath(curve, juce::PathStrokeType(4.f));
            g.setColour(T.curve.withAlpha(.92f)); g.strokePath(curve, juce::PathStrokeType(1.4f));
        }

        {
            const auto chord = describeChord();
            juce::AttributedString as;
            as.setJustification(juce::Justification::centredRight);
            as.append("CHORD  ", juce::Font(juce::FontOptions(10.f, juce::Font::bold)), T.mutedOnDark);
            as.append(chord.name, juce::Font(juce::FontOptions(14.f, juce::Font::bold)), T.chordName);
            if(chord.notes.isNotEmpty()) as.append("    " + chord.notes, juce::Font(juce::FontOptions(12.f, juce::Font::bold)), T.inkOnDark);
            as.draw(g, juce::Rectangle<float>(500, 109, 386, 22));
        }

        paintMeter(g, 194.f, shownIn, "IN");
        paintMeter(g, 898.f, shownOut, "OUT");

        // Table position indicators.
        const char* tn[3] = {"TABLE 1  POSITION", "TABLE 2  WUMP", "TABLE 3  CHORD"};
        const juce::Colour tc[3] = {T.accent, T.accent, T.accent};
        for(int k = 0; k < 3; ++k) {
            const float x = 214.f + float(k) * 228.f, v = p.tablePos[size_t(k)].load(std::memory_order_relaxed);
            g.setColour(T.mutedOnDark); g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.drawText(tn[k], int(x), 436, 150, 12, juce::Justification::centredLeft);
            g.setColour(T.inkOnDark); g.drawText(juce::String(int(std::round(v * 255.f))), int(x) + 150, 436, 52, 12, juce::Justification::centredRight);
            const juce::Rectangle<float> track(x, 454.f, 202.f, 7.f);
            g.setColour(T.displayBg.withAlpha(1.f).contrasting(.12f)); g.fillRoundedRectangle(track, 3.5f);
            g.setColour(tc[k]); g.fillRoundedRectangle(track.withWidth(std::max(7.f, track.getWidth() * v)), 3.5f);
            const juce::Rectangle<float> thumb(track.getX() + track.getWidth() * v - 5.f, track.getY() - 3.5f, 10.f, 14.f);
            g.setGradientFill(juce::ColourGradient(T.metal.brighter(.15f), 0, thumb.getY(), T.metal.interpolatedWith(T.night, .45f), 0, thumb.getBottom(), false));
            g.fillRoundedRectangle(thumb, 3.f);
            g.setColour(juce::Colours::black.withAlpha(.45f)); g.drawRoundedRectangle(thumb, 3.f, 1.f);
        }
    }
    void paintMeter(juce::Graphics& g, float x, float level, const char* name) {
        const float top = 128.f, bottom = 418.f;
        const int segs = 24;
        const float db = juce::Decibels::gainToDecibels(level, -60.f);
        const float lit = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f) * segs;
        const float segH = (bottom - top) / float(segs);
        for(int s = 0; s < segs; ++s) {
            const float y = bottom - float(s + 1) * segH;
            const float pos = float(s) / float(segs - 1);
            const juce::Colour c = pos < .85f ? T.accent : T.accent.brighter(.35f);
            const bool on = float(s) < lit;
            g.setColour(on ? c : T.displayBg.withAlpha(1.f).contrasting(.12f)); g.fillRoundedRectangle(x, y + 1, 8, segH - 2, 2);
        }
        g.setColour(T.mutedOnDark); g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        g.drawText(name, int(x) - 10, int(bottom) + 3, 28, 10, juce::Justification::centred);
    }
    void timerCallback() override {
        for(int i = 0; i < kBins; ++i) levels[size_t(i)] = std::max(p.display[size_t(i)].load(), levels[size_t(i)] * .92f);
        if(++tick % 2 == 0) { head = (head + 1) % kRows; hist[size_t(head)] = levels; }
        else hist[size_t(head)] = levels;
        shownIn = std::max(p.inMeter.load(), shownIn * .88f);
        shownOut = std::max(p.meter.load(), shownOut * .88f);
        if(toastTicks > 0 && --toastTicks == 0) repaint(0, 676, int(W), 24);
        repaint(displayR.expanded(4).toNearestInt());
        repaint(760, 676, 330, 24);
    }
};

class Editor final : public juce::AudioProcessorEditor {
public:
    explicit Editor(SpectralFoundry& processor) : AudioProcessorEditor(processor), panel(processor) {
        addAndMakeVisible(panel);
        setResizable(true, true);
        setResizeLimits(660, 420, 2200, 1400);
        getConstrainer()->setFixedAspectRatio(double(W) / double(H));
        setSize(int(W), int(H));
    }
    void resized() override {
        const float s = std::min(float(getWidth()) / W, float(getHeight()) / H);
        panel.setBounds(0, 0, int(W), int(H));
        panel.setTransform(juce::AffineTransform::scale(s));
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0d0b22)); }
private:
    Panel panel;
};
}
juce::AudioProcessorEditor* SpectralFoundry::createEditor() { return new Editor(*this); }

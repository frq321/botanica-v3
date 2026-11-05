#pragma once
#include "BotanicaProcessor.h"

namespace botui
{
inline juce::Colour bg()      { return juce::Colour (0xff09160f); }
inline juce::Colour panel()   { return juce::Colour (0xff10261d); }
inline juce::Colour panelHi() { return juce::Colour (0xff17372a); }
inline juce::Colour track()   { return juce::Colour (0xff24473a); }
inline juce::Colour text()    { return juce::Colour (0xffe8f4ea); }
inline juce::Colour dim()     { return juce::Colour (0xff8aab9b); }
inline juce::Colour cream()   { return juce::Colour (0xfff3e2b0); }   // piano
inline juce::Colour lilac()   { return juce::Colour (0xffb9a4ff); }   // pad
inline juce::Colour aqua()    { return juce::Colour (0xff54e6d3); }   // bubbles
inline juce::Colour leaf()    { return juce::Colour (0xff9be57c); }   // nature
inline juce::Colour amber()   { return juce::Colour (0xffffb877); }   // granulizer
inline juce::Colour sky()     { return juce::Colour (0xff86c5ff); }   // reverb
inline juce::Colour rose()    { return juce::Colour (0xffff93b4); }   // macro
}

struct BotanicaLook : public juce::LookAndFeel_V4
{
    BotanicaLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& title, juce::Colour);
    void resized() override;
    void setTitle (const juce::String& t) { name.setText (t, juce::dontSendNotification); }

    juce::Slider slider;
private:
    juce::Label name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

//==============================================================================
// 16 pattern steps: click the top to switch a step on/off, drag the bottom up/down to set its scale degree.
class StepGrid : public juce::Component, private juce::Timer
{
public:
    StepGrid (BotanicaProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void timerCallback() override;
    int cellAt (juce::Point<float>) const;
    juce::Rectangle<float> cellBounds (int i) const;

    BotanicaProcessor& proc;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 16> onAtt, degAtt;
    std::array<bool, 16> on {};
    std::array<int, 16> deg {};
    int dragCell = -1, dragStartDeg = 0, current = -1;
    float dragStartY = 0;
};

//==============================================================================
// The "pond": bubbles rise, notes bloom, grain bursts sparkle - driven by what the engine really plays.
class Garden : public juce::Component, private juce::Timer
{
public:
    explicit Garden (BotanicaProcessor&);
    void paint (juce::Graphics&) override;
    void tickNow() { timerCallback(); }          // used by the snapshot tool (no message loop there)
private:
    void timerCallback() override;
    struct Blob { uint8_t kind; float x, y, size, age, life, vy; };

    BotanicaProcessor& proc;
    uint32_t readPos = 0;
    std::vector<Blob> blobs;
    juce::Random rnd;
    float time = 0;
};

//==============================================================================
class BotanicaPanel : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit BotanicaPanel (BotanicaProcessor&);
    ~BotanicaPanel() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void debugTick() { garden.tickNow(); }

    // drag and drop of sound files
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    Knob* addKnob (const juce::String& id, const juce::String& title, juce::Colour c);
    void addToggle (juce::ToggleButton&, const juce::String& id, const juce::String& text, juce::Colour c);
    void layoutKnobs (juce::Rectangle<int> area, const std::vector<Knob*>& ks, int cols, int cellH);

    // ---- sounds + presets
    struct Hit { bool valid = false; BotanicaProcessor::DropTarget target = BotanicaProcessor::DropTarget::piano; juce::Rectangle<int> area; juce::String label; };
    Hit hitAt (juce::Point<int>) const;
    void timerCallback() override;
    void refreshSounds();
    void refreshPresets();
    void stepPreset (int dir);
    void savePresetDialog();
    void chooseFileFor (BotanicaProcessor::DropTarget);
    void showSoundsMenu();

    juce::ComboBox pnSoundBox, pdSoundBox, presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" }, delBtn { "Delete" }, soundsBtn { "Sounds..." }, rollBtn { "Roll" };
    std::unique_ptr<juce::ParameterAttachment> pnSrcAtt, pdSrcAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Label statusLabel;
    juce::String lastSig, flash;
    double flashUntil = 0;
    Hit hover;
    juce::Rectangle<int> pianoRect, padRect, sceneRect, smearHintArea;

    BotanicaProcessor& proc;
    BotanicaLook look;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Knob*> kPiano, kPad, kBubble, kScene, kSpace, kSmear, kGrain, kMacro;

    juce::ComboBox rootBox, scaleBox, rateBox, modeBox, arRateBox, patBox;
    juce::ToggleButton snapBtn, bbKeyBtn, ambBtn, sceneKeysBtn, patternBtn, smearBtn, freezeBtn;
    std::unique_ptr<ComboAtt> rootA, scaleA, rateA, modeA, arRateA, patA;
    std::vector<std::unique_ptr<ButtonAtt>> buttonAtts;

    StepGrid steps;
    Garden garden;

    struct Panel { juce::String title; juce::Colour colour; juce::Rectangle<int> r; };
    std::vector<Panel> panels;
    juce::Rectangle<int> keyLabelArea, scaleLabelArea, rateLabelArea, stepsLabelArea, modeLabelArea, arRateLabelArea, patLabelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BotanicaPanel)
};

//==============================================================================
// The plugin window: the 1100 x 1164 panel, scaled to whatever size the user drags the window to.
class BotanicaEditor : public juce::AudioProcessorEditor
{
public:
    explicit BotanicaEditor (BotanicaProcessor&);
    void resized() override;
    void debugTick() { panel.debugTick(); }

private:
    static constexpr int kBaseW = 1100, kBaseH = 1164;
    BotanicaPanel panel;
    juce::ComponentBoundsConstrainer limits;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BotanicaEditor)
};

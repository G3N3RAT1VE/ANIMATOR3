#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace riq
{

namespace Colours
{
    const juce::Colour bg { 0xff15171c }, panel { 0xff1e2128 }, panelHi { 0xff272b34 }, line { 0xff353a45 },
                       text { 0xffd8dce4 }, dim { 0xff7d8594 }, accent { 0xffff6a1a }, live { 0xff3ddc84 },
                       frozen { 0xff4aa8ff }, armed { 0xffffd23f };
    const juce::Colour stem[3] { juce::Colour (0xffdfe3ea), juce::Colour (0xff6a5cff), juce::Colour (0xff2fc4b2) };
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
};

// Rotary knob with a caption, bound to a parameter.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption,
          juce::Colour colour = Colours::accent, bool captionOnTop = true);
    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String caption;
    bool captionOnTop;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Choice parameter as a combo box.
class Choice : public juce::ComboBox
{
public:
    Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// Choice parameter as a row of segment buttons (like Rhythmiq's 1/4 | 1/8 | 1/16 selectors).
class Segmented : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& paramId);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray items;
    int selected = 0;
    juce::ParameterAttachment attachment;
};

// Toggle parameter as a latching button.
class Toggle : public juce::TextButton
{
public:
    Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text,
            juce::Colour onColour = Colours::accent);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// Bool parameter that is true only while the button is held.
class Momentary : public juce::TextButton
{
public:
    Momentary (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text);
    void buttonStateChanged() override;

private:
    juce::RangedAudioParameter* param = nullptr;
    bool down = false;
};

// Waveform of what a stem currently plays (or the sum of all stems when stemIndex < 0).
class WaveView : public juce::Component
{
public:
    WaveView (RiqLiveProcessor& p, int stemIndex) : proc (p), stem (stemIndex) {}
    void paint (juce::Graphics&) override;

private:
    RiqLiveProcessor& proc;
    int stem;
    std::vector<float> columnPeak;
    std::vector<bool> columnAltered;
    float displayGain = 0.0f;
};

// Master display: summed waveform, bar numbers and the keyboard-fill key strip.
class MasterDisplay : public juce::Component
{
public:
    explicit MasterDisplay (RiqLiveProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RiqLiveProcessor& proc;
    Segmented fillSize, loopLen;
    WaveView wave;
};

class StemColumn : public juce::Component
{
public:
    StemColumn (RiqLiveProcessor& p, int stemIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    RiqLiveProcessor& proc;
    int stem;
    Choice source;
    Segmented slice;
    WaveView wave;
    Momentary hold;
    Toggle capture;
    Knob arrange, reverse, silence, ratchet, pan, gain;
    Momentary evolveArr, evolveRev;
    Toggle on, solo;
    Segmented out, rzone;
    int lastEvolveCount = 0;
    juce::uint32 flashUntil = 0;
};

class SendPanel : public juce::Component
{
public:
    explicit SendPanel (RiqLiveProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RiqLiveProcessor& proc;
    Segmented slot;
    Toggle thru;
};

} // namespace riq

class RiqLiveEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RiqLiveEditor (RiqLiveProcessor&);
    ~RiqLiveEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateSceneButtons();
    juce::Rectangle<int> assistantArea() const;
    juce::Rectangle<int> autopilotArea() const;

    RiqLiveProcessor& proc;
    riq::LookAndFeel lnf;

    riq::Choice protect, freezeQ, sceneQ;
    riq::Knob masterGain;
    riq::Toggle limiter;
    riq::MasterDisplay master;
    riq::Knob iqArrange;
    riq::Toggle iqEvolve, iqReverse;
    riq::Segmented apPhrase, apEvolveEvery;
    riq::Knob apFill, apMotion, apRatchet, apEvolveChance, apRevDepth, apDrop;
    std::array<std::unique_ptr<riq::Toggle>, riq::kNumStems> links;
    std::array<std::unique_ptr<riq::StemColumn>, riq::kNumStems> columns;
    std::array<juce::TextButton, riq::kNumScenes> sceneButtons;
    juce::TextButton saveButton { "SAVE" };
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiqLiveEditor)
};

// Editor of the RiQ Live Send effect.
class RiqSendEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RiqSendEditor (RiqLiveProcessor&);
    ~RiqSendEditor() override;
    void resized() override { panel.setBounds (getLocalBounds()); }

private:
    void timerCallback() override { panel.repaint(); }

    riq::LookAndFeel lnf;
    riq::SendPanel panel;
};

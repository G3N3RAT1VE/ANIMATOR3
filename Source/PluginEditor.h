#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace riq
{

namespace Colours
{
    const juce::Colour bg { 0xff070807 }, panel { 0xff0e110e }, panelHi { 0xff172017 }, line { 0xff243024 },
                       text { 0xffe2f5da }, dim { 0xff6a8565 }, accent { 0xffd4ff1e }, edit { 0xffff2bd6 },
                       live { 0xff39ff88 }, frozen { 0xff00c8ff }, armed { 0xffffd000 }, warn { 0xffff2e63 },
                       wave { 0xff040504 };
    const juce::Colour stem[3] { juce::Colour (0xff9dff2e), juce::Colour (0xffb45cff), juce::Colour (0xff19e3ff) };
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

// Rotary knob with a caption, bound to a parameter.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption,
          juce::Colour colour = Colours::accent);
    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String caption;
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

// Choice parameter as a row of segment buttons.
class Segmented : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, juce::Colour colour = Colours::accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray items;
    int selected = 0;
    juce::Colour colour;
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

// Button that fires on press (not on release) - used for Evolve.
class PressButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onPress;
    void buttonStateChanged() override
    {
        const bool isDown = getState() == juce::Button::buttonDown;
        if (isDown && ! down && onPress != nullptr)
            onPress();
        down = isDown;
    }

private:
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

// Autopilot plan for the current phrase: one row per stem, one cell per bar.
class PhraseMap : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PhraseMap (RiqLiveProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    RiqLiveProcessor& proc;
};

// Top deck: IQ controls, phrase map, live meters and the autopilot settings.
class AutopilotDeck : public juce::Component
{
public:
    explicit AutopilotDeck (RiqLiveProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

    Knob iqArrange;

private:
    RiqLiveProcessor& proc;
    Toggle iqEvolve, iqReverse;
    std::array<std::unique_ptr<Toggle>, kNumStems> links;
    PhraseMap phraseMap;
    Segmented apPhrase, apEvolveEvery;
    Knob apFill, apMotion, apRatchet, apEvolveChance, apRevDepth, apDrop;
    juce::Rectangle<int> metersArea;
};

// Master strip: loop / fill / quantise settings, summed waveform and the keyboard-fill keys.
class MasterDisplay : public juce::Component
{
public:
    explicit MasterDisplay (RiqLiveProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RiqLiveProcessor& proc;
    Segmented fillSize, loopLen;
    Choice freezeQ, sceneQ;
    WaveView wave;
};

// Preset browser: whole-plugin presets stored as files in Documents/AUTOPILOT/Presets.
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (RiqLiveProcessor& p);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();

private:
    void showMenu();
    void step (int delta);
    void askName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk);
    void confirmDelete();

    RiqLiveProcessor& proc;
    juce::TextButton prev { "<" }, next { ">" }, name, save { "SAVE" }, del { "DEL" };
    juce::String shown;
};

// One horizontal lane per stem.
class StemLane : public juce::Component
{
public:
    StemLane (RiqLiveProcessor& p, int stemIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    RiqLiveProcessor& proc;
    int stem;
    Choice source, keep;
    Segmented slice;
    WaveView wave;
    Momentary hold;
    Toggle capture, autoLink;
    Knob arrange, reverse, silence, ratchet, pan, gain;
    PressButton evolveArr { "EVOLVE" }, evolveRev { "EVOLVE" };
    Segmented rzone;
    Toggle on, solo;
    Segmented out;
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
    juce::Rectangle<int> meterArea() const;

    RiqLiveProcessor& proc;
    riq::LookAndFeel lnf;

    riq::PresetBar presetBar;
    riq::Choice protect;
    riq::Segmented style;
    riq::Knob masterGain, limDrive, limCeiling;
    riq::Toggle limiter;
    riq::AutopilotDeck deck;
    riq::MasterDisplay master;
    std::array<std::unique_ptr<riq::StemLane>, riq::kNumStems> lanes;
    std::array<juce::TextButton, riq::kNumScenes> sceneButtons;
    juce::TextButton saveButton { "SAVE" };
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiqLiveEditor)
};

// Editor of the AUTOPILOT Send effect.
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

/*
  ==============================================================================
    Superalloy — PluginEditor.h
    A medium heat-tinted Inconel rectangle with one dropdown ("Metal") and one
    two-position switch ("Contour": Subtle / Color).
  ==============================================================================
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class SuperalloyLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SuperalloyLookAndFeel();
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
};

//==============================================================================
// Two-segment switch bound to the "contour" choice parameter.
class ContourSwitch final : public juce::Component
{
public:
    explicit ContourSwitch (juce::RangedAudioParameter& param);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void select (int index);

    juce::ParameterAttachment attachment;
    int current = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContourSwitch)
};

//==============================================================================
class SuperalloyAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SuperalloyAudioProcessorEditor (SuperalloyAudioProcessor&);
    ~SuperalloyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 560, kHeight = 300;

private:
    SuperalloyAudioProcessor& processorRef;
    SuperalloyLookAndFeel lnf;
    juce::Image background;
    juce::ComboBox metalBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    ContourSwitch contour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperalloyAudioProcessorEditor)
};

/*
  ==============================================================================
    Superalloy — PluginEditor.h
    A medium heat-tinted Inconel rectangle with one dropdown: "Metal".
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

class SuperalloyAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SuperalloyAudioProcessorEditor (SuperalloyAudioProcessor&);
    ~SuperalloyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 560, kHeight = 300;

private:
    SuperalloyAudioProcessor& processor630;
    SuperalloyLookAndFeel lnf;
    juce::Image background;
    juce::ComboBox metalBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperalloyAudioProcessorEditor)
};

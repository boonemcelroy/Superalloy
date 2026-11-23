/*
  ==============================================================================
    Superalloy — PluginEditor.h
    A medium heat-tinted Inconel rectangle with one dropdown ("Metal"), a
    four-position switch ("Contour": Subtle / Color / Curie / Geiger) and, top
    right, a two-position "Bypass" switch (Off / On) in the same style.
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
// Segmented switch bound to a stepped parameter (a choice, or a bool for two
// segments). Segment i, counted from the left, selects value i.
class SegmentSwitch final : public juce::Component
{
public:
    SegmentSwitch (juce::RangedAudioParameter& param, juce::StringArray segmentLabels,
                   const juce::String& title, const juce::String& description);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void select (int index);
    juce::Rectangle<float> segmentBounds (int index) const;

    juce::ParameterAttachment attachment;
    juce::StringArray labels;
    int current = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentSwitch)
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
    SegmentSwitch contour;
    SegmentSwitch bypass;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperalloyAudioProcessorEditor)
};

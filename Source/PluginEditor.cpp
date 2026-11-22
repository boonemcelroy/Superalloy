/*
  ==============================================================================
    Superalloy — PluginEditor.cpp
  ==============================================================================
*/
#include "PluginEditor.h"
#include "InconelTexture.h"

//==============================================================================
SuperalloyLookAndFeel::SuperalloyLookAndFeel()
{
    setColour (juce::ComboBox::textColourId,              juce::Colour (0xfff4efe6));
    setColour (juce::ComboBox::arrowColourId,             juce::Colour (0xffe8dcc4));
    setColour (juce::PopupMenu::backgroundColourId,       juce::Colour (0xf01b1d22));
    setColour (juce::PopupMenu::textColourId,             juce::Colour (0xffe9e4da));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff6a4a7a));
    setColour (juce::PopupMenu::highlightedTextColourId,  juce::Colours::white);
}

void SuperalloyLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                          int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (juce::Colour (0xb0121418));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (0x60ffffff));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    // arrow
    const float ax = (float) width - 22.0f, ay = (float) height * 0.5f;
    juce::Path p;
    p.addTriangle (ax - 5.0f, ay - 2.5f, ax + 5.0f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (p);
}

juce::Font SuperalloyLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (16.0f));
}

void SuperalloyLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (12, 1, box.getWidth() - 40, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font SuperalloyLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (15.0f));
}

//==============================================================================
SuperalloyAudioProcessorEditor::SuperalloyAudioProcessorEditor (SuperalloyAudioProcessor& p)
    : AudioProcessorEditor (&p), processor630 (p)
{
    setLookAndFeel (&lnf);
    background = superalloy::texture::renderHeatTintedInconel (kWidth, kHeight);

    metalBox.addItemList (SuperalloyAudioProcessor::metalNames(), 1);
    metalBox.setTooltip ("Metal");
    addAndMakeVisible (metalBox);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor630.apvts, "metal", metalBox);

    setResizable (false, false);
    setSize (kWidth, kHeight);
}

SuperalloyAudioProcessorEditor::~SuperalloyAudioProcessorEditor()
{
    attachment.reset();
    setLookAndFeel (nullptr);
}

void SuperalloyAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (background, 0, 0);

    // dropdown title: "Metal"
    const auto box = metalBox.getBounds();
    g.setFont (juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.25f));
    g.setColour (juce::Colour (0xd0101114));
    g.drawText ("METAL", box.getX(), box.getY() - 22, box.getWidth(), 18, juce::Justification::centredLeft);

    // machined edge
    g.setColour (juce::Colour (0x40000000));
    g.drawRect (getLocalBounds(), 1);
}

void SuperalloyAudioProcessorEditor::resized()
{
    metalBox.setBounds (getLocalBounds().withSizeKeepingCentre (380, 38).translated (0, 10));
}

/*
  ==============================================================================
    Superalloy — PluginEditor.cpp
  ==============================================================================
*/
#include "PluginEditor.h"
#include "InconelTexture.h"

namespace
{
    const juce::Colour kPanel        (0xb0121418);
    const juce::Colour kPanelEdge    (0x60ffffff);
    const juce::Colour kEngraving    (0xd0101114);
    const juce::Colour kTextBright   (0xfff4efe6);
    const juce::Colour kTextDim      (0xffb9b0a2);
    const juce::Colour kTemperLight  (0xff8a6a3c);   // straw-bronze heat tint
    const juce::Colour kTemperDark   (0xff5a3f6e);   // violet heat tint
}

//==============================================================================
SuperalloyLookAndFeel::SuperalloyLookAndFeel()
{
    setColour (juce::ComboBox::textColourId,              kTextBright);
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
    g.setColour (kPanel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (kPanelEdge);
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
SegmentSwitch::SegmentSwitch (juce::RangedAudioParameter& param, juce::StringArray segmentLabels,
                              const juce::String& title, const juce::String& description)
    : attachment (param, [this] (float v) { current = juce::jlimit (0, labels.size() - 1, (int) std::lround (v)); repaint(); }),
      labels (std::move (segmentLabels))
{
    jassert (labels.size() >= 2);
    setTitle (title);
    setDescription (description);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

void SegmentSwitch::select (int index)
{
    index = juce::jlimit (0, labels.size() - 1, index);
    if (index != current)
        attachment.setValueAsCompleteGesture ((float) index);
}

juce::Rectangle<float> SegmentSwitch::segmentBounds (int index) const
{
    const auto inner = getLocalBounds().toFloat().reduced (3.5f);
    const float w = inner.getWidth() / (float) labels.size();
    return { inner.getX() + w * (float) index, inner.getY(), w, inner.getHeight() };
}

void SegmentSwitch::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()))
        select (juce::jlimit (0, labels.size() - 1, (int) ((float) e.x * (float) labels.size() / (float) getWidth())));
}

bool SegmentSwitch::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::leftKey)  { select (current - 1); return true; }
    if (k == juce::KeyPress::rightKey) { select (current + 1); return true; }
    if (k == juce::KeyPress::homeKey)  { select (0); return true; }
    if (k == juce::KeyPress::endKey)   { select (labels.size() - 1); return true; }
    if (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) { select ((current + 1) % labels.size()); return true; }
    return false;
}

void SegmentSwitch::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;

    g.setColour (kPanel);
    g.fillRoundedRectangle (r, radius);

    // selected segment: a heat-tinted inlay
    const auto seg = segmentBounds (current);
    juce::ColourGradient grad (kTemperLight.withAlpha (0.92f), seg.getX(), seg.getY(),
                               kTemperDark.withAlpha (0.92f), seg.getRight(), seg.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
    g.setColour (juce::Colour (0x40ffffff));
    g.drawRoundedRectangle (seg.reduced (0.5f), seg.getHeight() * 0.5f, 1.0f);

    g.setColour (kPanelEdge);
    g.drawRoundedRectangle (r, radius, 1.0f);
    if (hasKeyboardFocus (false))
    {
        g.setColour (juce::Colour (0x80e8dcc4));
        g.drawRoundedRectangle (r.expanded (2.0f), radius + 2.0f, 1.0f);
    }

    g.setFont (juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.22f));
    for (int i = 0; i < labels.size(); ++i)
    {
        g.setColour (i == current ? kTextBright : kTextDim);
        g.drawText (labels[i], segmentBounds (i).toNearestInt(), juce::Justification::centred);
    }
}

//==============================================================================
SuperalloyAudioProcessorEditor::SuperalloyAudioProcessorEditor (SuperalloyAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p),
      contour (*p.apvts.getParameter ("contour"), { "SUBTLE", "COLOR", "CURIE", "GEIGER" }, "Contour",
               "Intensity: Subtle, Color (4x), Curie (12x), Geiger (48x)"),
      bypass  (*p.apvts.getParameter ("bypass"),  { "OFF", "ON" }, "Bypass", "True bypass: On passes the input through untouched")
{
    setLookAndFeel (&lnf);
    background = superalloy::texture::renderHeatTintedInconel (kWidth, kHeight);

    metalBox.addItemList (SuperalloyAudioProcessor::metalNames(), 1);
    metalBox.setTooltip ("Metal");
    addAndMakeVisible (metalBox);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processorRef.apvts, "metal", metalBox);

    addAndMakeVisible (contour);
    addAndMakeVisible (bypass);

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

    // engraved titles
    g.setFont (juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.25f));
    g.setColour (kEngraving);
    const auto box = metalBox.getBounds();
    g.drawText ("METAL", box.getX(), box.getY() - 22, box.getWidth(), 18, juce::Justification::centredLeft);
    const auto sw = contour.getBounds();
    g.drawText ("CONTOUR", sw.getX(), sw.getY() - 22, sw.getWidth(), 18, juce::Justification::centredLeft);
    const auto bp = bypass.getBounds();
    g.drawText ("BYPASS", bp.getX(), bp.getY() - 22, bp.getWidth(), 18, juce::Justification::centredLeft);

    // machined edge
    g.setColour (juce::Colour (0x40000000));
    g.drawRect (getLocalBounds(), 1);
}

void SuperalloyAudioProcessorEditor::resized()
{
    metalBox.setBounds (getLocalBounds().withSizeKeepingCentre (380, 38).translated (0, -22));
    contour.setBounds (juce::Rectangle<int> (0, 0, metalBox.getWidth(), 32)       // four segments, as wide as the Metal box
                           .withX (metalBox.getX())
                           .withY (metalBox.getBottom() + 44));
    bypass.setBounds (juce::Rectangle<int> (0, 0, 150, 32)
                          .withX (kWidth - 20 - 150)
                          .withY (38));
}

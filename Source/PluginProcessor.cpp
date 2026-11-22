/*
  ==============================================================================
    Superalloy — PluginProcessor.cpp
  ==============================================================================
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>
#include <mutex>

using namespace superalloy;

//==============================================================================
juce::StringArray SuperalloyAudioProcessor::metalNames()
{
    juce::StringArray names;
    for (const auto& m : kMetals)
        names.add (juce::String::fromUTF8 (m.menuName));
    return names;
}

juce::StringArray SuperalloyAudioProcessor::contourNames()
{
    return { "Subtle", "Color" };
}

juce::AudioProcessorValueTreeState::ParameterLayout SuperalloyAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "metal", 1 },
                                                              "Metal", metalNames(), kDefaultMetal));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "contour", 2 },
                                                              "Contour", contourNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 3 },
                                                            "Bypass", false));
    return layout;
}

SuperalloyAudioProcessor::SuperalloyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Superalloy", createLayout())
{
    metalParam = apvts.getRawParameterValue ("metal");
    contourParam = apvts.getRawParameterValue ("contour");
    bypassRaw = apvts.getRawParameterValue ("bypass");
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    jassert (bypassParameter != nullptr);
}

//==============================================================================
// Design data (skin-shelf fits, makeup gains, shadow matrices, Color contours) depends only on
// the sample rate. It is computed once per rate and shared by every instance.
std::shared_ptr<const MetalBank> SuperalloyAudioProcessor::bankForRate (double sampleRate)
{
    static std::mutex lock;
    static std::map<double, std::shared_ptr<const MetalBank>> cache;
    const std::lock_guard<std::mutex> guard (lock);
    if (auto it = cache.find (sampleRate); it != cache.end())
        return it->second;
    auto b = std::make_shared<MetalBank>();
    b->prepare (sampleRate);
    cache[sampleRate] = b;
    return b;
}

void SuperalloyAudioProcessor::prepareToPlay (double sampleRate, int)
{
    bank = bankForRate (sampleRate);
    const int metal = juce::jlimit (0, kNumMetals - 1, (int) std::lround (metalParam->load()));
    const bool color = contourParam->load() >= 0.5f;
    const bool bypass = bypassRaw->load() >= 0.5f;
    channels.assign ((size_t) juce::jmax (1, getTotalNumOutputChannels()), Channel630 {});
    for (auto& c : channels)
        c.prepare (*bank, metal, color, bypass);
    setLatencySamples (0);
}

void SuperalloyAudioProcessor::releaseResources() {}

void SuperalloyAudioProcessor::reset()
{
    for (auto& c : channels)
        c.reset();
}

bool SuperalloyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
template <typename Sample>
void SuperalloyAudioProcessor::processSamples (juce::AudioBuffer<Sample>& buffer, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int n = buffer.getNumSamples();
    const int metal = juce::jlimit (0, kNumMetals - 1, (int) std::lround (metalParam->load()));
    const bool color = contourParam->load() >= 0.5f;
    const bool bypass = forceBypass || bypassRaw->load() >= 0.5f;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& c = channels[(size_t) ch];
        c.selectMetal (metal);                      // seamless: 40 ms crossfade inside
        c.selectColor (color);                      // seamless: 40 ms crossfade inside
        c.selectBypass (bypass);                    // true bypass, seamless: 40 ms crossfade inside
        Sample* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            d[i] = (Sample) c.process ((double) d[i]);
    }

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
}

void SuperalloyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processSamples (buffer, false);
}

void SuperalloyAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processSamples (buffer, false);
}

void SuperalloyAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processSamples (buffer, true);
}

void SuperalloyAudioProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processSamples (buffer, true);
}

//==============================================================================
juce::AudioProcessorEditor* SuperalloyAudioProcessor::createEditor()
{
    return new SuperalloyAudioProcessorEditor (*this);
}

void SuperalloyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SuperalloyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SuperalloyAudioProcessor();
}

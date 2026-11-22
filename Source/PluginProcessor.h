/*
  ==============================================================================
    Superalloy — PluginProcessor.h
  ==============================================================================
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Channel630.h"
#include <memory>
#include <vector>

class SuperalloyAudioProcessor final : public juce::AudioProcessor
{
public:
    SuperalloyAudioProcessor();
    ~SuperalloyAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    // Hosts normally drive bypass through getBypassParameter(); if one calls the
    // bypassed path anyway, the engine keeps running and the output crossfades to dry.
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }
    bool supportsDoublePrecisionProcessing() const override { return true; }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Superalloy"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::StringArray metalNames();
    static juce::StringArray contourNames();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static std::shared_ptr<const superalloy::MetalBank> bankForRate (double sampleRate);

    template <typename Sample>
    void processSamples (juce::AudioBuffer<Sample>& buffer, bool forceBypass);

    std::atomic<float>* metalParam = nullptr;
    std::atomic<float>* contourParam = nullptr;
    std::atomic<float>* bypassRaw = nullptr;
    juce::AudioParameterBool* bypassParameter = nullptr;
    std::shared_ptr<const superalloy::MetalBank> bank;
    std::vector<superalloy::Channel630> channels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperalloyAudioProcessor)
};

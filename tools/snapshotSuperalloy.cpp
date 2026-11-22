/*
  Superalloy — snapshotSuperalloy.cpp
  Renders the real plugin editor off-screen to PNG (closed and open menu).
  Usage: snapshotSuperalloy <out-dir>
*/
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    SuperalloyAudioProcessor proc;
    proc.prepareToPlay (352800.0, 512);
    for (int metal : { 2, 11 })
    {
        proc.apvts.getParameter ("metal")->setValueNotifyingHost (proc.apvts.getParameter ("metal")->convertTo0to1 ((float) metal));
        proc.apvts.getParameter ("contour")->setValueNotifyingHost (metal == 11 ? 1.0f : 0.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());  // attachment reads the current value
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::File f = dir.getChildFile ("Superalloy_gui_" + juce::String (metal) + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
    return 0;
}

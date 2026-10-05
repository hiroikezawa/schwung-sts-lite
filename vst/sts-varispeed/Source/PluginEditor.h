#pragma once

#include "PluginProcessor.h"

class STSVarispeedAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      public juce::FileDragAndDropTarget,
      private juce::ChangeListener {
public:
    explicit STSVarispeedAudioProcessorEditor(STSVarispeedAudioProcessor&);
    ~STSVarispeedAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void chooseSample();
    void refreshSampleDisplay();
    void drawWaveform(juce::Graphics&, juce::Rectangle<float>);
    static void configureKnob(juce::Slider&);

    STSVarispeedAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Label sampleCaption;
    juce::Label sampleName;
    juce::Label rootCaption;
    juce::Label pitchCaption;
    juce::Label gainCaption;
    juce::ComboBox rootNote;
    juce::Slider pitch;
    juce::Slider gain;
    juce::TextButton loadButton { "Load Sample" };
    juce::TextButton reloadButton { "Reload" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rootAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    bool dragActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(STSVarispeedAudioProcessorEditor)
};

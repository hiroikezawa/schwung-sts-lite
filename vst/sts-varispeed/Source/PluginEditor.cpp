#include "PluginEditor.h"

namespace {
constexpr auto background = 0xff17191d;
constexpr auto panel = 0xff24272d;
constexpr auto accent = 0xfff0a34a;
constexpr auto muted = 0xff9ca3ad;
}

STSVarispeedAudioProcessorEditor::STSVarispeedAudioProcessorEditor(
    STSVarispeedAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    setSize(640, 390);
    setResizable(true, false);
    setResizeLimits(520, 340, 920, 520);

    titleLabel.setText("STS VARISPEED", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(21.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(accent));
    addAndMakeVisible(titleLabel);

    for (auto* label : { &sampleCaption, &rootCaption, &pitchCaption, &gainCaption }) {
        label->setColour(juce::Label::textColourId, juce::Colour(muted));
        label->setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(label);
    }
    sampleCaption.setText("SAMPLE", juce::dontSendNotification);
    rootCaption.setText("ROOT NOTE", juce::dontSendNotification);
    pitchCaption.setText("PITCH", juce::dontSendNotification);
    gainCaption.setText("GAIN", juce::dontSendNotification);

    sampleName.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    sampleName.setColour(juce::Label::textColourId, juce::Colours::white);
    sampleName.setMinimumHorizontalScale(0.6f);
    addAndMakeVisible(sampleName);

    for (int note = 0; note < 128; ++note)
        rootNote.addItem(STSVarispeedAudioProcessor::midiNoteName(note), note + 1);
    rootNote.setColour(juce::ComboBox::backgroundColourId, juce::Colour(panel));
    rootNote.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    addAndMakeVisible(rootNote);

    configureKnob(pitch);
    pitch.setTextValueSuffix(" st");
    configureKnob(gain);
    gain.setTextValueSuffix(" dB");
    addAndMakeVisible(pitch);
    addAndMakeVisible(gain);

    loadButton.onClick = [this] { chooseSample(); };
    reloadButton.onClick = [this] { processor.reloadSample(); };
    addAndMakeVisible(loadButton);
    addAndMakeVisible(reloadButton);

    rootAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, STSVarispeedAudioProcessor::rootNoteId, rootNote);
    pitchAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters, STSVarispeedAudioProcessor::pitchOffsetId, pitch);
    gainAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters, STSVarispeedAudioProcessor::gainId, gain);

    processor.addChangeListener(this);
    refreshSampleDisplay();
}

STSVarispeedAudioProcessorEditor::~STSVarispeedAudioProcessorEditor() {
    processor.removeChangeListener(this);
}

void STSVarispeedAudioProcessorEditor::configureKnob(juce::Slider& slider) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 22);
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(accent));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(panel));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(panel));
}

void STSVarispeedAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(background));
    auto waveform = getLocalBounds().toFloat().reduced(20.0f);
    waveform.removeFromTop(48.0f);
    waveform = waveform.removeFromTop(160.0f);
    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(waveform, 7.0f);
    g.setColour(juce::Colour(dragActive ? accent : 0xff3b4049));
    g.drawRoundedRectangle(waveform, 7.0f, dragActive ? 2.0f : 1.0f);
    drawWaveform(g, waveform.reduced(10.0f));
}

void STSVarispeedAudioProcessorEditor::drawWaveform(
    juce::Graphics& g, juce::Rectangle<float> area) {
    auto sample = processor.getSample();
    if (!sample || sample->mono.empty()) {
        g.setColour(juce::Colour(muted));
        g.drawFittedText("Drop a WAV file here", area.toNearestInt(),
                         juce::Justification::centred, 1);
        return;
    }

    juce::Path path;
    const int columns = juce::jmax(1, static_cast<int>(area.getWidth()));
    const double framesPerColumn = static_cast<double>(sample->mono.size()) / columns;
    const float centre = area.getCentreY();
    const float halfHeight = area.getHeight() * 0.46f;
    for (int x = 0; x < columns; ++x) {
        const size_t begin = static_cast<size_t>(x * framesPerColumn);
        const size_t end = juce::jmin(sample->mono.size(),
            static_cast<size_t>((x + 1) * framesPerColumn) + 1);
        float peak = 0.0f;
        for (size_t i = begin; i < end; ++i)
            peak = juce::jmax(peak, std::abs(sample->mono[i]));
        const float px = area.getX() + static_cast<float>(x);
        const float height = peak * halfHeight;
        path.startNewSubPath(px, centre - height);
        path.lineTo(px, centre + height);
    }
    g.setColour(juce::Colour(accent));
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void STSVarispeedAudioProcessorEditor::resized() {
    auto bounds = getLocalBounds().reduced(20);
    titleLabel.setBounds(bounds.removeFromTop(38));
    bounds.removeFromTop(170);

    auto sampleRow = bounds.removeFromTop(45);
    sampleCaption.setBounds(sampleRow.removeFromLeft(62));
    sampleName.setBounds(sampleRow.removeFromLeft(260));
    reloadButton.setBounds(sampleRow.removeFromRight(78).reduced(2));
    sampleRow.removeFromRight(8);
    loadButton.setBounds(sampleRow.removeFromRight(112).reduced(2));

    bounds.removeFromTop(8);
    const int columnWidth = bounds.getWidth() / 3;
    auto rootColumn = bounds.removeFromLeft(columnWidth).reduced(8, 0);
    auto pitchColumn = bounds.removeFromLeft(columnWidth).reduced(8, 0);
    auto gainColumn = bounds.reduced(8, 0);

    rootCaption.setBounds(rootColumn.removeFromTop(20));
    rootNote.setBounds(rootColumn.removeFromTop(28));
    pitchCaption.setBounds(pitchColumn.removeFromTop(20));
    pitch.setBounds(pitchColumn);
    gainCaption.setBounds(gainColumn.removeFromTop(20));
    gain.setBounds(gainColumn);
}

bool STSVarispeedAudioProcessorEditor::isInterestedInFileDrag(
    const juce::StringArray& files) {
    const bool interested = std::any_of(files.begin(), files.end(), [](const auto& path) {
        return juce::File(path).hasFileExtension("wav");
    });
    dragActive = interested;
    repaint();
    return interested;
}

void STSVarispeedAudioProcessorEditor::fileDragExit(const juce::StringArray&) {
    dragActive = false;
    repaint();
}

void STSVarispeedAudioProcessorEditor::filesDropped(
    const juce::StringArray& files, int, int) {
    dragActive = false;
    for (const auto& path : files) {
        juce::File file(path);
        if (file.hasFileExtension("wav")) {
            processor.loadSample(file);
            break;
        }
    }
    repaint();
}

void STSVarispeedAudioProcessorEditor::chooseSample() {
    chooser = std::make_unique<juce::FileChooser>(
        "Load WAV sample", processor.getSampleFile(), "*.wav");
    juce::Component::SafePointer<STSVarispeedAudioProcessorEditor> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& selected) {
            if (safeThis == nullptr)
                return;
            const auto file = selected.getResult();
            if (file.existsAsFile())
                safeThis->processor.loadSample(file);
        });
}

void STSVarispeedAudioProcessorEditor::changeListenerCallback(
    juce::ChangeBroadcaster*) {
    refreshSampleDisplay();
}

void STSVarispeedAudioProcessorEditor::refreshSampleDisplay() {
    sampleName.setText(processor.getSampleStatus(), juce::dontSendNotification);
    reloadButton.setEnabled(processor.getSampleFile() != juce::File());
    repaint();
}

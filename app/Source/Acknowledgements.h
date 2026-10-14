#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <BinaryData.h>

// Embedded copies keep acknowledgements available offline and in test executables.
class Acknowledgements final : public juce::Component {
public:
    Acknowledgements() {
        text.setMultiLine(true, true);
        text.setReadOnly(true);
        text.setScrollbarsShown(true);
        text.setCaretVisible(false);
        text.setFont(juce::Font(juce::FontOptions(14.0f)));
        text.setText(juce::String("FOH Gauge — About / Acknowledgements\n\n")
            + juce::String::fromUTF8(BinaryData::THIRD_PARTY_NOTICES_md,
                                     BinaryData::THIRD_PARTY_NOTICES_mdSize)
            + "\n\nStandalone Inter OFL text\n\n"
            + juce::String::fromUTF8(BinaryData::InterOFLLICENSE_txt,
                                     BinaryData::InterOFLLICENSE_txtSize), false);
        text.setAccessible(true);
        addAndMakeVisible(text);
        setSize(760, 560);
    }
    void resized() override { text.setBounds(getLocalBounds().reduced(12)); }
private:
    juce::TextEditor text;
};

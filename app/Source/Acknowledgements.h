#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <BinaryData.h>

#ifdef JUCE_APPLICATION_VERSION_STRING
 #define JUCE_APPLICATION_VERSION_STRING_OR_DEV JUCE_APPLICATION_VERSION_STRING
#else
 #define JUCE_APPLICATION_VERSION_STRING_OR_DEV "dev"
#endif
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
        setSize(760, 640);
    }
    static constexpr int headerHeight = 132;
    void paint(juce::Graphics& g) override {
        static const juce::Image logo = juce::ImageCache::getFromMemory(BinaryData::aboutlogo_png, BinaryData::aboutlogo_pngSize);
        auto header = getLocalBounds().removeFromTop(headerHeight);
        g.setColour(juce::Colour(0xff252527)); g.fillRect(header);
        if (logo.isValid())
            g.drawImageWithin(logo, header.getX(), header.getY() + 6, header.getWidth(), header.getHeight() - 24,
                              juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        g.setColour(juce::Colour(0xff8e8e93)); g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("Version " + juce::String(JUCE_APPLICATION_VERSION_STRING_OR_DEV), header.removeFromBottom(18),
                   juce::Justification::centred);
    }
    void resized() override { text.setBounds(getLocalBounds().withTrimmedTop(headerHeight).reduced(12)); }
private:
    juce::TextEditor text;
};

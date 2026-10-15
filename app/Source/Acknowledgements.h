#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <BinaryData.h>
#include "Widgets.h"

#ifdef JUCE_APPLICATION_VERSION_STRING
 #define JUCE_APPLICATION_VERSION_STRING_OR_DEV JUCE_APPLICATION_VERSION_STRING
#else
 #define JUCE_APPLICATION_VERSION_STRING_OR_DEV "dev"
#endif
// Embedded copies keep acknowledgements available offline and in test executables.
class Acknowledgements final : public juce::Component {
public:
    Acknowledgements() {
        // Give the reader its final width before laying out the offline notices.
        setSize(760, 640);
        text.setMultiLine(true, true);
        text.setReadOnly(true);
        text.setScrollbarsShown(true);
        text.setCaretVisible(false);
        text.setFont(FohLookAndFeel::font(foh::theme::type::body));
        text.setText(juce::String::fromUTF8("FOH Gauge \xe2\x80\x94 About / Acknowledgements\n\n")
            + juce::String::fromUTF8(BinaryData::THIRD_PARTY_NOTICES_md,
                                     BinaryData::THIRD_PARTY_NOTICES_mdSize)
            + "\n\nStandalone Inter OFL text\n\n"
            + juce::String::fromUTF8(BinaryData::InterOFLLICENSE_txt,
                                     BinaryData::InterOFLLICENSE_txtSize), false);
        text.setAccessible(true);
        addAndMakeVisible(text);
    }
    static constexpr int headerHeight = 132;
    void paint(juce::Graphics& g) override {
        const bool light = foh::ui::theme().isLight;
        const auto logo = juce::ImageCache::getFromMemory(light ? BinaryData::toolbarlockuplight_png : BinaryData::toolbarlockupdark_png,
                                                        light ? BinaryData::toolbarlockuplight_pngSize : BinaryData::toolbarlockupdark_pngSize);
        auto header = getLocalBounds().removeFromTop(headerHeight);
        g.setColour(foh::ui::col::pillBackground()); g.fillRect(header);
        if (logo.isValid())
            g.drawImageWithin(logo, header.getX() + foh::theme::space::ml, header.getY() + foh::theme::space::ml, header.getWidth() - 2 * foh::theme::space::ml, header.getHeight() - 3 * foh::theme::space::ml,
                              juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        g.setColour(foh::ui::col::textSecondary()); g.setFont(FohLookAndFeel::font(foh::theme::type::label));
        g.drawText("Version " + juce::String(JUCE_APPLICATION_VERSION_STRING_OR_DEV), header.removeFromBottom(18),
                   juce::Justification::centred);
    }
    void resized() override { text.setBounds(getLocalBounds().withTrimmedTop(headerHeight).reduced(12)); }
private:
    juce::TextEditor text;
};

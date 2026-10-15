#include <catch2/catch_test_macros.hpp>
#include "MainComponent.h"
#include <fohcore/input.h>
#include "ui_form_helpers.h"
#include <juce_graphics/juce_graphics.h>

namespace {
std::vector<juce::Button*> buttons(juce::Component& component) {
    std::vector<juce::Button*> found;
    for (auto* child : component.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child)) found.push_back(button);
    return found;
}
struct TemporaryCalibration {
    juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("foh-synthetic-cal", ".csv", false);
    ~TemporaryCalibration() { file.deleteFile(); }
};
class BannerMain : public MainComponent {
public:
    using MainComponent::MainComponent;
    using MainComponent::timerCallback;
    using MainComponent::model;
    using MainComponent::audioNotice;
    using MainComponent::microphoneNotice;
};
}

TEST_CASE("Recovery banners wrap in both themes at 100 and 200 percent", "[app][banner][ui]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    FohLookAndFeel look;
    const auto oldTheme = foh::ui::theme().id;
    for (auto theme : {"pro-dark", "pro-sunlight"}) {
        foh::theme::ThemeManager::get().setTheme(theme); look.applyTheme();
        for (int scale : {1, 2}) {
            juce::Image sheet(juce::Image::ARGB, 1000 * scale, 720 * scale, true);
            juce::Graphics graphics(sheet); graphics.fillAll(foh::ui::col::background());
            graphics.addTransform(juce::AffineTransform::scale((float) scale));
            int y = 0, column = 0;
            for (auto text : {fohapp::microphonePermissionMessage, "Audio device disconnected.",
                              "Audio dropped out; measuring restarted.", fohapp::micCalibrationErrorMessage}) {
                const bool calibration = text == fohapp::micCalibrationErrorMessage;
                const auto action = calibration ? juce::String::fromUTF8("Choose another file\xe2\x80\xa6") :
                    juce::String(text == fohapp::microphonePermissionMessage ? "Open Privacy Settings" : "Open Audio Setup");
                Banner banner; banner.setLookAndFeel(&look);
                int invoked = 0; banner.setMessage(text, action, [&] { ++invoked; });
                for (int width : {240, 1000}) {
                    banner.setSize(width, banner.preferredHeight(width));
                    auto controls = buttons(banner); REQUIRE(controls.size() == 2);
                    CHECK(controls[0]->getButtonText() == action);
                    for (auto* control : controls) CHECK(banner.getLocalBounds().contains(control->getBounds()));
                    CHECK_FALSE(controls[0]->getBounds().intersects(controls[1]->getBounds()));
                    auto snapshot = banner.createComponentSnapshot(banner.getLocalBounds());
                    int inkPixels = 0;
                    for (int py = 12; py < 28; ++py) for (int px = 36; px < 180; ++px)
                        if (snapshot.getPixelAt(px, py) == foh::ui::col::textPrimary()) ++inkPixels;
                    CHECK(inkPixels > 0);
                    controls[0]->onClick(); CHECK(invoked > 0);
                    if (width == 240) {
                        juce::Graphics::ScopedSaveState saved(graphics);
                        graphics.setOrigin(column * 250, 350); banner.paintEntireComponent(graphics, true);
                    }
                    if (width == 1000) {
                        juce::Graphics::ScopedSaveState saved(graphics);
                        graphics.setOrigin(0, y); banner.paintEntireComponent(graphics, true);
                        y += banner.getHeight() + 8;
                    }
                }
                auto controls = buttons(banner); controls[1]->onClick(); CHECK_FALSE(banner.isVisible());
                banner.setMessage(text, action, [] {}); CHECK_FALSE(banner.isVisible());
                banner.setMessage("", action, [] {});
                banner.setMessage(text, action, [] {}); CHECK(banner.isVisible());
                banner.setLookAndFeel(nullptr); ++column;
            }
            if (auto* output = std::getenv("FOH_BANNER_SNAPSHOTS")) {
                juce::File file = juce::File(output).getChildFile(juce::String(theme) + "-" + juce::String(scale * 100) + ".png");
                file.getParentDirectory().createDirectory(); file.deleteFile(); juce::FileOutputStream stream(file);
                REQUIRE(juce::PNGImageFormat().writeImageToStream(sheet, stream));
            }
        }
    }
    foh::theme::ThemeManager::get().setTheme(oldTheme);
}

TEST_CASE("Audio fault and permission banners have one recovery action", "[app][banner]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("foh-banner-workspaces", "", false);
    {
        BannerMain main(folder); main.model.analysis.stop(); main.model.dm.removeAudioCallback(&main.model.engine);
        for (auto state : {AudioEngine::Stopped, AudioEngine::InvalidInputs, AudioEngine::InvalidOutputs, AudioEngine::Overflow}) {
            main.model.engine.state = state; main.timerCallback();
            CHECK(main.audioNotice.isVisible());
            CHECK(main.audioNotice.message() == (state == AudioEngine::Stopped ? "Audio device disconnected." :
                state == AudioEngine::InvalidInputs ? "Choose available microphone and reference inputs." :
                state == AudioEngine::InvalidOutputs ? "Choose available generator outputs." :
                "Audio dropped out; measuring restarted."));
            CHECK(buttons(main.audioNotice)[0]->getButtonText() == "Open Audio Setup");
        }
        main.model.engine.state = AudioEngine::Ready;
        main.model.engine.captureEpoch = 1; main.model.engine.analysisEpoch = 0; main.timerCallback();
        CHECK(main.audioNotice.message() == "Audio changed; measuring restarted.");
        main.model.engine.analysisEpoch = 1; main.timerCallback(); CHECK_FALSE(main.audioNotice.isVisible());
        main.model.engine.deviceFailed = true; main.timerCallback();
        CHECK(main.audioNotice.message() == "Audio device disconnected.");
        main.setMicrophoneAccessBlocked(true); main.timerCallback();
        CHECK(main.microphoneNotice.message() == fohapp::microphonePermissionMessage);
        CHECK(buttons(main.microphoneNotice)[0]->getButtonText() == "Open Privacy Settings");
        CHECK_FALSE(main.audioNotice.isVisible());
    }
    folder.deleteRecursively();
}

TEST_CASE("Mic calibration errors use one inline recovery and preserve a working calibration", "[app][banner][calibration]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model; model.analysis.stop(); model.dm.removeAudioCallback(&model.engine);
    TemporaryCalibration synthetic;
    REQUIRE(synthetic.file.replaceWithText("20,0\n1000,1\n20000,0\n")); model.loadCalFile(synthetic.file);
    REQUIRE(model.cal.ok()); REQUIRE(model.calImportError.isEmpty());
    const auto previous = model.calText;
    for (const auto& text : {std::string("Sens Factor = 2 dBV\n20,0\n1000,0\n"), std::string(),
                             std::string("20,broken\n1000,0\n"), std::string("20 0\n1000 1\n2000 2e"),
                             std::string("20 0\n1000 1\n2000 0 0 1"), std::string("20 nan\n1000 inf"),
                             std::string("20 0\n1000 1\n\xff"), std::string(foh::input::maxBytes + 1, ' ')}) {
        REQUIRE(synthetic.file.replaceWithData(text.data(), text.size())); model.loadCalFile(synthetic.file);
        CHECK(model.calImportError == fohapp::micCalibrationErrorMessage);
        CHECK(model.calText == previous); CHECK(model.calOn);
        auto panel = PanelRegistry::standard().create("Calibration", model); panel->setSize(240, 100);
        auto& canvas = formCanvas(*panel);
        int recoveries = 0;
        for (auto* child : canvas.getChildren()) {
            if (auto* banner = dynamic_cast<Banner*>(child)) {
                CHECK(banner->isVisible()); CHECK(buttons(*banner)[0]->getButtonText() == juce::String::fromUTF8("Choose another file\xe2\x80\xa6")); ++recoveries;
            }
            if (auto* button = dynamic_cast<juce::TextButton*>(child)) CHECK_FALSE(button->isVisible());
        }
        CHECK(recoveries == 1);
    }
    REQUIRE(synthetic.file.deleteFile()); // selected file disappears before loading
    model.loadCalFile(synthetic.file);
    CHECK(model.calText == previous); CHECK(model.calOn);
    CHECK(std::isfinite(model.cal.dbAt(1000)));
    CHECK(model.calImportError == fohapp::micCalibrationErrorMessage);
    REQUIRE(synthetic.file.replaceWithText("20,0\n1000,0\n")); model.loadCalFile(synthetic.file);
    CHECK(model.calImportError.isEmpty());
}

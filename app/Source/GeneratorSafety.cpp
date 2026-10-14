#include "AppModel.h"
using namespace juce;

void AppModel::confirmGeneratorStart(const String& signal, std::function<void()> start, std::optional<float> requestedLevel) {
    if (generatorConfirmationPending) return;
    if (generatorStartConfirmed) { start(); return; }
    generatorConfirmationPending = true;
    const auto level = requestedLevel.value_or(engine.genGainDb.load());
    const String levelText = level < 0 ? u8("\xe2\x88\x92") + String(-level, 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".") : String(level, 1);
    generatorConfirmation = AlertWindow::showScopedAsync(MessageBoxOptions()
        .withIconType(MessageBoxIconType::WarningIcon).withTitle("Start generator")
        .withMessage(signal + " will play through the PA at " + levelText + " dBFS. Start?")
        .withButton("Start noise").withButton("Cancel"),
        [this, level, start = std::move(start)](int result) {
            generatorConfirmationPending = false;
            if (result != 1) return;
            generatorStartConfirmed = true;
            setGeneratorLevel(level); // Start at the level stated in the confirmation.
            start();
        });
}


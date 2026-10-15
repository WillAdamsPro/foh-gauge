#include "AppModel.h"
#include <fohcore/input.h>
using namespace juce;

void AppModel::loadCalFile(const File& f) {
    try {
        auto text = foh::input::read(f.getFullPathName().toStdString());
        auto parsed = foh::parseMicCal(text);
        if (!parsed.ok()) throw std::runtime_error(parsed.error);
        auto published = std::make_shared<foh::MicCal>(parsed);
        calImportError.clear();
        calText = std::move(text); cal = std::move(parsed); calFile = f.getFileName(); calOn = true;
        analysis.setCal(std::move(published), calFile); ++analysis.leqRevision;
        notify(Cal);
    } catch (const std::exception&) {
        calImportError = fohapp::micCalibrationErrorMessage;
        notify(Cal);
    }
}

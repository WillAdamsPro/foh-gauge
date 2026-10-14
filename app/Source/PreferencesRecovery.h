#pragma once
#include "FileValidation.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <fohcore/fohcore.h>
#include <fohcore/json.h>
#include <map>

namespace fohapp {
// Message-thread only. Publish all saved corrections together after validation.
inline bool loadLoopbackPreferences(const std::string& text, std::map<std::string, foh::LoopbackCorrection>& out) {
    std::string error;
    auto j = foh::Json::parse(text, &error);
    if (!error.empty() || !j.isObject()) return false;
    std::map<std::string, foh::LoopbackCorrection> candidate;
    for (const auto& entry : j.members()) {
        auto c = foh::LoopbackCorrection::fromJson(entry.second);
        if (!c.valid()) return false;
        candidate[entry.first] = std::move(c);
    }
    out = std::move(candidate); return true;
}
// Message-thread preflight: reject the audio setup as a unit before opening any
// device. Keeping only some saved channels can route against a different device.
inline bool recoverAudioPreferences(juce::PropertySet& prefs) {
    bool valid = true;
    auto text = prefs.getValue("audioDeviceState").toStdString();
    auto xml = boundedXml(text) ? juce::parseXML(juce::String(text)) : nullptr;
    if (!text.empty() || prefs.getBoolValue("setupComplete"))
        valid = xml && xml->hasTagName("DEVICESETUP");
    if (xml) {
        valid &= xml->getStringAttribute("deviceType").isNotEmpty();
        for (const auto* key : {"audioDeviceInChans", "audioDeviceOutChans"}) {
            auto mask = xml->getStringAttribute(key).toStdString();
            valid &= mask.size() <= 1024 && mask.find_first_not_of("01") == std::string::npos;
        }
        for (const auto* key : {"audioDeviceRate", "audioDeviceBufferSize"}) {
            if (!xml->hasAttribute(key)) continue;
            double value;
            bool rate = juce::String(key) == "audioDeviceRate";
            valid &= foh::input::number(xml->getStringAttribute(key).toStdString(), value)
                && (rate ? value >= 8000 && value <= 768000 : value >= 1 && value <= (1 << 20) && std::floor(value) == value);
        }
    }
    for (const auto* key : {"micChannel", "refChannel", "generatorChannel", "loopbackChannel"}) {
        if (prefs.getBoolValue("setupComplete") && !prefs.containsKey(key)) valid = false;
        if (!prefs.containsKey(key)) continue;
        double value;
        valid &= foh::input::number(prefs.getValue(key).toStdString(), value)
            && value >= -1 && value <= 1023 && std::floor(value) == value;
    }
    if (prefs.containsKey("setupComplete")) {
        auto flag = prefs.getValue("setupComplete");
        valid &= flag == "0" || flag == "1";
    }
    if (valid) return false;
    // Override common-property fallbacks too: removeValue alone would expose
    // the same corrupt value again through ApplicationProperties' fallback.
    prefs.setValue("audioDeviceState", "");
    for (const auto* key : {"micChannel", "refChannel", "generatorChannel", "loopbackChannel"}) prefs.setValue(key, -1);
    prefs.setValue("setupComplete", false);
    return true;
}
}

#pragma once
#include "fohcore.h"
#include "json.h"
#include "rig_profile.h"
#include <filesystem>
#include <optional>

namespace foh {
// Message/worker thread only: these APIs allocate and perform filesystem I/O.
// Optional opaque fields preserve future SPL calibration and target schemas.
struct Session {
    std::string deviceType, inputDevice, outputDevice;
    double sampleRate = 48000;
    int bufferSize = 512;
    std::string inputChannels, outputChannels; // JUCE channel masks, binary strings
    int mic = 0, reference = 1, outputA = 0, outputB = 1;
    bool micCalEnabled = false;
    std::string micCalName, micCalText; // embedded copy, never an external path
    std::optional<Json> splCal, targets;
    std::optional<RigProfile> rigProfile; // absent in older sessions: default guide flow
    std::vector<Trace> captures;
    Json workspace = Json::object(), measurement = Json::object();
};
// Save uses a sibling staging folder and rollback backup; existing sessions survive failure.
bool saveSession(const std::filesystem::path& folder, const Session&, std::string& error);
// Strong guarantee: output is unchanged on failure. Only fixed bundle paths are read.
// Missing/invalid optional rig answers use the default flow; notice reports that
// fallback on success (and is cleared otherwise). Notices are not saved.
bool loadSession(const std::filesystem::path& folder, Session&, std::string& error, std::string* notice = nullptr);
bool sessionDevicesAvailable(const Session&, const std::string& deviceType,
                             const std::vector<std::string>& inputs, const std::vector<std::string>& outputs);
}

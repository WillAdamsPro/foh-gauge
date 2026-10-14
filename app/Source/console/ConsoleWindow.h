#pragma once
#include "Control.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include <deque>
#include <mutex>

namespace foh::console {
// Created from the message thread. All socket I/O lives in Worker::run().
// No reference to AudioEngine, AnalysisEngine, or DSP state.
class ConsolePanel : public juce::Component, private juce::Timer {
public:
    ConsolePanel();
    ~ConsolePanel() override;
    void resized() override;
    void disconnect();
private:
    struct Worker : juce::Thread {
        explicit Worker(juce::String host);
        ~Worker() override;
        void run() override;
        bool enqueue(Bytes);
        std::vector<Bytes> replies();
        juce::String host;
        juce::DatagramSocket socket {false};
        std::mutex mutex;
        std::deque<Bytes> outgoing, incoming;
    };
    void timerCallback() override;
    void reconnect();
    void propose();
    void showReadValue();
    Target target() const;
    juce::TextEditor host, value;
    juce::ComboBox desk, bus, number, output, parameter, band;
    juce::TextButton connect {"Connect / Read"}, write {"Review change"};
    juce::Label status, instructions;
    std::unique_ptr<Worker> worker;
    Control control;
};
class ConsoleWindow : public juce::DocumentWindow {
public:
    ConsoleWindow();
    void closeButtonPressed() override;
};
}

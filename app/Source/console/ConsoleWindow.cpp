#include "ConsoleWindow.h"
#include <cstdlib>
using namespace juce;
namespace foh::console {
ConsolePanel::Worker::Worker(String h) : Thread("Console control"), host(std::move(h)) { startThread(); }
ConsolePanel::Worker::~Worker() { signalThreadShouldExit(); socket.shutdown(); stopThread(-1); }
bool ConsolePanel::Worker::enqueue(Bytes b) {
    std::lock_guard<std::mutex> lock(mutex);
    if (outgoing.size() >= 128) return false;
    outgoing.push_back(std::move(b)); return true;
}
std::vector<Bytes> ConsolePanel::Worker::replies() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Bytes> result;
    while (!incoming.empty()) { result.push_back(std::move(incoming.front())); incoming.pop_front(); }
    return result;
}
void ConsolePanel::Worker::run() {
    if (!socket.bindToPort(0)) return;
    while (!threadShouldExit()) {
        std::deque<Bytes> packets;
        { std::lock_guard<std::mutex> lock(mutex); packets.swap(outgoing); }
        for (const auto& b : packets) {
            if (threadShouldExit()) break;
            socket.write(host, 10023, b.data(), static_cast<int>(b.size()));
            wait(2); // Pace the console control port; never on the audio thread.
        }
        if (socket.waitUntilReady(true, 20) > 0) {
            std::uint8_t data[4096]; String sender; int port = 0;
            int count = socket.read(data, sizeof(data), false, sender, port);
            if (count > 0 && sender == host && port == 10023) {
                std::lock_guard<std::mutex> lock(mutex);
                if (incoming.size() < 128) incoming.emplace_back(data, data + count);
            }
        }
    }
}
ConsolePanel::ConsolePanel() : control([this](const Bytes& b) { return worker && worker->enqueue(b); }) {
    for (auto* c : std::initializer_list<Component*>{&host, &value, &desk, &bus, &number, &output, &parameter, &band, &connect, &write, &status, &instructions}) addAndMakeVisible(c);
    host.setTextToShowWhenEmpty("Console IPv4 address", Colours::grey);
    desk.addItem("Behringer X32 (OSC)", 1); desk.addItem("SQ-5 1.6.3 — awaiting protocol verification", 2); desk.setSelectedId(1);
    bus.addItem("Mix bus", 1); bus.addItem("Matrix", 2); bus.setSelectedId(1);
    for (int i = 1; i <= 16; ++i) { number.addItem(String(i), i); output.addItem("Output " + String(i), i); }
    output.addItem("No delay output selected", 17); output.setSelectedId(17); number.setSelectedId(1);
    const char* names[] {"EQ enabled (0/1)", "EQ frequency (Hz)", "EQ gain (dB)", "EQ Q", "EQ filter type (index)", "Delay enabled (0/1)", "Delay (ms)"};
    for (int i = 0; i < 7; ++i) parameter.addItem(names[i], i + 1);
    parameter.setSelectedId(3);
    for (int i = 1; i <= 6; ++i) band.addItem("EQ band " + String(i), i);
    band.setSelectedId(1); value.setText("0");
    instructions.setText("EQ is on the selected mix or matrix. X32 delay is on an output slot: select its routed output. Read does not change the console. Every write requires a separate confirmation.", dontSendNotification);
    status.setText("Disconnected", dontSendNotification);
    connect.onClick = [this] { reconnect(); }; write.onClick = [this] { propose(); };
    auto changed = [this] { control.cancel(); worker.reset(); status.setText("Selection changed — connect/read again", dontSendNotification); };
    for (auto* box : {&desk, &bus, &number, &output}) box->onChange = changed;
    host.onTextChange = changed;
    parameter.onChange = [this] { control.cancel(); showReadValue(); }; band.onChange = [this] { control.cancel(); showReadValue(); }; value.onTextChange = [this] { control.cancel(); };
    setSize(640, 340); startTimerHz(20);
}
ConsolePanel::~ConsolePanel() { stopTimer(); disconnect(); }
void ConsolePanel::disconnect() {
    control.cancel(); worker.reset(); control.select(target());
    status.setText("Disconnected", dontSendNotification);
}
Target ConsolePanel::target() const {
    return {desk.getSelectedId() == 1 ? Desk::x32 : Desk::sq, bus.getSelectedId() == 1 ? Bus::mix : Bus::matrix,
        number.getSelectedId(), output.getSelectedId() == 17 ? 0 : output.getSelectedId()};
}
void ConsolePanel::reconnect() {
    worker.reset(); control.select(target());
    if (target().desk == Desk::sq) { status.setText("SQ EQ/delay requires verified firmware 1.6.3 MixPad captures. Connection disabled.", dontSendNotification); return; }
    if (!validTarget(target())) { status.setText("Invalid target (X32 has 16 buses and 6 matrices).", dontSendNotification); return; }
    const auto ip = host.getText().trim(); auto parts = StringArray::fromTokens(ip, ".", "");
    bool valid = parts.size() == 4;
    for (const auto& part : parts) valid = valid && part.isNotEmpty() && part.containsOnly("0123456789") && part.getIntValue() <= 255;
    if (!valid) { status.setText("Enter a numeric console IPv4 address.", dontSendNotification); return; }
    worker = std::make_unique<Worker>(ip);
    status.setText(control.read() ? "Read requested — awaiting reply" : "Read request failed", dontSendNotification);
}
void ConsolePanel::timerCallback() {
    if (!worker) return;
    auto replies = worker->replies();
    for (const auto& b : replies) control.receive(b);
    if (!replies.empty()) showReadValue();
}
void ConsolePanel::showReadValue() {
    const auto p = static_cast<Parameter>(parameter.getSelectedId() - 1);
    const int eqBand = p == Parameter::frequency || p == Parameter::gain || p == Parameter::q || p == Parameter::shape ? band.getSelectedId() : 0;
    if (auto v = control.readValue(p, eqBand)) {
        status.setText("Read: " + String(describe(*v)), dontSendNotification);
    } else status.setText("Awaiting read-back for selected parameter", dontSendNotification);
}
void ConsolePanel::propose() {
    if (!worker) { status.setText("Connect / Read before proposing a change.", dontSendNotification); return; }
    const auto text = value.getText().trim().toStdString(); char* end = nullptr;
    const double n = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size()) { status.setText("Enter a finite numeric value.", dontSendNotification); return; }
    auto p = static_cast<Parameter>(parameter.getSelectedId() - 1);
    const int eqBand = p == Parameter::frequency || p == Parameter::gain || p == Parameter::q || p == Parameter::shape ? band.getSelectedId() : 0;
    auto id = control.propose({target(), p, eqBand, n});
    if (!id) { status.setText("Change rejected: check range, connection, and output source read-back for delay.", dontSendNotification); return; }
    const auto preview = control.preview();
    AlertWindow::showOkCancelBox(AlertWindow::WarningIcon, "Confirm console write", String(preview) + "\n\nSend this change to " + host.getText().trim() + "?", "Write", "Cancel", nullptr,
        ModalCallbackFunction::create([safe = Component::SafePointer<ConsolePanel>(this), token = *id](int result) {
            if (safe) safe->status.setText(safe->control.confirm(token, result == 1) ? "Write queued — read again to verify" : "No write sent", dontSendNotification);
        }));
}
void ConsolePanel::resized() {
    auto r = getLocalBounds().reduced(16);
    desk.setBounds(r.removeFromTop(30)); r.removeFromTop(6);
    host.setBounds(r.removeFromTop(30)); r.removeFromTop(6);
    auto row = r.removeFromTop(30); bus.setBounds(row.removeFromLeft(160)); number.setBounds(row.removeFromLeft(70)); output.setBounds(row);
    r.removeFromTop(6); row = r.removeFromTop(30); parameter.setBounds(row.removeFromLeft(220)); band.setBounds(row.removeFromLeft(130)); value.setBounds(row);
    r.removeFromTop(8); row = r.removeFromTop(30); connect.setBounds(row.removeFromLeft(160)); write.setBounds(row.removeFromLeft(160));
    r.removeFromTop(8); instructions.setBounds(r.removeFromTop(64)); status.setBounds(r);
}
void ConsoleWindow::closeButtonPressed() {
    if (auto* panel = dynamic_cast<ConsolePanel*>(getContentComponent())) panel->disconnect();
    setVisible(false);
}
ConsoleWindow::ConsoleWindow() : DocumentWindow("Console control", Colours::darkgrey, DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true); setContentOwned(new ConsolePanel, true); centreWithSize(getWidth(), getHeight()); setVisible(true);
}
}

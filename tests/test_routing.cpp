#include <catch2/catch_test_macros.hpp>
#include <fohcore/routing.h>
#include <limits>

TEST_CASE("Interface presets match model tokens in device names", "[routing]") {
    for (auto name : {"UMC404HD", "BEHRINGER UMC404HD 192k", "umc404hd", "USB (UMC404HD)"}) {
        const auto* p = foh::findInterfacePreset(name);
        REQUIRE(p);
        CHECK(p->micInput == 0); CHECK(p->referenceInput == 1);
        CHECK(p->loopbackOutput == 1); CHECK(p->consoleOutput == 0);
    }
    for (auto name : {"", "Unknown USB", "UMC404", "UMC404HD2", "XUMC404HD"})
        CHECK(foh::findInterfacePreset(name) == nullptr);
}
TEST_CASE("Connection detection distinguishes expected, swapped, and absent signals", "[routing]") {
    const float signal[] {0.1f, -0.1f, 0.1f, -0.1f};
    const float quiet[] {0, 0, 0, 0};
    const float leak[] {0.001f, -0.001f, 0.001f, -0.001f};
    const float* inputs[] {signal, quiet};
    foh::ConnectionDetector detector;
    detector.reset(0); detector.process(inputs, 2, 4);
    CHECK(detector.result() == foh::ConnectionResult::pass);
    detector.reset(1); detector.process(inputs, 2, 4);
    CHECK(detector.result() == foh::ConnectionResult::swapInputs);
    CHECK(std::string_view(foh::connectionHint(detector.result())).find("swap inputs") != std::string_view::npos);
    inputs[1] = leak; detector.reset(1); detector.process(inputs, 2, 4);
    CHECK(detector.result() == foh::ConnectionResult::swapInputs);
    inputs[1] = signal; detector.reset(1); detector.process(inputs, 2, 4);
    CHECK(detector.result() == foh::ConnectionResult::pass);
    inputs[0] = quiet; inputs[1] = nullptr;
    detector.reset(1); detector.process(inputs, 2, 4);
    CHECK(detector.result() == foh::ConnectionResult::noSignal);
    detector.reset(2); detector.process(inputs, 2, 4);
    CHECK(detector.result() == foh::ConnectionResult::invalid);
    const float bad[] {std::numeric_limits<float>::quiet_NaN()};
    inputs[0] = bad; detector.reset(0); detector.process(inputs, 2, 1);
    CHECK(detector.result() == foh::ConnectionResult::invalid);
    detector.reset(0); detector.process(nullptr, 65, 4);
    CHECK(detector.result() == foh::ConnectionResult::invalid);
}

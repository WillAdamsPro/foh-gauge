#pragma once
#include "SqCodec.h"
#include <array>
namespace foh::sq {
enum class StripKind { Input, Aux, Matrix };
struct Strip { StripKind kind{StripKind::Aux}; int number{1}; };
enum class Parameter { Gain, Frequency, DelayTime, DelayIn, PeqIn, Q, Shape };
enum class Band { LF, LM, HM, HF };
struct Target { Strip strip; Parameter parameter{Parameter::Gain}; Band band{Band::HM}; };
struct Address { std::uint8_t cat{}, reg{}, b3{}, mod{}; };
struct FirmwareProfile {
    const char* name;
    // Empty until capture 3. Display strings never authorize writes.
    Bytes versionBytes;
};
class SqAddressMap {
public:
    static const FirmwareProfile& profile();
    static bool firmwareVerified(const Bytes&);
    static std::optional<std::uint8_t> stripAddress(Strip);
    static std::optional<Address> address(Target);
    static bool writable(Target);
    static std::optional<Target> writableTarget(const Frame&);
    static const std::array<Strip, 2>& verifiedOutputs();
    static std::string describe(Target);
};
}

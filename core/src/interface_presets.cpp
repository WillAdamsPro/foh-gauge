#include "fohcore/routing.h"

namespace foh {
namespace {
constexpr InterfacePreset presets[] { {"UMC404HD", 0, 1, 1, 0} };
char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }
bool word(char c) noexcept { c = lower(c); return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
}
const InterfacePreset* findInterfacePreset(std::string_view name) noexcept {
    for (const auto& preset : presets) {
        const std::string_view model(preset.name);
        for (std::size_t i = 0; i + model.size() <= name.size(); ++i) {
            if ((i && word(name[i - 1])) || (i + model.size() < name.size() && word(name[i + model.size()]))) continue;
            std::size_t j = 0;
            while (j < model.size() && lower(name[i + j]) == lower(model[j])) ++j;
            if (j == model.size()) return &preset;
        }
    }
    return nullptr;
}
}

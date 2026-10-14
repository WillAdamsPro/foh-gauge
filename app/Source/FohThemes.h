// Theme palettes for FOH Gauge. See docs/VISUAL-SPEC.md.
#pragma once
#include "Strings.h"
#include "FohTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace foh::theme
{
    struct TraceSlot {
        const char* name;
        juce::Colour colour;
        std::array<float, 4> dash {};
        int dashCount = 0;
    };
    struct Theme {
        juce::String id, displayName;
        bool isLight = false;
        struct { juce::Colour background, nav, surface, surfaceRaised, border, grid, plotWell, control; } surfaces;
        struct { juce::Colour primary, secondary, axis, onAccent; } text;
        juce::Colour accent;
        struct { juce::Colour needle, ivory; } brand;
        struct { juce::Colour good, caution, poor; } status;
        struct { juce::Colour magnitude, phase; float width = 2.0f; bool glow = false; float lowCoherenceHatchAlpha = 0.35f; juce::Colour secondary; } trace;
        std::array<TraceSlot, 8> traceSlots;
        struct { juce::String ui, numeric; std::array<float, 7> sizes; } type;
        float cornerRadius = 6.0f, borderWidth = 1.0f;
        int navWidth = 220, helperWidth = 300;
    };

    inline Theme proDark()
    {
        Theme t; t.id = "pro-dark"; t.displayName = foh::str::dark;
        t.brand = { juce::Colour(0xffFF6A3D), juce::Colour(0xffFFF4E0) };
        t.surfaces = { juce::Colour(0xff141210), juce::Colour(0xff1E1B18), juce::Colour(0xff1E1B18),
            juce::Colour(0xff2A2622), t.brand.ivory.withAlpha(0.12f), t.brand.ivory.withAlpha(0.07f),
            juce::Colour(0xff100E0C), juce::Colour(0xff3A3630) };
        // Axis text is essential: 62% rather than the spec's non-essential 40%, for >=4.5:1 on controls.
        t.text = { t.brand.ivory, t.brand.ivory.withAlpha(0.62f), t.brand.ivory.withAlpha(0.62f), juce::Colour(0xff100E0C) };
        t.accent = juce::Colour(0xff0A84FF);
        t.status = { juce::Colour(0xff30D158), juce::Colour(0xffFFD60A), juce::Colour(0xffFF453A) };
        t.trace = { t.brand.needle, juce::Colour(0xffBF5AF2), 2.0f, false, 0.35f, juce::Colour(0xffFF9F0A) };
        t.traceSlots = {{ { "Ivory", t.brand.ivory }, { "Blue", t.accent, {6, 3}, 2 },
            { "Purple", t.trace.phase, {2, 2}, 2 }, { "Cyan", juce::Colour(0xff64D2FF), {8, 2, 2, 2}, 4 },
            { "Yellow", t.status.caution }, { "Green", t.status.good, {6, 3}, 2 },
            { "Orange", t.trace.secondary, {2, 2}, 2 }, { "Pink", juce::Colour(0xffFF375F), {8, 2, 2, 2}, 4 } }};
        t.type = { "", "", {11, 12, 13, 13, 17, 20, 34} };
        t.borderWidth = 0.5f;
        return t;
    }

    inline Theme proSunlight()
    {
        Theme t = proDark(); t.id = "pro-sunlight"; t.displayName = foh::str::sunlight; t.isLight = true;
        const juce::Colour ink(0xff1C1A17);
        t.brand.needle = juce::Colour(0xffC2410C);
        t.surfaces = { juce::Colour(0xffFFFFFF), juce::Colour(0xffF4F2EE), juce::Colour(0xffF4F2EE),
            juce::Colour(0xffE8E5DF), ink.withAlpha(0.40f), ink.withAlpha(0.18f),
            juce::Colour(0xffFAFAF8), juce::Colour(0xffFFFFFF) };
        t.text = { ink, ink.withAlpha(0.78f), ink.withAlpha(0.62f), juce::Colour(0xffFFFFFF) };
        t.accent = juce::Colour(0xff0040DD);
        t.status = { juce::Colour(0xff006B1F), juce::Colour(0xff7A5A00), juce::Colour(0xffB0000A) };
        t.trace = { t.brand.needle, juce::Colour(0xff7A1FA2), 3.5f, false, 0.35f, juce::Colour(0xff9A4A00) };
        const juce::Colour colours[] = {ink, t.accent, t.trace.phase, juce::Colour(0xff006580),
            t.status.caution, t.status.good, t.trace.secondary, juce::Colour(0xffB00040)};
        for (size_t i = 0; i < t.traceSlots.size(); ++i) t.traceSlots[i].colour = colours[i];
        t.type.sizes = {12, 13, 15, 15, 19, 24, 42};
        t.borderWidth = 1.0f;
        return t;
    }

    juce::String systemFontName(); // Native system UI family (SF Pro on macOS).

    // ---- Workspace / docking tokens (A2). 1x px. Overlay colours are composited over the element's surface.
    namespace ws
    {
        inline constexpr int   panelHeaderHeight   = 28;
        inline constexpr auto panelTitleSize = type::title;  // semibold, text.secondary; active panel = text.primary
        inline constexpr int   headerButtonSize    = 20;     // float / close hit box
        inline constexpr int   headerGlyphSize     = 12;
        inline constexpr int   headerButtonGap     = 2;
        inline constexpr int   headerPaddingX      = 8;
        inline constexpr int   tabHeight           = 26;
        inline constexpr int   tabPaddingX         = 12;
        inline constexpr int   tabMinWidth         = 72, tabMaxWidth = 200;
        inline constexpr int   tabCloseSize        = 14;
        inline constexpr int   tabIndicatorHeight  = 2;      // accent underline on active tab
        inline constexpr int   overflowButtonWidth = 24;
        inline constexpr int   splitterHitArea     = 8;      // invisible grab zone
        inline constexpr float splitterLine        = 1.0f;   // visible line (0.5 on Retina dark)
        inline constexpr float splitterLineActive  = 2.0f;   // hover / drag
        inline constexpr int   panelMinWidth       = 240, panelMinHeight = 140;
        inline constexpr int   plotPanelMinWidth   = 420, plotPanelMinHeight = 220;
        inline constexpr int   inspectorMinWidth   = 260, inspectorMaxWidth = 420;
        inline constexpr int   switcherHeight      = 30;     // segmented control in toolbar
        inline constexpr int   switcherSegmentPadX = 12;
        inline constexpr auto shortcutHintSize = type::caption;  // text.tertiary
        inline constexpr int   floatTitleBarHeight = 28;
        inline constexpr int   floatMinWidth       = 320, floatMinHeight = 200;
        inline constexpr int   snapDistance        = 24;     // drag within this of a dock edge to snap
        inline constexpr int   dropZoneInset       = 6;
        inline constexpr float dropZoneAlpha       = 0.20f;  // accent fill for dock-target preview
        inline constexpr int   menuItemHeight      = 22, menuPaddingX = 10;
        inline constexpr float menuRadius          = 6.0f;
        inline constexpr int   animMs              = 120;    // hover/snap transitions

        // Interaction overlays (white on dark, black on light)
        inline juce::Colour hoverOverlay   (const Theme& t) { return t.isLight ? juce::Colour (0x14000000) : juce::Colour (0x14FFFFFF); } // 8%
        inline juce::Colour pressedOverlay (const Theme& t) { return t.isLight ? juce::Colour (0x29000000) : juce::Colour (0x29FFFFFF); } // 16%
        inline juce::Colour splitterIdle   (const Theme& t) { return t.surfaces.border; }
        inline juce::Colour splitterActive (const Theme& t) { return t.accent; }
        inline juce::Colour closeHover     (const Theme& t) { return t.status.poor; }   // glyph only, with the × shape
        inline juce::Colour panelHeaderBg  (const Theme& t) { return t.surfaces.surfaceRaised; }
        inline juce::Colour tabActiveBg    (const Theme& t) { return t.surfaces.background; }  // tab merges with panel body
        inline juce::Colour tabInactiveBg  (const Theme& t) { return panelHeaderBg (t); }
    }

    // Single source of truth; components call ThemeManager::get().current() in paint()
    // and register as ChangeListeners to repaint/re-layout on switch.
    class ThemeManager : public juce::ChangeBroadcaster
    {
    public:
        static ThemeManager& get() { static ThemeManager m; return m; }
        const Theme& current() const noexcept { return themes[(size_t) index]; }
        const std::vector<Theme>& all() const noexcept { return themes; }
        void setTheme (const juce::String& requestedId)
        {
            const auto id = (requestedId == "sunlight" || requestedId == "apple-pro-sunlight" || requestedId == "c-sunlight")
                ? juce::String("pro-sunlight") : (requestedId == "dark" || requestedId == "apple-pro-dark"
                || requestedId == "a" || requestedId == "b" || requestedId == "c") ? juce::String("pro-dark") : requestedId;
            for (size_t i = 0; i < themes.size(); ++i)
                if (themes[i].id == id && (int) i != index) { index = (int) i; sendChangeMessage(); return; }
        }
    private:
        ThemeManager() : themes { proDark(), proSunlight() } {}
        std::vector<Theme> themes;
        int index = 0; // Pro Dark default
    };
}

/* Usage:
   struct TfView : juce::Component, juce::ChangeListener {
       TfView()  { foh::theme::ThemeManager::get().addChangeListener (this); }
       ~TfView() override { foh::theme::ThemeManager::get().removeChangeListener (this); }
       void changeListenerCallback (juce::ChangeBroadcaster*) override { resized(); repaint(); }
       void paint (juce::Graphics& g) override { auto& t = foh::theme::ThemeManager::get().current(); g.fillAll (t.surfaces.background); }
   };
   Hook a LookAndFeel_V4 subclass to the same listener to refresh ColourIds for stock widgets. */

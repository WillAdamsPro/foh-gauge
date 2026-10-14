// FohThemes.h: switchable visual-direction themes for FOH Gauge (JUCE 8). Values match src/gen.py.
#pragma once
#include "Strings.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace foh::theme
{
    struct Theme
    {
        juce::String id, displayName;
        bool isLight = false;
        struct { juce::Colour background, nav, surface, surfaceRaised, border, grid; } surfaces;
        struct { juce::Colour primary, secondary, axis, onAccent; } text;
        juce::Colour accent;
        struct { juce::Colour good, caution, poor; } status;          // always paired with circle / triangle / square + label
        struct { juce::Colour magnitude, phase; float width = 2.0f; bool glow = false; float lowCoherenceHatchAlpha = 0.35f; juce::Colour secondary; } trace; // secondary: sub trace (added by Lyra)
        struct { juce::String ui, numeric; float base, title, readout; } type;
        float cornerRadius = 6.0f, borderWidth = 1.0f;
        int navWidth = 200, helperWidth = 320;
    };

    [[deprecated ("Superseded by applePro themes (Oct 2026)")]] inline Theme studioInstrument()
    {
        Theme t; t.id = "a"; t.displayName = "Studio Instrument";
        t.surfaces = { juce::Colour (0xff17191C), juce::Colour (0xff1B1E22), juce::Colour (0xff202328), juce::Colour (0xff272B31), juce::Colour (0xff33373E), juce::Colour (0xff2C3036) };
        t.text = { juce::Colour (0xffE8E6E1), juce::Colour (0xffA3A7AE), juce::Colour (0xff7D828A), juce::Colour (0xff0B1220) };
        t.accent = juce::Colour (0xff6FB7FF);
        t.status = { juce::Colour (0xff6FD39A), juce::Colour (0xffE8C468), juce::Colour (0xffF07A6E) };
        t.trace = { juce::Colour (0xff6FB7FF), juce::Colour (0xffB9A6FF), 1.5f, true, 0.35f };
        t.type = { "Inter", "JetBrains Mono", 13.0f, 20.0f, 44.0f };
        t.cornerRadius = 6.0f; t.borderWidth = 1.0f; t.navWidth = 200; t.helperWidth = 310;
        return t;
    }

    [[deprecated ("Superseded by applePro themes (Oct 2026)")]] inline Theme nightOps()
    {
        Theme t; t.id = "b"; t.displayName = "Night Ops";
        t.surfaces = { juce::Colour (0xff070605), juce::Colour (0xff0C0A08), juce::Colour (0xff110E0B), juce::Colour (0xff1A1510), juce::Colour (0xff2A2018), juce::Colour (0xff211A13) };
        t.text = { juce::Colour (0xffE9C9A4), juce::Colour (0xffB08B66), juce::Colour (0xff8A6C50), juce::Colour (0xff170C02) };
        t.accent = juce::Colour (0xffFFA43A);
        t.status = { juce::Colour (0xffC8B060), juce::Colour (0xffFFA43A), juce::Colour (0xffFF5A3C) };
        t.trace = { juce::Colour (0xffFFA43A), juce::Colour (0xffD86A4A), 3.0f, false, 0.35f };
        t.type = { "Barlow Condensed", "Barlow Condensed", 15.0f, 24.0f, 72.0f };
        t.cornerRadius = 2.0f; t.borderWidth = 2.0f; t.navWidth = 180; t.helperWidth = 320;
        return t;
    }

    [[deprecated ("Superseded by applePro themes (Oct 2026)")]] inline Theme friendlyGuide()
    {
        Theme t; t.id = "c"; t.displayName = "Friendly Guide";
        t.surfaces = { juce::Colour (0xff1A1714), juce::Colour (0xff211D19), juce::Colour (0xff28231E), juce::Colour (0xff332C25), juce::Colour (0xff40372E), juce::Colour (0xff3A322A) };
        t.text = { juce::Colour (0xffF3EDE4), juce::Colour (0xffC3B8A9), juce::Colour (0xff9C9183), juce::Colour (0xff0E1D19) };
        t.accent = juce::Colour (0xff5ED1B8);
        t.status = { juce::Colour (0xff7DDB8E), juce::Colour (0xffF2C14E), juce::Colour (0xffF2785E) };
        t.trace = { juce::Colour (0xff5ED1B8), juce::Colour (0xffF2A65E), 3.0f, false, 0.35f };
        t.type = { "Atkinson Hyperlegible", "Atkinson Hyperlegible", 16.0f, 28.0f, 56.0f };
        t.cornerRadius = 18.0f; t.borderWidth = 1.0f; t.navWidth = 210; t.helperWidth = 360;
        return t;
    }

    [[deprecated ("Superseded by applePro themes (Oct 2026)")]] inline Theme friendlyGuideSunlight()
    {
        Theme t; { _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") t = friendlyGuide(); _Pragma("GCC diagnostic pop") } t.id = "c-sunlight"; t.displayName = "Friendly Guide (Sunlight)"; t.isLight = true;
        t.surfaces = { juce::Colour (0xffF7F4EE), juce::Colour (0xffECE6DC), juce::Colour (0xffFFFFFF), juce::Colour (0xffF1ECE3), juce::Colour (0xff6B6258), juce::Colour (0xffD6CFC4) };
        t.text = { juce::Colour (0xff14110E), juce::Colour (0xff3A332C), juce::Colour (0xff3A332C), juce::Colour (0xffFFFFFF) };
        t.accent = juce::Colour (0xff00594C);
        t.status = { juce::Colour (0xff1E6B2E), juce::Colour (0xff7A5600), juce::Colour (0xffA3200D) };
        t.trace = { juce::Colour (0xff00594C), juce::Colour (0xff8A3B00), 4.0f, false, 0.35f };
        t.type = { "Atkinson Hyperlegible", "Atkinson Hyperlegible", 18.0f, 30.0f, 60.0f };
        t.borderWidth = 2.0f;
        return t;
    }


    // ---- Apple-pro (current). UI font = macOS system font (SF Pro) at runtime; "Inter" was only the mockup stand-in.
    // Alpha label colours are pre-composited over windowBackground for JUCE (labelColor 85%, secondary 55%, tertiary 30%).
    inline Theme appleProDark()
    {
        Theme t; t.id = "apple-pro-dark"; t.displayName = foh::str::dark;
        t.surfaces = { juce::Colour (0xff1E1E1E),   // windowBackground (plot area #161616 drawn by plot)
                       juce::Colour (0xb82C2C2E),   // sidebar: rgba(44,44,46,0.72), vibrancy fallback
                       juce::Colour (0xff2B2B2B),   // toolbar / inspector
                       juce::Colour (0x1aFFFFFF),   // controlBackground: white 10%
                       juce::Colour (0x1aFFFFFF),   // separator: white 10%
                       juce::Colour (0x12FFFFFF) }; // grid: white 7%
        t.text = { juce::Colour (0xd9FFFFFF), juce::Colour (0x8cFFFFFF), juce::Colour (0x4dFFFFFF), juce::Colour (0xffFFFFFF) };
        t.accent = juce::Colour (0xff0A84FF);
        t.status = { juce::Colour (0xff30D158), juce::Colour (0xffFFD60A), juce::Colour (0xffFF453A) };
        t.trace = { juce::Colour (0xff0A84FF), juce::Colour (0xffBF5AF2), 1.75f, false, 0.10f, juce::Colour (0xffFF9F0A) };
        t.type = { "", "", 13.0f, 14.0f, 34.0f };   // "" = system font, see systemFontName()
        t.cornerRadius = 6.0f; t.borderWidth = 0.5f; t.navWidth = 220; t.helperWidth = 300;
        return t;
    }

    inline Theme appleProSunlight()
    {
        Theme t = appleProDark(); t.id = "apple-pro-sunlight"; t.displayName = foh::str::sunlight; t.isLight = true;
        t.surfaces = { juce::Colour (0xffFFFFFF), juce::Colour (0xffE8E8EA), juce::Colour (0xffF2F2F2), juce::Colour (0xffFFFFFF), juce::Colour (0xff8E8E93), juce::Colour (0xffD1D1D6) };
        t.text = { juce::Colour (0xff000000), juce::Colour (0xff2C2C2E), juce::Colour (0xff48484A), juce::Colour (0xffFFFFFF) };
        t.accent = juce::Colour (0xff0040DD);
        t.status = { juce::Colour (0xff006B1F), juce::Colour (0xff7A5A00), juce::Colour (0xffB0000A) };
        t.trace = { juce::Colour (0xff0040DD), juce::Colour (0xff7A1FA2), 3.5f, false, 0.22f, juce::Colour (0xff9A4A00) };
        t.type = { "", "", 15.0f, 16.0f, 42.0f };
        t.borderWidth = 1.0f;
        return t;
    }

    inline juce::String systemFontName()
    {
       #if JUCE_MAC
        return juce::Font::getDefaultSansSerifFontName();   // resolves to the system UI font (SF Pro)
       #elif JUCE_WINDOWS
        return "Segoe UI";
       #else
        return juce::Font::getDefaultSansSerifFontName();
       #endif
    }


    // ---- Workspace / docking tokens (A2). 1x px. Overlay colours are composited over the element's surface.
    namespace ws
    {
        inline constexpr int   panelHeaderHeight   = 28;
        inline constexpr float panelTitleSize      = 12.0f;  // semibold, text.secondary; active panel = text.primary
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
        inline constexpr float shortcutHintSize    = 11.0f;  // text.tertiary
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
        inline juce::Colour panelHeaderBg  (const Theme& t) { return t.isLight ? juce::Colour (0xffEDEDEF) : juce::Colour (0xff252526); }
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
        void setTheme (const juce::String& id)
        {
            for (size_t i = 0; i < themes.size(); ++i)
                if (themes[i].id == id && (int) i != index) { index = (int) i; sendChangeMessage(); return; }
        }
    private:
        ThemeManager() : themes { appleProDark(), appleProSunlight() } {}   // deprecated themes intentionally not registered
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

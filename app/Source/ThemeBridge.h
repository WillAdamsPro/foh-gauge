// ThemeBridge.h: the single place UI code gets colours / widths / radii from.
// Everything resolves against foh::theme::ThemeManager::get().current() at call time,
// so switching between Dark and Sunlight only needs a repaint + relayout.
#pragma once
#include "FohTheme.h"   // Iris's static spec (type sizes, dash patterns, frame base size)
#include "FohThemes.h"  // switchable Theme + ThemeManager

namespace foh::ui
{
    inline const foh::theme::Theme& theme() { return foh::theme::ThemeManager::get().current(); }

    // Pre-composite overlays for stock JUCE widgets that require opaque colours.
    inline juce::Colour flat(juce::Colour c) { return theme().surfaces.background.overlaidWith(c); }
    inline juce::String uiFontName() { return theme().type.ui.isEmpty() ? foh::theme::systemFontName() : theme().type.ui; }

    namespace col
    {
        using juce::Colour;
        inline Colour background()        { return theme().surfaces.background; }
        inline Colour statusBarBackground(){ return theme().surfaces.surfaceRaised; }
        inline Colour navBackground()     { return flat(theme().surfaces.nav); }
        inline Colour helperBackground()  { return flat(theme().surfaces.surface); }
        inline Colour cardBackground()    { return flat(theme().surfaces.surface); }
        inline Colour controlBackground() { return theme().surfaces.control; }
        inline Colour pillBackground()    { return flat(theme().surfaces.surfaceRaised); }
        inline Colour navItemActiveBg()   { return theme().accent.withMultipliedAlpha(theme().isLight ? 0.18f : 0.55f); }
        inline Colour border()            { return theme().surfaces.border; }
        inline Colour grid()              { return theme().surfaces.grid; }
        inline Colour divider()           { return theme().surfaces.grid; }
        inline Colour pillBorder()        { return theme().surfaces.border; }
        inline Colour ghostBorder()       { return theme().surfaces.border.interpolatedWith(theme().text.axis, 0.3f); }
        inline Colour cardBorderEmphasis(){ return theme().surfaces.border.interpolatedWith(theme().accent, 0.25f); }
        inline Colour textPrimary()       { return theme().text.primary; }
        inline Colour textStrong()        { return theme().text.primary; }
        inline Colour textBody()          { return theme().text.primary.interpolatedWith(theme().text.secondary, 0.5f); }
        inline Colour textSecondary()     { return theme().text.secondary; }
        inline Colour textAxis()          { return theme().text.axis; }
        inline Colour textOnAccent()      { return theme().text.onAccent; }
        inline Colour accent()            { return theme().accent; }
        inline Colour ok()                { return theme().status.good; }
        inline Colour warning()           { return theme().status.caution; }
        inline Colour danger()            { return theme().status.poor; }
        inline Colour traceLive()         { return theme().trace.magnitude; }
        inline Colour tracePhase()        { return theme().trace.phase; }
        inline Colour traceSecondary()    { return theme().trace.secondary.isTransparent() ? theme().status.caution : theme().trace.secondary; } // sub trace
        inline Colour traceStored()       { return theme().text.axis; }
        inline Colour traceCoherence()    { return theme().text.secondary; }
        inline Colour traceSum()          { return theme().text.primary; }
        inline Colour traceMainsMuted()   { return theme().text.axis.withMultipliedAlpha(0.8f); }
        inline Colour traceSubMuted()     { return traceSecondary().withMultipliedAlpha(0.6f); }
        inline Colour plotWell()          { return theme().surfaces.plotWell; }
        inline Colour rtaBar()            { return flat(theme().surfaces.surface).interpolatedWith(theme().accent, 0.25f); }
        inline Colour successBadgeBg()    { return flat(theme().surfaces.surface).interpolatedWith(theme().status.good, 0.18f); }
        // Fixed named palette, resolved against the active theme.
        inline Colour storedTrace(int i)  { return theme().traceSlots[(size_t) juce::jlimit(0, 7, i)].colour; }
        inline float crossoverBandAlpha()  { return foh::theme::colour::crossoverBandAlpha; }
        inline float annotationBandAlpha() { return foh::theme::colour::annotationBandAlpha; }
    }

    namespace dim
    {
        inline float hairline()       { return theme().borderWidth; }
        inline float trace()          { return theme().trace.width; }
        inline float traceSecondary() { return std::max(1.0f, theme().trace.width * 0.7f); }
        inline float traceStored()    { return std::max(1.0f, theme().trace.width * 0.6f); }
        inline float navActiveBar()   { return foh::theme::stroke::navActiveBar; }
        inline float radiusCard()     { return theme().cornerRadius; }
        inline float radiusButton()   { return std::min(theme().cornerRadius, 10.0f); }
        inline float radiusTag()      { return std::min(theme().cornerRadius, 4.0f); }
        inline float radiusProgress() { return foh::theme::radius::progress; }
        inline int navWidth()         { return theme().navWidth; }
        inline int helperWidth()      { return theme().helperWidth; }
        inline int statusBarHeight()  { return foh::theme::frame::statusBarHeight; }
        inline int mainPadding()      { return foh::theme::frame::mainPadding; }
    }
}

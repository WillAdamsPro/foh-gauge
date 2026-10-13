// FohTheme.h: FOH Gauge design tokens. Values match src/gen.py exactly (1x px). Header-only.
#pragma once
#include <juce_graphics/juce_graphics.h>

namespace foh::theme
{
    // ---------- Colours (0xAARRGGBB) ----------
    namespace colour
    {
        // Surfaces
        inline const juce::Colour background        { 0xff0E1013 }; // body
        inline const juce::Colour navBackground     { 0xff121418 }; // left nav
        inline const juce::Colour helperBackground  { 0xff14171B }; // right helper panel
        inline const juce::Colour statusBarBackground{ 0xff16191E }; // top bar
        inline const juce::Colour cardBackground    { 0xff16191E }; // .card
        inline const juce::Colour pillBackground    { 0xff1F2329 };
        inline const juce::Colour navItemActiveBg   { 0xff1E2A2D }; // also selected pill
        inline const juce::Colour successBadgeBg    { 0xff1E3A28 }; // done-screen tick circle
        // Lines / borders
        inline const juce::Colour border            { 0xff262A31 }; // panel borders + plot grid
        inline const juce::Colour divider           { 0xff22262C }; // row dividers, meter/progress track
        inline const juce::Colour cardBorderEmphasis{ 0xff2F3A3D }; // action bar / start cards
        inline const juce::Colour pillBorder        { 0xff30353D };
        inline const juce::Colour ghostBorder       { 0xff3A4048 }; // ghost button + pending progress step
        // Text
        inline const juce::Colour textPrimary       { 0xffE6E8EB };
        inline const juce::Colour textBody          { 0xffC9CDD3 }; // helper body, pill text
        inline const juce::Colour textNavInactive   { 0xffA9AEB6 };
        inline const juce::Colour textSecondary     { 0xff9AA0A8 }; // labels, sub copy
        inline const juce::Colour textAxis          { 0xff7A808A }; // axis ticks, pending step labels
        inline const juce::Colour textNavSection    { 0xff6B717A };
        inline const juce::Colour textStrong        { 0xffFFFFFF }; // active nav, emphasis
        inline const juce::Colour textOnAccent      { 0xff071316 }; // primary button label
        // Semantic accents
        inline const juce::Colour accent            { 0xff3BC9DB }; // live trace, primary action, mains
        inline const juce::Colour warning           { 0xffF2B84B }; // amber caution, sub trace, EXAMPLE DATA tag
        inline const juce::Colour danger            { 0xffFF6B6B }; // over limit, limit line
        inline const juce::Colour ok                { 0xff7BD88F }; // ticks, completed steps
        // Traces
        inline const juce::Colour traceStored       { 0xff8A9099 }; // stored/reference trace (dashed)
        inline const juce::Colour traceCoherence    { 0xffA9AEB6 };
        inline const juce::Colour traceSum          { 0xffFFFFFF }; // mains+sub summed
        inline const juce::Colour traceMainsMuted   { 0xff6A7079 }; // mains component under sum (dashed)
        inline const juce::Colour traceSubMuted     { 0xff8A7040 }; // sub component under sum (dashed)
        inline const juce::Colour rtaBar            { 0xff3A4A50 };
        inline const juce::Colour rtaBarLegend      { 0xff5A6A70 };
        // Alpha overlays (applied to accent/warning)
        inline constexpr float crossoverBandAlpha   = 0.07f; // accent fill
        inline constexpr float annotationBandAlpha  = 0.12f; // warning fill (TF dip highlight)
    }

    // ---------- Type ----------
    namespace type
    {
        inline constexpr const char* fontFamily = "Inter";          // fallbacks: Helvetica Neue, Arial
        inline constexpr int weightRegular = 400, weightSemibold = 600, weightBold = 700;
        inline constexpr float axisTick      = 11.0f; // plot ticks, nav section, EXAMPLE tag
        inline constexpr float label         = 12.0f; // uppercase labels (letter-spacing 0.06em), plot annotations
        inline constexpr float small         = 13.0f; // pills, legends, progress labels, helper title
        inline constexpr float body          = 14.0f; // base + helper body
        inline constexpr float navItem       = 15.0f; // nav items, buttons, logo-adjacent
        inline constexpr float logo          = 16.0f; // logo, action-bar copy, summary rows
        inline constexpr float buttonLarge   = 17.0f; // guide primary action
        inline constexpr float cardTitle     = 20.0f;
        inline constexpr float unitSuffix    = 22.0f;
        inline constexpr float title         = 24.0f; // h1
        inline constexpr float valueMedium   = 26.0f; // suggested delay
        inline constexpr float valueDelay    = 28.0f; // delay finder readout
        inline constexpr float icon          = 30.0f;
        inline constexpr float valueLarge    = 40.0f; // dose, peak
        inline constexpr float valueHero     = 64.0f; // LAeq hero numbers (tabular figures)
        inline constexpr float lineHeight    = 1.5f;  // helper/action copy
        inline constexpr float labelTracking = 0.06f; // em
    }

    // ---------- Spacing (px) ----------
    namespace space
    {
        inline constexpr int xxs = 4, xs = 6, s = 8, m = 10, ml = 12, l = 14, xl = 16, xxl = 18, xxxl = 20, page = 22;
    }

    // ---------- Corner radii (px) ----------
    namespace radius
    {
        inline constexpr float progress = 3.0f, tag = 4.0f, meter = 7.0f, dose = 8.0f,
                               navItem = 9.0f, button = 10.0f, card = 12.0f, pill = 18.0f;
    }

    // ---------- Strokes (px) ----------
    namespace stroke
    {
        inline constexpr float hairline = 1.0f;     // borders, grid
        inline constexpr float traceStored = 1.5f;  // RTA stored trace
        inline constexpr float traceSecondary = 2.0f; // annotation circles, alt trace
        inline constexpr float trace = 2.5f;        // live traces
        inline constexpr float navActiveBar = 3.0f; // inset left bar on active nav item
        inline constexpr float dashStored[]  = { 6.0f, 5.0f }; // stored traces
        inline constexpr float dashLimit[]   = { 6.0f, 4.0f }; // limit line, delayed sub
        inline constexpr float dashComponent[] = { 5.0f, 4.0f }; // components under sum
    }

    // ---------- Frame (px @1x, base 1440x900) ----------
    namespace frame
    {
        inline constexpr int baseWidth = 1440, baseHeight = 900;
        inline constexpr int statusBarHeight = 56, navWidth = 200, helperWidth = 320;
        inline constexpr int mainPadding = 22;
        inline constexpr int minWidth = 1280, minHeight = 800;
    }
}

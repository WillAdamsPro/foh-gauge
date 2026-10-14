// FOH Gauge type roles and geometry tokens. Palettes and sizes live in FohThemes.h.
#pragma once
#include <juce_graphics/juce_graphics.h>

namespace foh::theme
{
    // Roles index the current theme's seven-size scale; no UI point sizes at call sites.
    namespace type {
        enum Role { caption, label, body, title, heading, valueMd, valueLg };
    }
    namespace colour {
        inline constexpr float crossoverBandAlpha = 0.07f;
        inline constexpr float annotationBandAlpha = 0.12f;
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

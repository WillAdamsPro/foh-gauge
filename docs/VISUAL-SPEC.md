# FOH Gauge visual spec v1 — A2 Pro Dark × VU Character
Supersedes colour/type parts of layout-spec.md, FohTheme.h, visual-directions/FohThemes.h. Structure (sidebar, panels, docking) stays per workspaces/ws-component-spec.md.

## 1 Principles
Function first. One hero per workspace (the plot). Blue = "you can click this". Orange needle = "live measurement / brand". Status always colour + shape + word.

## 2 Colour roles (Dark / Sunlight)
| Token | Dark | Sunlight | Use |
|---|---|---|---|
| surface.window | #141210 | #FFFFFF | window bg |
| surface.plotWell | #100E0C | #FAFAF8 | plot area |
| surface.panel | #1E1B18 | #F4F2EE | panels, sidebar |
| surface.raised | #2A2622 | #E8E5DF | headers, toolbar, pills |
| surface.control | #3A3630 | #FFFFFF | buttons/fields |
| separator | #FFF4E0 @12% | #1C1A17 @40% | hairlines |
| grid | #FFF4E0 @7% | #1C1A17 @18% | plot grid |
| text.primary | #FFF4E0 (Meter Ivory) | #1C1A17 (ink) | |
| text.secondary | #FFF4E0 @62% | #1C1A17 @78% | labels |
| text.tertiary | #FFF4E0 @40% (non-essential only) | #1C1A17 @62% | axis ticks |
| control.accent | #0A84FF | #0040DD | buttons, focus, selection |
| brand.needle | #FF6A3D | #C2410C | live trace, logo, active indicator |
| status.good / caution / poor | #30D158 / #FFD60A / #FF453A | #006B1F / #7A5A00 / #B0000A | always + ●/▲/■ glyph + word |
| trace.slots 1-8 | Ivory, #0A84FF, #BF5AF2, #64D2FF, #FFD60A, #30D158, #FF9F0A, #FF375F (+ dash patterns solid, 6-3, 2-2, 8-2-2-2 repeating) | darkened ≥4.5:1 on white | stored traces, named in UI |
Rules: no literal colours outside FohThemes.h; text ≥4.5:1, indicators ≥3:1 in both themes; no large pure-white areas in Dark.

## 3 Type (SF Pro → Inter fallback; numbers SF Mono → JetBrains Mono; Barlow Condensed = wordmark image only, never live UI text)
| Role | Dark pt | Sunlight pt | Weight |
|---|---|---|---|
| caption (axis ticks) | 11 | 12 | Regular, mono for numbers |
| label | 12 | 13 | Medium |
| body | 13 | 15 | Regular |
| title (panel header) | 13 | 15 | Semibold |
| heading (sheet/guide step) | 17 | 19 | Semibold |
| valueMd | 20 mono | 24 mono | Medium, tabular |
| valueLg (hero readout) | 34 mono | 42 mono | Medium, tabular |
No other sizes; no `size+1` arithmetic; no ALL CAPS except 2–3 letter units.

## 4 Spacing, radii, strokes
Grid 4. Tokens: xs 4, sm 8, md 12, lg 16, xl 24. Panel inset 12; control gap 8; group gap 16; control height 24 (row 28); toolbar 44; panel header 28; tab 26. Radii: control 6, panel 8, pill 10 (full), tag 4. Strokes: hairline 1 px physical (0.5 pt @2x Dark, 1 pt Sunlight); trace live 2 / 3.5 Sunlight; stored 1.5 / 2.5; focus ring 2 px control.accent.

## 5 Component rules
- Toolbar: lockup (20 pt high) · workspace switcher · spacer · status pills · Pause/Resume measuring · Setup · Workspace ▾. Everything else in menus.
- Panel toolbar: max one row; FlexBox; overflow into "⋯" menu; advanced groups in disclosures.
- Buttons: one primary (accent fill) per panel; verbs; no trailing glyphs; destructive in poor colour only in confirm sheets.
- Pills: glyph + word + value, clickable when there's an action.
- Banners (errors/permission): icon, one sentence, one action button, dismiss.
- Empty states: what's missing + next action ("Start pink noise, then press Find.").
- Plots: legend chips top-left, note top-right in caption/secondary; live trace = brand.needle.
- Generator: off by default, −20 dBFS default, "Output on" ▲ pill while running.
- States: hover (+6% overlay), pressed (+12%), disabled (40% opacity + no hover), focus ring, selected (accent).

## 6 Copy style & glossary
US English, sentence case, "…" for dialogs, no "+" for "and", no internal codes (W1–W7, build #, Smaart in titles), numbers with units and a thin space ("3.2 ms", "−20 dBFS").
| Use | Not |
|---|---|
| Store trace | Capture (for traces), Save trace |
| Capture position (multi-mic) | Store |
| Find delay / Lock delay | Insert delay, Find + lock, Use this delay, Use found |
| Reference delay | ref delay, arrival delay, offset |
| Mic calibration / SPL calibration / Interface correction | Cal, cal applied |
| Pause measuring / Resume measuring | Stop / Start |
| Show Monitor | W7 Show Monitoring |
| Coherence | coh |
| Dark / Sunlight (appearance) | Pro Dark, Dark Theme, Pro Sunlight |
| Workspace | layout (in user copy) |

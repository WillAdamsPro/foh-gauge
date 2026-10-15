#include "Widgets.h"
#if JUCE_MAC
#import <AppKit/AppKit.h>
#endif
#if __has_include("BinaryData.h")
 #include "BinaryData.h"
 #define FOH_HAS_FONTS 1
#endif
using namespace juce;
namespace C = foh::ui::col;
namespace D = foh::ui::dim;
namespace T = foh::theme::type;

juce::String foh::theme::systemFontName() {
   #if JUCE_MAC
    return juce::String::fromUTF8([[NSFont systemFontOfSize: NSFont.systemFontSize].familyName UTF8String]);
   #elif JUCE_WINDOWS
    return "Segoe UI";
   #else
    return juce::Font::getDefaultSansSerifFontName();
   #endif
}

// ===================== Look and feel =====================
FohLookAndFeel::FohLookAndFeel() {
#ifdef FOH_HAS_FONTS
    if (!regular) {
        regular  = Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
        semibold = Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
        bold     = Typeface::createSystemTypefaceFor(BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize);
    }
#endif
    applyTheme();
}
void FohLookAndFeel::applyTheme() {
    appliedLight = foh::ui::theme().isLight;
    setColour(ResizableWindow::backgroundColourId, C::background());
    setColour(Label::textColourId, C::textPrimary());
    setColour(TextButton::buttonColourId, C::controlBackground()); setColour(TextButton::textColourOffId, C::textBody());
    setColour(TextButton::buttonOnColourId, C::navItemActiveBg()); setColour(TextButton::textColourOnId, C::textStrong());
    setColour(ComboBox::backgroundColourId, C::controlBackground()); setColour(ComboBox::outlineColourId, C::pillBorder()); setColour(ComboBox::textColourId, C::textBody());
    setColour(PopupMenu::backgroundColourId, C::cardBackground()); setColour(ListBox::backgroundColourId, C::cardBackground());
    setColour(Slider::thumbColourId, C::accent()); setColour(Slider::trackColourId, C::divider());
    setColour(Slider::textBoxTextColourId, C::textPrimary()); setColour(Slider::textBoxBackgroundColourId, C::pillBackground()); setColour(Slider::textBoxOutlineColourId, C::pillBorder());
    setColour(ComboBox::arrowColourId, C::textSecondary()); setColour(PopupMenu::textColourId, C::textPrimary()); setColour(PopupMenu::highlightedBackgroundColourId, C::accent()); setColour(PopupMenu::highlightedTextColourId, C::textOnAccent());
    setColour(TextEditor::backgroundColourId, C::controlBackground()); setColour(TextEditor::textColourId, C::textPrimary()); setColour(TextEditor::outlineColourId, C::pillBorder());
    setColour(ListBox::textColourId, C::textPrimary()); setColour(ToggleButton::textColourId, C::textPrimary()); setColour(ToggleButton::tickColourId, C::accent());
    setColour(ToggleButton::tickDisabledColourId, C::textSecondary());
}
AlertWindow* FohLookAndFeel::createAlertWindow(const String& title, const String& message,
    const String& button1, const String& button2, const String& button3,
    MessageBoxIconType icon, int numButtons, juce::Component* associated) {
    auto* alert = LookAndFeel_V4::createAlertWindow(title, message, button1, button2, button3,
        icon, numButtons, associated);
#if JUCE_MAC
    // Scoped notices may be replaced or closed with their owner immediately.
    // AppKit's automatic visibility animation must not outlive the JUCE peer.
    if (auto* peer = alert->getPeer())
        [(NSView*) peer->getNativeHandle() window].animationBehavior = NSWindowAnimationBehaviorNone;
#endif
    return alert;
}
Typeface::Ptr FohLookAndFeel::getTypefaceForFont(const Font& f) {
    // The bundled Inter typeface remains available as a fallback; live UI uses the system font.
    if (regular != nullptr && foh::ui::uiFontName() == "Inter" && f.getTypefaceName() == "Inter") {
        auto st = f.getTypefaceStyle();
        if (st.containsIgnoreCase("Bold")) return bold;
        if (st.containsIgnoreCase("SemiBold")) return semibold;
        return regular;
    }
    return LookAndFeel_V4::getTypefaceForFont(f);
}
void FohLookAndFeel::refreshThemeFonts(juce::Component& root) {
    const auto oldSizes = (appliedLight ? foh::theme::proSunlight() : foh::theme::proDark()).type.sizes;
    const auto& newSizes = foh::ui::theme().type.sizes;
    auto refresh = [&](Font f) {
        for (size_t i = 0; i < oldSizes.size(); ++i)
            if (std::abs(f.getHeight() - oldSizes[i]) < 0.01f) return f.withHeight(newSizes[i]);
        return f;
    };
    if (auto* label = dynamic_cast<Label*>(&root)) label->setFont(refresh(label->getFont()));
    if (auto* editor = dynamic_cast<TextEditor*>(&root)) editor->applyFontToAllText(refresh(editor->getFont()));
    for (auto* child : root.getChildren()) refreshThemeFonts(*child);
}
Font FohLookAndFeel::numeric(foh::theme::type::Role role, bool b) {
    auto mono = foh::ui::theme().type.numeric;
    if (mono.isEmpty()) {
       #if JUCE_MAC
        static const String macMono = Font::findAllTypefaceNames().contains("SF Mono") ? "SF Mono" : "Menlo";
        mono = macMono;
       #elif JUCE_WINDOWS
        mono = "Consolas";
       #else
        mono = Font::getDefaultMonospacedFontName();
       #endif
    }
    return Font(FontOptions(mono, foh::ui::theme().type.sizes[role], Font::plain).withStyle(b ? "Bold" : "Medium"));
}
Font FohLookAndFeel::font(foh::theme::type::Role role, bool b, bool sb) {
    if (role == T::valueMd || role == T::valueLg) return numeric(role, b);
    const auto style = (role == T::title || role == T::heading) ? "Semibold" : b ? "Bold" : sb ? "Semibold"
        : role == T::label ? "Medium" : "Regular";
    return Font(FontOptions(foh::ui::uiFontName(), foh::ui::theme().type.sizes[role], Font::plain).withStyle(style));
}
void FohLookAndFeel::drawButtonBackground(Graphics& g, Button& b, const Colour& bgc, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    bool primary = b.getProperties()["primary"];
    if (primary) { g.setColour(C::accent().brighter(over ? 0.1f : 0.f).darker(down ? 0.2f : 0.f)); g.fillRoundedRectangle(r, D::radiusButton()); return; }
    g.setColour(b.getToggleState() ? C::navItemActiveBg() : bgc.brighter(over ? 0.05f : 0.f));
    g.fillRoundedRectangle(r, D::radiusButton());
    g.setColour(C::ghostBorder()); g.drawRoundedRectangle(r, D::radiusButton(), D::hairline());
}
void FohLookAndFeel::drawCallOutBoxBackground(CallOutBox&, Graphics& g, const Path& path, Image&) {
    g.setColour(C::cardBackground()); g.fillPath(path);
    g.setColour(C::border()); g.strokePath(path, PathStrokeType(D::hairline()));
}
void makePrimary(TextButton& b) { b.getProperties().set("primary", true); b.setColour(TextButton::textColourOffId, C::textOnAccent()); }

// ===================== Plot =====================
float PlotComponent::xOf(double f) const { return (float) (plotX() + plotW() * (std::log10(std::clamp(f, fMin, fMax)) - std::log10(fMin)) / (std::log10(fMax) - std::log10(fMin))); }
float PlotComponent::yOf(double v) const { return (float) (plotY() + plotH() * (yMax - jlimit(yMin, yMax, v)) / (yMax - yMin)); }
int PlotComponent::legendHeight() const {
    auto font = FohLookAndFeel::font(T::body);
    int x = 0, rows = 1, width = jmax(1, plotW());
    auto item = [&](const String& label, int extra) {
        int w = jmin(width, GlyphArrangement::getStringWidthInt(font, label) + extra);
        if (x && x + w > width) { ++rows; x = 0; }
        x += w + 8;
    };
    for (const auto& s : series) if (s.label.isNotEmpty()) item(s.label, 40);
    for (const auto& chip : legendChips) item(chip, 16);
    return rows * 24;
}
void PlotComponent::paint(Graphics& g) {
    if (topLegend) legendRowsHeight = legendHeight();
    // Workspace collapse/reparenting and UI scaling can briefly leave no room
    // after axis/legend padding. Do not submit negative drawing dimensions.
    if (plotW() <= 0 || plotH() <= 0) return;
    if (!std::isfinite(fMin) || !std::isfinite(fMax) || fMin <= 0 || fMax <= fMin
        || !(std::log10(fMax) > std::log10(fMin))
        || !std::isfinite(yMin) || !std::isfinite(yMax) || yMax <= yMin
        || !std::isfinite(yMax - yMin) || !std::isfinite(yStep) || yStep <= 0) {
        g.setColour(C::textAxis()); g.drawText("Invalid plot range", getLocalBounds(), Justification::centred); return;
    }
    auto validRange = [](double lo, double hi) { return std::isfinite(lo) && std::isfinite(hi) && lo > 0 && hi > lo; };
    auto validPoint = [this](double f, double v) {
        return std::isfinite(f) && f >= fMin && f <= fMax && std::isfinite(v)
            && std::fpclassify(v) != FP_SUBNORMAL;
    };
    auto pr = Rectangle<float>((float) plotX(), (float) plotY(), (float) plotW(), (float) plotH());
    g.setColour(C::plotWell()); g.fillRect(pr); // A2: darker plot well
    g.setFont(FohLookAndFeel::numeric(T::caption, false));
    for (auto& s : shade) { if (!validRange(s.first, s.second)) continue; float a = xOf(s.first), b = xOf(s.second); g.setColour(C::accent().withAlpha(C::crossoverBandAlpha())); g.fillRect(a, pr.getY(), b - a, pr.getHeight());
        if (shadeLabel.isNotEmpty()) { g.setColour(C::accent()); g.setFont(FohLookAndFeel::font(T::label)); g.drawText(shadeLabel, (int) a - 40, (int) pr.getY() + 10, (int) (b - a) + 80, 14, Justification::centred); g.setFont(FohLookAndFeel::numeric(T::caption, false)); } }
    for (auto& a : annotations) { if (!validRange(a.lo, a.hi)) continue; float x0 = xOf(a.lo), x1 = xOf(a.hi); // layout-spec: warning @ annotationBandAlpha, label at band right + 6, y 30
        g.setColour(C::warning().withAlpha(C::annotationBandAlpha())); g.fillRect(x0, pr.getY(), x1 - x0, pr.getHeight());
        if (a.label.isNotEmpty()) { g.setColour(C::warning()); g.setFont(FohLookAndFeel::font(T::label)); g.drawText(a.label, (int) x1 + 6, (int) pr.getY() + 30 - 7, 160, 14, Justification::left); g.setFont(FohLookAndFeel::numeric(T::caption, false)); } }
    if (showLegend && !topLegend) { int x = plotX() + 14; int y = (int) pr.getBottom() + 24 + 4; g.setFont(FohLookAndFeel::font(T::body));
        for (auto& s : series) { if (s.label.isEmpty()) continue; g.setColour(s.c);
            Path key; key.startNewSubPath((float)x, (float)y + 7); key.lineTo((float)x + 18, (float)y + 7);
            PathStrokeType stroke(s.width);
            if (s.dash) { Path dashed; stroke.createDashedStroke(dashed, key, s.dash, s.dashCount); g.fillPath(dashed); }
            else g.strokePath(key, stroke);
            int tw = GlyphArrangement::getStringWidthInt(g.getCurrentFont(), s.label); g.setColour(C::textBody()); g.drawText(s.label, x + 24, y, tw + 4, 16, Justification::left); x += 24 + tw + 18; }
        g.setFont(FohLookAndFeel::numeric(T::caption, false)); }
    g.setColour(C::border());
    for (double f : {20., 50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000.}) { if (f < fMin || f > fMax) continue;
        float x = xOf(f); g.drawVerticalLine((int) x, pr.getY(), pr.getBottom());
        g.setColour(C::textAxis()); g.drawText(f >= 1000 ? String((int) (f / 1000)) + "k" : String((int) f), (int) x - 15, (int) pr.getBottom() + 4, 30, 14, Justification::centred); g.setColour(C::border()); }
    // Bound tick work even for corrupt saved ranges or a step too small to advance.
    for (int tick = 0; tick < 256; ++tick) { double v = yMin + tick * yStep; if (!std::isfinite(v) || v > yMax) break; float y = yOf(v); g.drawHorizontalLine((int) y, pr.getX(), pr.getRight());
        g.setColour(C::textAxis()); g.drawText(yStep < 1 ? String(v, 1) : String(v, 0), 0, (int) y - 7, plotX() - 6, 14, Justification::right); g.setColour(C::border()); }
    if (topLegend) {
        g.setFont(FohLookAndFeel::font(T::body));
        int x = plotX(), y = 0;
        auto chip = [&](const String& label, Colour colour, const Series* s) {
            int width = jmin(plotW(), GlyphArrangement::getStringWidthInt(g.getCurrentFont(), label) + (s ? 40 : 16));
            if (x > plotX() && x + width > (int) pr.getRight()) { x = plotX(); y += 24; }
            auto box = Rectangle<int>(x, y, width, 20);
            g.setColour(C::pillBackground()); g.fillRoundedRectangle(box.toFloat(), D::radiusTag());
            if (s) {
                g.setColour(colour);
                Path key; key.startNewSubPath((float)x + 8, (float)y + 9); key.lineTo((float)x + 26, (float)y + 9);
                PathStrokeType stroke(s->width);
                if (s->dash) { Path dashed; stroke.createDashedStroke(dashed, key, s->dash, s->dashCount); g.fillPath(dashed); }
                else g.strokePath(key, stroke);
            }
            g.setColour(C::textSecondary()); g.drawText(label, box.withTrimmedLeft(s ? 32 : 8).withTrimmedRight(8), Justification::centredLeft);
            x += width + 8;
        };
        for (const auto& s : series) if (s.label.isNotEmpty()) chip(s.label, s.c, &s);
        for (const auto& label : legendChips) chip(label, C::textSecondary(), nullptr);
        g.setFont(FohLookAndFeel::numeric(T::caption, false));
    }
    if (note.isNotEmpty()) {
        g.setColour(C::textSecondary());
        auto bounds = topLegend ? Rectangle<int>(plotX(), legendRowsHeight, plotW(), 20)
                                : Rectangle<int>((int)pr.getX(), (int)pr.getY() + 2, (int)pr.getWidth() - 4, 14);
        g.drawText(note, bounds, Justification::right);
    }
    if (topLegend && yLabel.isNotEmpty()) { g.setColour(C::textSecondary()); g.drawText(yLabel, plotX(), getHeight() - 20, plotW(), 20, Justification::centred); }
    if (centreNote.isNotEmpty()) {
        AttributedString message; message.setJustification(Justification::centred);
        message.append(centreNote, FohLookAndFeel::font(T::body), C::textSecondary());
        TextLayout layout; layout.createLayout(message, jmax(1.0f, pr.getWidth() - 24));
        auto area = Rectangle<float>(pr.getX() + 12, pr.getCentreY() - layout.getHeight() / 2, pr.getWidth() - 24, layout.getHeight());
        g.setColour(C::plotWell()); g.fillRoundedRectangle(area.expanded(4), D::radiusTag()); layout.draw(g, area);
    }
    if (exampleData) { // top-right, ~103x20, 11 px warning text, 1 px warning border, radius 4 (layout-spec)
        auto tag = Rectangle<float>(pr.getRight() - 109, pr.getY() + 6, 103, 20);
        g.setColour(C::warning()); g.drawRoundedRectangle(tag.reduced(0.5f), D::radiusTag(), D::hairline());
        g.setFont(FohLookAndFeel::font(T::caption, false, true)); g.drawText("EXAMPLE DATA", tag, Justification::centred); g.setFont(FohLookAndFeel::numeric(T::caption, false)); }
    if (!lowCoh.empty()) { auto hc = C::textAxis().withAlpha(foh::ui::theme().trace.lowCoherenceHatchAlpha);
        if (hatchTile.isNull() || hatchFor != hc) { hatchFor = hc; hatchTile = Image(Image::ARGB, 8, 8, true); Graphics hg(hatchTile); hg.setColour(hc);
            hg.drawLine(0, 8, 8, 0, 1.0f); hg.drawLine(-4, 4, 4, -4, 1.0f); hg.drawLine(4, 12, 12, 4, 1.0f); }
        Graphics::ScopedSaveState hs(g); g.setTiledImageFill(hatchTile, 0, 0, 1.0f);
        for (auto& r : lowCoh) { if (!validRange(r.first, r.second)) continue; float a = jmax(pr.getX(), xOf(r.first)), b = jmin(pr.getRight(), xOf(r.second)); if (b > a) g.fillRect(a, pr.getY(), b - a, pr.getHeight()); } }
    {
        Graphics::ScopedSaveState ss(g); g.reduceClipRegion(pr.toNearestInt());
        for (const auto& band : toleranceBands) {
            if (band.f.size() < 2 || band.lower.size() != band.f.size() || band.upper.size() != band.f.size()) continue;
            bool valid = true;
            for (size_t i = 0; i < band.f.size(); ++i)
                if (!std::isfinite(band.f[i]) || band.f[i] <= 0 || (i && band.f[i] <= band.f[i - 1])
                    || !std::isfinite(band.lower[i]) || !std::isfinite(band.upper[i]) || band.lower[i] > band.upper[i]) valid = false;
            if (!valid) continue;
            Path area;
            area.startNewSubPath(xOf(band.f.front()), yOf(band.upper.front()));
            for (size_t i = 1; i < band.f.size(); ++i) area.lineTo(xOf(band.f[i]), yOf(band.upper[i]));
            for (size_t i = band.f.size(); i-- > 0;) area.lineTo(xOf(band.f[i]), yOf(band.lower[i]));
            area.closeSubPath(); g.setColour(band.colour.withAlpha(0.15f)); g.fillPath(area);
        }
        if (bars) for (auto& s : series) { if (s.dash) continue; g.setColour(C::rtaBar());
            for (size_t i = 0; i < s.v.size() && i < s.f.size() && i < barLo.size() && i < barHi.size(); ++i) {
                if (!validPoint(s.f[i], s.v[i]) || !validRange(barLo[i], barHi[i]) || (i < s.gap.size() && s.gap[i])) continue;
                float a = xOf(std::max(fMin, barLo[i])) + 1, b = xOf(std::min(fMax, barHi[i])) - 1, y = yOf(s.v[i]);
                if (b > a) g.fillRect(a, y, b - a, pr.getBottom() - y);
            }
        }
        for (auto& s : series) {
            Path p; bool started = false; double prev = 0;
            for (size_t i = 0; i < s.f.size() && i < s.v.size(); ++i) { double f = s.f[i]; if (!validPoint(f, s.v[i]) || f > s.maxF) { started = false; continue; }
                if (i < s.gap.size() && s.gap[i]) { started = false; continue; } // blanked bin: gap in the trace
                float x = xOf(f), y = yOf(s.v[i]);
                bool jump = started && wrapPhase && std::abs(s.v[i] - prev) > 180;
                if (!started || jump) { p.startNewSubPath(x, y); started = true; } else p.lineTo(x, y); prev = s.v[i]; }
            if (!bars && !s.dash && foh::ui::theme().trace.glow) { g.setColour(s.c.withMultipliedAlpha(0.22f)); g.strokePath(p, PathStrokeType(s.width * 3.5f, PathStrokeType::curved, PathStrokeType::rounded)); } // glow = second wide stroke, no shadow effects
            g.setColour(s.c); PathStrokeType st(bars && !s.dash ? D::traceSecondary() : s.width, PathStrokeType::curved, PathStrokeType::rounded);
            if (s.dash) { Path d; st.createDashedStroke(d, p, s.dash, s.dashCount); g.fillPath(d); } else g.strokePath(p, st);
        }
    }
    bool adviceVisible = false;
    if (showLowCohCallout && !lowCohDismissed && !lowCoh.empty()) {
        double widest = 0;
        for (auto region : lowCoh) if (validRange(region.first, region.second))
            widest = jmax(widest, (double)(xOf(region.second) - xOf(region.first)));
        if (widest > 0 && widest < pr.getWidth() * 0.8f) {
            const String title = "Low coherence: don't EQ here";
            const String detail = "Likely a reflection at the mic";
            auto titleFont = FohLookAndFeel::font(T::body, true);
            auto detailFont = FohLookAndFeel::font(T::caption);
            int textWidth = jmax(GlyphArrangement::getStringWidthInt(titleFont, title), GlyphArrangement::getStringWidthInt(detailFont, detail));
            // Wrap narrow panels instead of clipping the advice.
            int width = jmin((int)pr.getWidth() - 16, textWidth + 64);
            int lines = width < textWidth + 64 ? 2 : 1;
            auto box = Rectangle<int>((int)pr.getRight() - width - 8, (int)pr.getY() + 8, width, lines * 36 + 12);
            g.setColour(C::cardBackground()); g.fillRoundedRectangle(box.toFloat(), D::radiusButton());
            g.setColour(C::warning()); g.drawRoundedRectangle(box.toFloat().reduced(0.5f), D::radiusButton(), D::hairline());
            Path triangle; triangle.addTriangle((float)box.getX() + 16, (float)box.getY() + 12, (float)box.getX() + 23, (float)box.getY() + 25, (float)box.getX() + 9, (float)box.getY() + 25); g.fillPath(triangle);
            g.setColour(C::textPrimary()); g.setFont(titleFont);
            g.drawFittedText(title, box.getX() + 32, box.getY() + 6, width - 64, lines * 18, Justification::left, lines, 1.0f);
            g.setColour(C::textSecondary()); g.setFont(detailFont);
            g.drawFittedText(detail, box.getX() + 32, box.getY() + 6 + lines * 18, width - 64, lines * 18, Justification::left, lines, 1.0f);
            dismissLowCoh.setBounds(box.getRight() - 28, box.getY() + 4, 24, 24); adviceVisible = true;
        }
    }
    dismissLowCoh.setVisible(adviceVisible);
}
void logDecimate(const foh::TFResult& r, const std::vector<double>& v, std::vector<double>& fo, std::vector<double>& vo, std::vector<uint8_t>* gap) {
    fo.clear(); vo.clear(); if (gap) gap->clear();
    if (r.fftSize < 2 || (r.fftSize & (r.fftSize - 1)) || r.fftSize > (1 << 24)
        || !std::isfinite(r.sampleRate) || r.sampleRate < 1
        || v.size() != (size_t) r.fftSize / 2 + 1) return;
    const double bin = r.sampleRate / r.fftSize; size_t last = 0;
    for (double f = 18; f < 22000; f *= std::pow(2.0, 1.0 / 30)) {
        double index = std::round(f / bin);
        if (!std::isfinite(index) || index <= 0 || index >= (double) v.size()) continue;
        size_t k = (size_t) index; if (k == last) continue; last = k;
        bool invalid = !std::isfinite(v[k]) || std::fpclassify(v[k]) == FP_SUBNORMAL;
        if (invalid && !gap) continue;
        fo.push_back(k * bin); vo.push_back(invalid ? 0 : v[k]);
        if (gap) gap->push_back(invalid || r.isBlanked(k) ? 1 : 0);
    }
} // ~300 log-spaced points (spec)

// ===================== Impulse =====================
void ImpulseComponent::paint(Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(8, 6); auto axis = r.removeFromBottom(14);
    g.setFont(FohLookAndFeel::font(T::caption));
    if (res.arrivals.empty() || res.impulse.empty()) { g.setColour(C::textAxis()); g.drawText("Impulse: press Measure sweep or Find delay", r, Justification::centred); return; }
    double t0 = std::max(0.0, res.arrivals[0].delayMs - preMs), t1 = res.arrivals[0].delayMs + postMs;
    for (auto& a : res.arrivals) t1 = std::max(t1, a.delayMs + 2);
    auto xOf = [&](double ms) { return r.getX() + (float) ((ms - t0) / (t1 - t0)) * r.getWidth(); };
    double peak = 1e-30; for (double v : res.impulse) peak = std::max(peak, std::abs(v));
    g.setColour(C::grid()); g.drawHorizontalLine((int) r.getCentreY(), r.getX(), r.getRight());
    double step = (t1 - t0) > 30 ? 10 : 5;
    for (double t = std::ceil(t0 / step) * step; t <= t1; t += step) { float x = xOf(t); g.setColour(C::grid()); g.drawVerticalLine((int) x, r.getY(), r.getBottom());
        g.setColour(C::textAxis()); g.drawText(String((int) t) + " ms", (int) x - 24, (int) axis.getY(), 48, 14, Justification::centred); }
    const bool etc = showEtc && etcDb.size() == res.impulse.size();
    if (etc) {
        for (int db : {0, -20, -40, -60}) {
            float y = r.getY() + float(-db / 60.0) * r.getHeight();
            g.setColour(C::grid()); g.drawHorizontalLine(int(y), r.getX(), r.getRight());
            g.setColour(C::textAxis()); g.drawText(String(db) + " dB", int(r.getX()), int(y), 48, 14, Justification::left);
        }
    }
    // min/max per pixel column (display decimation only)
    Path p; int W = (int) r.getWidth(); size_t N = res.impulse.size();
    for (int px = 0; px < W; ++px) {
        double a = (t0 + (t1 - t0) * px / W) * 1e-3 * sampleRate, b = (t0 + (t1 - t0) * (px + 1) / W) * 1e-3 * sampleRate;
        size_t i0 = (size_t) std::max(0.0, std::floor(a)), i1 = std::min(N, (size_t) std::max(0.0, std::ceil(b)) + 1); if (i0 >= N) break;
        double lo = 0, hi = 0; for (size_t i = i0; i < i1; ++i) { lo = std::min(lo, res.impulse[i]); hi = std::max(hi, res.impulse[i]); }
        float x = r.getX() + px, yh = r.getCentreY() - (float) (hi / peak) * r.getHeight() * 0.48f, yl = r.getCentreY() - (float) (lo / peak) * r.getHeight() * 0.48f;
        if (etc) {
            double db = -120;
            for (size_t i = i0; i < i1; ++i) db = std::max(db, etcDb[i]);
            yh = yl = r.getY() + float(std::clamp(-db / 60.0, 0.0, 1.0)) * r.getHeight();
        }
        if (px == 0) p.startNewSubPath(x, yh); else p.lineTo(x, yh); p.lineTo(x, yl); }
    g.setColour(C::traceLive()); g.strokePath(p, PathStrokeType(1.0f));
    for (size_t k = 0; k < res.arrivals.size(); ++k) { auto& a = res.arrivals[k]; float x = xOf(a.delayMs);
        g.setColour(k == 0 ? C::accent() : C::warning()); g.fillRect(x - 0.5f, r.getY(), 1.0f, r.getHeight());
        String lab = "#" + String((int) k + 1) + " " + String(a.delayMs, 2) + " ms" + (k ? " (" + String(a.relLevelDb, 1) + " dB)" : String());
        g.setFont(FohLookAndFeel::font(T::label)); g.drawText(lab, (int) x + 4, (int) r.getY() + (int) k * 14, 200, 14, Justification::left); }
}

// ===================== Segmented control / sidebar =====================
void SegmentedControl::paint(Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.5f); float rad = D::radiusButton();
    g.setColour(C::pillBackground()); g.fillRoundedRectangle(r, rad);
    g.setColour(C::border()); g.drawRoundedRectangle(r, rad, D::hairline());
    float w = r.getWidth() / (float) jmax(1, labels.size());
    g.setFont(FohLookAndFeel::font(T::body, false, true));
    for (int i = 0; i < labels.size(); ++i) { auto seg = Rectangle<float>(r.getX() + i * w, r.getY(), w, r.getHeight());
        if (i == sel) { g.setColour(foh::ui::theme().isLight ? C::cardBackground() : C::textPrimary().withAlpha(0.22f)); g.fillRoundedRectangle(seg.reduced(2), rad - 1);
                        g.setColour(C::border()); g.drawRoundedRectangle(seg.reduced(2), rad - 1, D::hairline()); }
        else if (i > 0 && i - 1 != sel) { g.setColour(C::border()); g.fillRect(seg.getX(), seg.getY() + 5, D::hairline(), seg.getHeight() - 10); }
        g.setColour(i == sel ? C::textPrimary() : disabled[i] ? C::textAxis() : C::textSecondary()); g.drawText(labels[i], seg, Justification::centred); }
}
Path NavButton::glyphPath(Glyph gl, Rectangle<float> b) { // 16 px line glyphs, drawn as stroked paths
    Path p; float x = b.getX(), y = b.getY(), s = b.getWidth() / 16.0f; auto P = [&](float px, float py) { return juce::Point<float>(x + px * s, y + py * s); };
    switch (gl) {
        case Home: p.startNewSubPath(P(2, 8)); p.lineTo(P(8, 2.5f)); p.lineTo(P(14, 8)); p.startNewSubPath(P(4, 6.5f)); p.lineTo(P(4, 14)); p.lineTo(P(12, 14)); p.lineTo(P(12, 6.5f));
                   p.startNewSubPath(P(6.75f, 14)); p.lineTo(P(6.75f, 10)); p.lineTo(P(9.25f, 10)); p.lineTo(P(9.25f, 14)); break;
        case Transfer: p.startNewSubPath(P(1, 8)); p.cubicTo(P(3.5f, 2), P(5.5f, 2), P(8, 8)); p.cubicTo(P(10.5f, 14), P(12.5f, 14), P(15, 8)); break;
        case Rta: for (int i = 0; i < 4; ++i) { float hgt[] = {5, 10, 7, 12}; float cx = 2.5f + i * 3.7f; p.startNewSubPath(P(cx, 14)); p.lineTo(P(cx, 14 - hgt[i])); } break;
        case Guides: for (int i = 0; i < 3; ++i) { float cx = 3.5f + i * 4.5f, ky[] = {5, 10, 7}; p.startNewSubPath(P(cx, 2)); p.lineTo(P(cx, 14));
                         p.addEllipse(x + (cx - 1.6f) * s, y + (ky[i] - 1.6f) * s, 3.2f * s, 3.2f * s); } break;
    }
    return p;
}
void NavButton::paintButton(Graphics& g, bool over, bool) {
    auto r = getLocalBounds().toFloat().reduced(0.5f); bool on = getToggleState();
    if (on) { g.setColour(C::navItemActiveBg()); g.fillRoundedRectangle(r, D::radiusButton()); }
    else if (over) { g.setColour(C::textPrimary().withAlpha(0.06f)); g.fillRoundedRectangle(r, D::radiusButton()); }
    auto gb = Rectangle<float>(r.getX() + 10, r.getCentreY() - 8, 16, 16);
    g.setColour(on ? C::textPrimary() : C::accent()); g.strokePath(glyphPath(glyph, gb), PathStrokeType(1.4f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour(on ? C::textPrimary() : C::textPrimary().withMultipliedAlpha(0.9f)); g.setFont(FohLookAndFeel::font(T::body));
    g.drawText(getButtonText(), r.withTrimmedLeft(36), Justification::centredLeft);
}

// ===================== Status pills =====================
void StatusPills::paint(Graphics& g) {
    auto f = FohLookAndFeel::font(T::body); g.setFont(f); float x = 0, h = 30, y = (getHeight() - h) * 0.5f;
    for (auto& p : pills) {
        float tw = (float) GlyphArrangement::getStringWidthInt(f, p.text), w = 12 + (p.state >= 0 ? 8 + 6 : 0) + tw + 12;
        if (x + w > getWidth()) break;
        auto r = Rectangle<float>(x, y, w, h);
        g.setColour(C::pillBackground()); g.fillRoundedRectangle(r, h * 0.5f);
        g.setColour(C::pillBorder()); g.drawRoundedRectangle(r.reduced(0.5f), h * 0.5f, D::hairline());
        float tx = x + 12;
        if (p.state >= 0) { auto d = Rectangle<float>(tx, r.getCentreY() - 4, 8, 8);
            Colour c = p.state == Good ? C::ok() : p.state == Caution ? C::warning() : C::danger(); g.setColour(c);
            if (p.state == Good) g.fillEllipse(d);
            else if (p.state == Caution) { Path t; t.addTriangle(d.getCentreX(), d.getY(), d.getRight(), d.getBottom(), d.getX(), d.getBottom()); g.fillPath(t); }
            else g.fillRect(d);
            tx += 14; }
        g.setColour(C::textBody()); g.drawText(p.text, Rectangle<float>(tx, r.getY(), tw + 2, h), Justification::centredLeft);
        x += w + 8;
    }
}

void HelpPanel::setText(const String& copy) {
    sections.clear(); StringArray lines; lines.addLines(copy);
    String cur; std::vector<String> ps; for (auto& l : lines) { if (l.trim().isEmpty()) { if (cur.isNotEmpty()) ps.push_back(cur); cur = {}; } else cur << (cur.isEmpty() ? "" : " ") << l.trim(); }
    if (cur.isNotEmpty()) ps.push_back(cur);
    for (size_t i = 0; i < ps.size(); ++i) { auto& p = ps[i]; int colon = p.indexOfChar(':');
        if (i == 0) sections.push_back({"What am I looking at?", p});
        else if (colon > 0 && colon <= 20) sections.push_back({p.substring(0, colon), p.substring(colon + 1).trim()});
        else if (!sections.empty()) sections.back().second << "\n\n" << p; }
    repaint();
}
void HelpPanel::paint(Graphics& g) {
    g.fillAll(C::helperBackground());
    g.setColour(overlay ? C::cardBorderEmphasis() : C::border()); g.fillRect(0.f, 0.f, D::hairline(), (float) getHeight());
    auto r = getLocalBounds().reduced(14, 0).withTrimmedTop(12);
    g.setColour(C::textSecondary()); g.setFont(FohLookAndFeel::font(T::caption, true)); g.drawText(caption, r.removeFromTop(18), Justification::left); r.removeFromTop(6);
    for (size_t i = 0; i < sections.size(); ++i) { auto& hd = sections[i].first; auto& body = sections[i].second;
        if (i) { g.setColour(C::border()); g.fillRect(0.f, (float) r.getY(), (float) getWidth(), D::hairline()); r.removeFromTop(12); }
        g.setColour(C::textPrimary()); g.setFont(FohLookAndFeel::font(i == 0 ? T::title : T::body, true)); g.drawText(hd, r.removeFromTop(20), Justification::left); r.removeFromTop(2);
        AttributedString as; as.append(body, FohLookAndFeel::font(T::body), C::textSecondary()); as.setLineSpacing(3.0f);
        TextLayout tl; tl.createLayout(as, (float) r.getWidth()); auto bh = (int) std::ceil(tl.getHeight());
        tl.draw(g, r.removeFromTop(bh).toFloat()); r.removeFromTop(12);
        if (r.getHeight() <= 0) break; }
}
void card(Graphics& g, Rectangle<int> r) { card(g, r, C::border()); }
void card(Graphics& g, Rectangle<int> r, Colour border) {
    g.setColour(C::cardBackground()); g.fillRoundedRectangle(r.toFloat(), D::radiusCard());
    g.setColour(border); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), D::radiusCard(), D::hairline());
}

void FohLookAndFeel::drawDocumentWindowTitleBar(DocumentWindow& win, Graphics& g, int w, int h, int, int, const Image*, bool) {
    auto& t = foh::theme::ThemeManager::get().current();
    g.setColour(foh::theme::ws::panelHeaderBg(t)); g.fillRect(0, 0, w, h);
    g.setColour(t.surfaces.border); g.fillRect(0.f, h - t.borderWidth, (float) w, t.borderWidth);
    g.setColour(win.isActiveWindow() ? t.text.primary : t.text.secondary); g.setFont(font(foh::theme::ws::panelTitleSize, false, true));
    g.drawText(win.getName(), foh::theme::ws::headerPaddingX + 4, 0, w - 120, h, Justification::centredLeft, true);
}

#pragma once
// Look of the app: colours, the Onest typeface, mode colours and names, icons, and the
// Russian/English text switch (Russian unless Windows speaks another language).
#include "../brain/TitleRules.h"
#include "BinaryData.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace rv::ui
{
namespace col
{
const juce::Colour bg { 0xff101216 }, card { 0xff181b21 }, cardHi { 0xff20242c }, line { 0xff292e37 };
const juce::Colour text { 0xffedf0f4 }, dim { 0xff9098a6 }, faint { 0xff5c6370 };
const juce::Colour accent { 0xffffa14d }, accent2 { 0xffff6846 }, ok { 0xff46d08b }, warn { 0xffffc457 };
} // namespace col

inline juce::Colour kindColour (Kind k)
{
    switch (k)
    {
        case Kind::Music: return juce::Colour (0xffa38dff);
        case Kind::Speech: return juce::Colour (0xff43d0ad);
        case Kind::Movie: return juce::Colour (0xffff7a90);
        case Kind::Game: return juce::Colour (0xff5eb2ff);
        default: return col::accent;
    }
}

// Russian when the Windows display language or the regional format is Russian, Ukrainian,
// Belarusian or Kazakh (many people run English Windows with Russian regional settings).
bool russian();
inline juce::String L (const char* ruUtf8, const char* enUtf8) { return juce::String::fromUTF8 (russian() ? ruUtf8 : enUtf8); }

inline juce::String kindName (Kind k)
{
    switch (k)
    {
        case Kind::Music: return L ("Музыка", "Music");
        case Kind::Speech: return L ("Речь", "Speech");
        case Kind::Movie: return L ("Кино", "Movies");
        case Kind::Game: return L ("Игры", "Games");
        default: return L ("Авто", "Auto");
    }
}

struct Fonts
{
    juce::Typeface::Ptr regular, medium, semibold, bold;
    static Fonts& get()
    {
        static Fonts f;
        return f;
    }
private:
    Fonts()
    {
        regular = juce::Typeface::createSystemTypefaceFor (BinaryData::OnestRegular_ttf, BinaryData::OnestRegular_ttfSize);
        medium = juce::Typeface::createSystemTypefaceFor (BinaryData::OnestMedium_ttf, BinaryData::OnestMedium_ttfSize);
        semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::OnestSemiBold_ttf, BinaryData::OnestSemiBold_ttfSize);
        bold = juce::Typeface::createSystemTypefaceFor (BinaryData::OnestBold_ttf, BinaryData::OnestBold_ttfSize);
    }
};

// weight: 400 regular, 500 medium, 600 semibold, 700 bold
inline juce::Font font (float height, int weight = 400)
{
    auto& f = Fonts::get();
    auto tf = weight >= 700 ? f.bold : weight >= 600 ? f.semibold : weight >= 500 ? f.medium : f.regular;
    return juce::Font (juce::FontOptions (tf).withHeight (height));
}

// Mode icons, drawn into `r`.
inline void drawKindIcon (juce::Graphics& g, Kind k, juce::Rectangle<float> r, juce::Colour c, float stroke = 2.0f)
{
    g.setColour (c);
    juce::Path p;
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    switch (k)
    {
        case Kind::Music:   // a pair of notes
            p.addEllipse (x + w * 0.10f, y + h * 0.66f, w * 0.30f, h * 0.24f);
            p.addEllipse (x + w * 0.56f, y + h * 0.56f, w * 0.30f, h * 0.24f);
            g.fillPath (p);
            g.drawLine (x + w * 0.38f, y + h * 0.78f, x + w * 0.38f, y + h * 0.16f, stroke);
            g.drawLine (x + w * 0.84f, y + h * 0.68f, x + w * 0.84f, y + h * 0.08f, stroke);
            g.drawLine (x + w * 0.38f, y + h * 0.16f, x + w * 0.84f, y + h * 0.08f, stroke * 1.6f);
            break;
        case Kind::Speech:  // a speech bubble with lines
        {
            juce::Path b;
            b.addRoundedRectangle (x + w * 0.06f, y + h * 0.12f, w * 0.88f, h * 0.60f, w * 0.18f);
            b.startNewSubPath (x + w * 0.26f, y + h * 0.70f);
            b.lineTo (x + w * 0.20f, y + h * 0.92f);
            b.lineTo (x + w * 0.46f, y + h * 0.71f);
            g.strokePath (b, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (x + w * 0.26f, y + h * 0.34f, x + w * 0.74f, y + h * 0.34f, stroke);
            g.drawLine (x + w * 0.26f, y + h * 0.50f, x + w * 0.60f, y + h * 0.50f, stroke);
            break;
        }
        case Kind::Movie:   // a film frame with perforations
        {
            g.drawRoundedRectangle (x + w * 0.08f, y + h * 0.16f, w * 0.84f, h * 0.68f, w * 0.10f, stroke);
            for (int i = 0; i < 3; ++i)
            {
                g.fillRoundedRectangle (x + w * (0.18f + i * 0.24f), y + h * 0.24f, w * 0.12f, h * 0.09f, 1.5f);
                g.fillRoundedRectangle (x + w * (0.18f + i * 0.24f), y + h * 0.67f, w * 0.12f, h * 0.09f, 1.5f);
            }
            juce::Path t;
            t.addTriangle (x + w * 0.42f, y + h * 0.39f, x + w * 0.42f, y + h * 0.61f, x + w * 0.62f, y + h * 0.50f);
            g.fillPath (t);
            break;
        }
        case Kind::Game:    // a gamepad
        {
            juce::Path b;
            b.addRoundedRectangle (x + w * 0.04f, y + h * 0.26f, w * 0.92f, h * 0.50f, h * 0.24f);
            g.strokePath (b, juce::PathStrokeType (stroke));
            g.drawLine (x + w * 0.20f, y + h * 0.51f, x + w * 0.40f, y + h * 0.51f, stroke);
            g.drawLine (x + w * 0.30f, y + h * 0.41f, x + w * 0.30f, y + h * 0.61f, stroke);
            g.fillEllipse (x + w * 0.62f, y + h * 0.40f, w * 0.09f, w * 0.09f);
            g.fillEllipse (x + w * 0.74f, y + h * 0.52f, w * 0.09f, w * 0.09f);
            break;
        }
        default:            // auto: level bars evened out (like the app icon)
        {
            const float heights[5] = { 0.30f, 0.62f, 0.44f, 0.44f, 0.44f };
            const float bw = w * 0.12f, gap = w * 0.075f, total = 5 * bw + 4 * gap;
            float bx = x + (w - total) / 2;
            for (int i = 0; i < 5; ++i)
            {
                const float bh = h * heights[i];
                g.setColour (i < 2 ? c.withAlpha (0.45f) : c);
                g.fillRoundedRectangle (bx, y + (h - bh) / 2, bw, bh, bw / 2);
                bx += bw + gap;
            }
            break;
        }
    }
}

inline void drawCard (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill = col::card)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
}
} // namespace rv::ui

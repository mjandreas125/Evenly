#pragma once
// "Liquid glass" for the main window, built the way the web recreations of Apple's look are:
//   1. a scene behind the glass: soft coloured light plus a few sharp shapes (rings, lines, dots),
//      so that the glass has something to blur and to bend;
//   2. a blurred, slightly more saturated copy of the scene: the frosted body of every pane;
//   3. refraction: near the edge of a pane the picture behind it is pulled inward (a lens bevel),
//      with the red and blue channels shifted a little (chromatic aberration);
//   4. tint and gloss: a dark tint, a diagonal sheen, a soft lip of light on top, an inner shadow below;
//   5. a specular rim: bright where the light hits (top left, bottom right), faint elsewhere;
//   6. a soft drop shadow under the pane.
// Everything is baked into images once per size, so painting a frame costs one drawImage.
#include "Theme.h"
#include <vector>

namespace rv::ui
{
class GlassBackdrop
{
public:
    static constexpr float kRadius = 14.0f;       // corner radius of the panes (logical px)
    static constexpr float kBezel = 20.0f;        // how far the refraction reaches in from the edge

    juce::Image scene, blurred, panes;            // device-pixel sized
    std::vector<juce::Rectangle<int>> lastRects;
    int logicalW = 0, logicalH = 0;
    float scale = 1.0f;

    bool matches (int w, int h, float s, const std::vector<juce::Rectangle<int>>& rects) const
    {
        return w == logicalW && h == logicalH && std::abs (s - scale) < 0.01f && scene.isValid() && rects == lastRects;
    }

    void build (int w, int h, float s, const std::vector<juce::Rectangle<int>>& rects)
    {
        logicalW = w; logicalH = h; scale = s; lastRects = rects;
        const int pw = juce::jmax (1, (int) std::ceil ((float) w * s)), ph = juce::jmax (1, (int) std::ceil ((float) h * s));
        scene = juce::Image (juce::Image::ARGB, pw, ph, true);
        {
            juce::Graphics g (scene);
            g.addTransform (juce::AffineTransform::scale (s));
            drawScene (g, w, h);
        }
        blurred = scene.createCopy();
        blurAndSaturate (blurred, juce::jmax (4, (int) std::lround (26.0f * s)));
        panes = juce::Image (juce::Image::ARGB, pw, ph, true);
        {
            // soft shadows first: the panes are painted over them
            juce::Graphics g (panes);
            g.addTransform (juce::AffineTransform::scale (s));
            for (const auto& r : rects)
            {
                juce::Path p;
                p.addRoundedRectangle (r.toFloat(), kRadius);
                juce::DropShadow (juce::Colours::black.withAlpha (0.50f), 24, { 0, 9 }).drawForPath (g, p);
            }
        }
        for (const auto& r : rects) refractPane (r);
        juce::Graphics g (panes);
        g.addTransform (juce::AffineTransform::scale (s));
        for (const auto& r : rects) paintFinish (g, r.toFloat());
    }

    // The glass fill for a pane that moves (the equalizer sheet): blurred scene + the same finish, no refraction.
    void paintLive (juce::Graphics& g, juce::Rectangle<float> r, juce::Point<int> originInMain) const
    {
        juce::Path p;
        p.addRoundedRectangle (r, kRadius);
        // a cheap shadow above the sheet (a blurred one would be recomputed every frame of the animation)
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.50f), 0, r.getY(), juce::Colours::black.withAlpha (0.0f), 0, r.getY() - 26.0f, false));
        g.fillRect (r.getX() + 6.0f, r.getY() - 26.0f, r.getWidth() - 12.0f, 26.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (p);
            g.setColour (juce::Colour (0xff090b0f));      // solid underneath: the sheet must hide the cards behind it
            g.fillPath (p);
            const auto src = (r + originInMain.toFloat()) * scale;
            g.drawImage (blurred, (int) r.getX(), (int) r.getY(), (int) std::ceil (r.getWidth()), (int) std::ceil (r.getHeight()),
                         (int) src.getX(), (int) src.getY(), (int) std::ceil (src.getWidth()), (int) std::ceil (src.getHeight()));
        }
        paintFinish (g, r);
    }

private:
    // ---- 1. the scene ----
    static void drawScene (juce::Graphics& g, int w, int h)
    {
        g.fillAll (juce::Colour (0xff06080b));
        const float W = (float) w, H = (float) h, big = std::max (W, H);
        struct Blob { float x, y, r; juce::uint32 colour; float alpha; };
        const Blob blobs[] = {
            { 0.08f, 0.20f, 0.44f, 0xffffa14d, 0.30f }, { 0.52f, 0.05f, 0.38f, 0xff2fd3b3, 0.22f }, { 0.95f, 0.42f, 0.48f, 0xffa38dff, 0.28f },
            { 0.28f, 0.98f, 0.44f, 0xffff6846, 0.23f }, { 0.78f, 0.95f, 0.40f, 0xff5eb2ff, 0.21f }, { 0.50f, 0.50f, 0.32f, 0xff6a4bd8, 0.12f } };
        for (const auto& b : blobs)
        {
            const juce::Colour c (b.colour);
            g.setGradientFill (juce::ColourGradient (c.withAlpha (b.alpha), W * b.x, H * b.y, c.withAlpha (0.0f), W * b.x + big * b.r, H * b.y, true));
            g.fillEllipse (W * b.x - big * b.r, H * b.y - big * b.r, big * b.r * 2, big * b.r * 2);
        }
        // sharp shapes: they are what the glass blurs inside and bends at its edge
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        for (const auto& b : blobs)
            for (float k : { 0.10f, 0.19f, 0.29f })
                g.drawEllipse (W * b.x - big * k, H * b.y - big * k, big * k * 2, big * k * 2, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        for (float x = -H; x < W; x += 150.0f) g.drawLine (x, H, x + H * 0.7f, 0, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        for (float y = 14; y < H; y += 28)
            for (float x = 14; x < W; x += 28) g.fillRect (x, y, 1.6f, 1.6f);
    }

    // ---- 2. frosted body: three box blurs, then a little more colour ----
    static void blurAndSaturate (juce::Image& img, int radius)
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
        const int w = bd.width, h = bd.height;
        std::vector<float> buf ((size_t) w * (size_t) h * 3);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const auto px = bd.getPixelColour (x, y);
                float* o = &buf[((size_t) y * w + x) * 3];
                o[0] = px.getFloatRed(); o[1] = px.getFloatGreen(); o[2] = px.getFloatBlue();
            }
        std::vector<float> tmp ((size_t) std::max (w, h) * 3);
        auto pass = [&] (bool horizontal)
        {
            const int n = horizontal ? w : h, lines = horizontal ? h : w;
            for (int l = 0; l < lines; ++l)
            {
                auto at = [&] (int i) -> float* { return horizontal ? &buf[((size_t) l * w + i) * 3] : &buf[((size_t) i * w + l) * 3]; };
                float sum[3] = { 0, 0, 0 };
                for (int i = -radius; i <= radius; ++i) { const float* p = at (juce::jlimit (0, n - 1, i)); sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; }
                const float inv = 1.0f / (float) (radius * 2 + 1);
                for (int i = 0; i < n; ++i)
                {
                    for (int c = 0; c < 3; ++c) tmp[(size_t) i * 3 + (size_t) c] = sum[c] * inv;
                    const float* add = at (juce::jlimit (0, n - 1, i + radius + 1));
                    const float* sub = at (juce::jlimit (0, n - 1, i - radius));
                    for (int c = 0; c < 3; ++c) sum[c] += add[c] - sub[c];
                }
                for (int i = 0; i < n; ++i) { float* p = at (i); p[0] = tmp[(size_t) i * 3]; p[1] = tmp[(size_t) i * 3 + 1]; p[2] = tmp[(size_t) i * 3 + 2]; }
            }
        };
        for (int k = 0; k < 3; ++k) { pass (true); pass (false); }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float* o = &buf[((size_t) y * w + x) * 3];
                const float luma = 0.299f * o[0] + 0.587f * o[1] + 0.114f * o[2];
                auto sat = [&] (float c) { return juce::jlimit (0.0f, 1.0f, luma + (c - luma) * 1.45f); };
                bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (sat (o[0]), sat (o[1]), sat (o[2]), 1.0f));
            }
    }

    // ---- 3. refraction at the bevel; the pane's body is the blurred scene ----
    void refractPane (juce::Rectangle<int> lr)
    {
        const auto r = lr.toFloat() * scale;
        const float radius = kRadius * scale, bezel = kBezel * scale, reach = 11.0f * scale;
        const juce::Point<float> centre = r.getCentre();
        const juce::Point<float> half (r.getWidth() * 0.5f, r.getHeight() * 0.5f);
        const int x0 = juce::jmax (0, (int) std::floor (r.getX()) - 1), y0 = juce::jmax (0, (int) std::floor (r.getY()) - 1);
        const int x1 = juce::jmin (panes.getWidth(), (int) std::ceil (r.getRight()) + 1), y1 = juce::jmin (panes.getHeight(), (int) std::ceil (r.getBottom()) + 1);
        juce::Image::BitmapData out (panes, juce::Image::BitmapData::readWrite);
        const juce::Image::BitmapData sharp (scene, juce::Image::BitmapData::readOnly), soft (blurred, juce::Image::BitmapData::readOnly);
        auto sample = [] (const juce::Image::BitmapData& d, float fx, float fy, int channel)
        {
            const int x = juce::jlimit (0, d.width - 1, (int) fx), y = juce::jlimit (0, d.height - 1, (int) fy);
            const auto c = d.getPixelColour (x, y);
            return channel == 0 ? c.getFloatRed() : (channel == 1 ? c.getFloatGreen() : c.getFloatBlue());
        };
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
            {
                const juce::Point<float> p ((float) x + 0.5f, (float) y + 0.5f);
                const juce::Point<float> d = p - centre;
                const juce::Point<float> q (std::abs (d.x) - (half.x - radius), std::abs (d.y) - (half.y - radius));
                const float qx = std::max (q.x, 0.0f), qy = std::max (q.y, 0.0f);
                const float sdf = std::sqrt (qx * qx + qy * qy) + std::min (std::max (q.x, q.y), 0.0f) - radius;   // < 0 inside
                const float cover = juce::jlimit (0.0f, 1.0f, 0.5f - sdf);
                if (cover <= 0.0f) continue;
                const float inside = -sdf;
                float rgb[3];
                if (inside < bezel)
                {
                    // outward normal of the rounded rectangle
                    juce::Point<float> n;
                    if (q.x > 0 && q.y > 0) { const float l = std::sqrt (qx * qx + qy * qy); n = { (d.x < 0 ? -1.0f : 1.0f) * qx / l, (d.y < 0 ? -1.0f : 1.0f) * qy / l }; }
                    else if (q.x > q.y) n = { d.x < 0 ? -1.0f : 1.0f, 0.0f };
                    else n = { 0.0f, d.y < 0 ? -1.0f : 1.0f };
                    const float t = juce::jlimit (0.0f, 1.0f, inside / bezel);
                    const float shift = reach * (1.0f - t) * (1.0f - t);       // strongest at the very edge
                    const float mixSharp = 0.75f * (1.0f - t);
                    const float aberr[3] = { 1.18f, 1.0f, 0.82f };               // red bends more than blue
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        const float sx = p.x - n.x * shift * aberr[ch], sy = p.y - n.y * shift * aberr[ch];
                        rgb[ch] = sample (soft, sx, sy, ch) * (1.0f - mixSharp) + sample (sharp, sx, sy, ch) * mixSharp;
                    }
                }
                else for (int ch = 0; ch < 3; ++ch) rgb[ch] = sample (soft, p.x, p.y, ch);
                // premultiplied, over whatever (the shadow) is already there
                const auto dst = out.getPixelColour (x, y);
                const float da = dst.getFloatAlpha();
                const float oa = cover + da * (1.0f - cover);
                const float r2 = rgb[0] * cover + dst.getFloatRed() * da * (1.0f - cover);
                const float g2 = rgb[1] * cover + dst.getFloatGreen() * da * (1.0f - cover);
                const float b2 = rgb[2] * cover + dst.getFloatBlue() * da * (1.0f - cover);
                out.setPixelColour (x, y, juce::Colour::fromFloatRGBA (oa > 0 ? r2 / oa : 0.0f, oa > 0 ? g2 / oa : 0.0f, oa > 0 ? b2 / oa : 0.0f, oa));
            }
    }

    // ---- 4-5. tint, gloss, inner shadow and the specular rim ----
    public:
    static void paintFinish (juce::Graphics& g, juce::Rectangle<float> r, float radius = kRadius)
    {
        juce::Path p;
        p.addRoundedRectangle (r, radius);
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (p);
        g.setColour (juce::Colour (0xff0a0d13).withAlpha (0.64f));          // dark tint: text stays readable
        g.fillPath (p);
        g.setColour (juce::Colours::white.withAlpha (0.035f));              // frost
        g.fillPath (p);
        // the sheen: a diagonal band of light across the top left
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.17f), r.getX(), r.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), r.getX() + r.getWidth() * 0.50f, r.getY() + r.getHeight() * 0.55f, false));
        g.fillPath (p);
        // a lip of light along the top edge
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.13f), 0, r.getY(), juce::Colours::white.withAlpha (0.0f), 0, r.getY() + 30.0f, false));
        g.fillPath (p);
        // inner shadow at the bottom: the glass has thickness
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.30f), 0, r.getBottom(), juce::Colours::black.withAlpha (0.0f), 0, r.getBottom() - 38.0f, false));
        g.fillPath (p);
        // the specular rim: lit at the top left and the bottom right
        juce::ColourGradient rim (juce::Colours::white.withAlpha (0.75f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.45f), r.getRight(), r.getBottom(), false);
        rim.addColour (0.30, juce::Colours::white.withAlpha (0.14f));
        rim.addColour (0.70, juce::Colours::white.withAlpha (0.10f));
        g.setGradientFill (rim);
        g.strokePath (p, juce::PathStrokeType (1.6f));
        juce::Path inner;
        inner.addRoundedRectangle (r.reduced (2.2f), radius - 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.strokePath (inner, juce::PathStrokeType (1.0f));
    }
};
} // namespace rv::ui

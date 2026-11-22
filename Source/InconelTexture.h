/*
  ==============================================================================
    Superalloy — InconelTexture.h
    Procedural heat-tinted Inconel: oxide temper bands (straw → gold → bronze →
    violet → blue → steel-blue) flowing across a brushed satin base, with a soft
    overhead sheen. Deterministic (hash noise), no bitmaps, no photographs.
  ==============================================================================
*/
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <cstdint>
#include <array>

namespace superalloy::texture
{
    inline float hash2 (int x, int y) noexcept
    {
        uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) ((h ^ (h >> 16)) & 0xffffffu) / 16777215.0f;
    }

    inline float valueNoise (float x, float y) noexcept
    {
        const int xi = (int) std::floor (x), yi = (int) std::floor (y);
        const float fx = x - (float) xi, fy = y - (float) yi;
        const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
        const float a = hash2 (xi, yi),     b = hash2 (xi + 1, yi);
        const float c = hash2 (xi, yi + 1), d = hash2 (xi + 1, yi + 1);
        return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
    }

    inline float fbm (float x, float y) noexcept
    {
        float s = 0.0f, amp = 0.5f;
        for (int o = 0; o < 5; ++o) { s += amp * valueNoise (x, y); x *= 2.03f; y *= 2.03f; amp *= 0.5f; }
        return s;
    }

    struct RGB { float r, g, b; };

    // Temper (oxide-film interference) palette, in order of film thickness.
    inline RGB temper (float t) noexcept
    {
        static const std::array<RGB, 8> stops {{
            { 0.78f, 0.77f, 0.74f },   // bare satin nickel alloy
            { 0.85f, 0.76f, 0.50f },   // straw
            { 0.80f, 0.60f, 0.30f },   // gold
            { 0.60f, 0.38f, 0.22f },   // bronze
            { 0.45f, 0.24f, 0.40f },   // violet
            { 0.24f, 0.30f, 0.58f },   // deep blue
            { 0.40f, 0.56f, 0.72f },   // light blue
            { 0.55f, 0.60f, 0.64f },   // steel grey
        }};
        t = juce::jlimit (0.0f, 0.9999f, t) * (float) (stops.size() - 1);
        const int i = (int) t; const float f = t - (float) i;
        const auto& a = stops[(size_t) i]; const auto& b = stops[(size_t) i + 1];
        return { a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f };
    }

    inline juce::Image renderHeatTintedInconel (int w, int h)
    {
        juce::Image img (juce::Image::RGB, w, h, false);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

        // brushed grain: per-row streak value, smeared along x
        std::vector<float> rowGrain ((size_t) h);
        for (int y = 0; y < h; ++y) rowGrain[(size_t) y] = hash2 (17, y) - 0.5f;

        for (int y = 0; y < h; ++y)
        {
            const float v = (float) y / (float) (h - 1);
            for (int x = 0; x < w; ++x)
            {
                const float u = (float) x / (float) (w - 1);

                // heat front: bands sweep left→right, warped by slow noise
                const float warp = 0.22f * (fbm (u * 2.2f, v * 1.6f + 3.0f) - 0.5f);
                const float t = juce::jlimit (0.0f, 1.0f, 0.06f + 0.94f * (u + warp + 0.10f * std::sin (v * 3.1f)));
                RGB c = temper (t);

                // brushed satin: fine horizontal streaks + micro noise
                const float streak = 0.055f * rowGrain[(size_t) y]
                                   + 0.030f * (valueNoise ((float) x * 0.02f, (float) y * 1.7f) - 0.5f)
                                   + 0.015f * (hash2 (x, y) - 0.5f);

                // overhead sheen (soft highlight band) and edge falloff
                const float sheen = 0.10f * std::exp (-std::pow ((v - 0.28f) / 0.20f, 2.0f));
                const float vign = 1.0f - 0.18f * (std::pow (std::abs (u - 0.5f) * 2.0f, 3.0f) + std::pow (std::abs (v - 0.5f) * 2.0f, 3.0f));

                auto px = [&] (float ch) { return (juce::uint8) juce::jlimit (0, 255, (int) std::lround (255.0f * ((ch + streak + sheen) * vign))); };
                bd.setPixelColour (x, y, juce::Colour (px (c.r), px (c.g), px (c.b)));
            }
        }
        return img;
    }
}

#pragma once
// Minimal WAV reading (PCM 16/24/32, float 32) and writing (float 32) for the offline tools.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace rv::wav
{
struct Audio { int rate = 0, channels = 0; std::vector<float> data; };   // interleaved

inline bool read (const std::string& path, Audio& a)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    std::vector<uint8_t> b;
    std::fseek (f, 0, SEEK_END);
    b.resize ((size_t) std::ftell (f));
    std::fseek (f, 0, SEEK_SET);
    const size_t got = std::fread (b.data(), 1, b.size(), f);
    std::fclose (f);
    if (got != b.size() || b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0) return false;
    uint16_t tag = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* data = nullptr;
    size_t dataLen = 0;
    for (size_t p = 12; p + 8 <= b.size();)
    {
        uint32_t size;
        std::memcpy (&size, &b[p + 4], 4);
        if (std::memcmp (&b[p], "fmt ", 4) == 0)
        {
            std::memcpy (&tag, &b[p + 8], 2); std::memcpy (&ch, &b[p + 10], 2);
            std::memcpy (&rate, &b[p + 12], 4); std::memcpy (&bits, &b[p + 22], 2);
            if (tag == 0xFFFE && size >= 26) std::memcpy (&tag, &b[p + 32], 2);   // WAVE_FORMAT_EXTENSIBLE subformat
        }
        else if (std::memcmp (&b[p], "data", 4) == 0)
        {
            data = &b[p + 8];
            dataLen = std::min<size_t> (size, b.size() - p - 8);
        }
        p += 8 + size + (size & 1);
    }
    if (! data || ch == 0) return false;
    a.rate = (int) rate;
    a.channels = ch;
    const size_t bytes = bits / 8, n = dataLen / bytes;
    a.data.resize (n);
    for (size_t i = 0; i < n; ++i)
    {
        const uint8_t* s = data + i * bytes;
        if (tag == 3 && bits == 32) { float v; std::memcpy (&v, s, 4); a.data[i] = v; }
        else if (bits == 16) { int16_t v; std::memcpy (&v, s, 2); a.data[i] = v / 32768.0f; }
        else if (bits == 24) { int32_t v = (int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24); a.data[i] = (float) (v / 2147483648.0); }
        else if (bits == 32) { int32_t v; std::memcpy (&v, s, 4); a.data[i] = (float) (v / 2147483648.0); }
        else return false;
    }
    return true;
}

inline bool write (const std::string& path, const Audio& a)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    const uint32_t dataBytes = (uint32_t) (a.data.size() * 4), riff = 36 + dataBytes, fmtLen = 16, rate = (uint32_t) a.rate;
    const uint16_t tag = 3, ch = (uint16_t) a.channels, align = (uint16_t) (4 * a.channels), bits = 32;
    const uint32_t byteRate = rate * align;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&tag, 2, 1, f); std::fwrite (&ch, 2, 1, f); std::fwrite (&rate, 4, 1, f);
    std::fwrite (&byteRate, 4, 1, f); std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    std::fwrite (a.data.data(), 4, a.data.size(), f);
    std::fclose (f);
    return true;
}
} // namespace rv::wav

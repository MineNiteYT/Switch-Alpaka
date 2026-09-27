// gui.cpp - minimal software GUI for libnx (no SDL2)
#include "gui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace gui {
namespace {

// ---------------------------------------------------------------- framebuffer

Framebuffer g_fb;
bool        g_fbReady   = false;
bool        g_plReady   = false;
u32*        g_pix       = nullptr;
u32         g_stride    = 0;  // in pixels

// Blend `color` over *dst. `cov` (0..255) is extra coverage, e.g. glyph alpha.
inline void blend(u32* dst, u32 color, u32 cov) {
    u32 a = (cov * (color >> 24) + 127) / 255;
    if (a == 0) return;
    if (a == 255) {
        *dst = color | 0xFF000000u;
        return;
    }
    u32 d  = *dst;
    u32 ia = 255 - a;
    u32 r  = ((color & 0xFF) * a + (d & 0xFF) * ia + 127) / 255;
    u32 g  = (((color >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * ia + 127) / 255;
    u32 b  = (((color >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * ia + 127) / 255;
    *dst   = r | (g << 8) | (b << 16) | 0xFF000000u;
}

inline void plot(int x, int y, u32 color, u32 cov) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    blend(&g_pix[(size_t)y * g_stride + x], color, cov);
}

// ----------------------------------------------------------------------- font

struct Glyph {
    unsigned char* bmp = nullptr;
    int   w = 0, h = 0;
    int   xoff = 0, yoff = 0;
    float advance = 0.f;
};

struct FontFace {
    float scale    = 0.f;
    float extScale = 0.f;  // scale for the Nintendo extension (button icon) font
    int   ascent = 0;
    int   lineH  = 0;
    std::unordered_map<uint32_t, Glyph> cache;
};

stbtt_fontinfo g_info;      // standard system font (Latin text)
stbtt_fontinfo g_extInfo;   // Nintendo extension font (button icons, U+E000..U+F8FF)
bool           g_extReady = false;
FontFace       g_faces[4];

struct FontDiag {
    u32         stdRc = 0, extRc = 0;
    u64         stdSize = 0, extSize = 0;
    int         stdOffset = -2, extOffset = -2;
    bool        stdInit = false, extInit = false;
    std::string stdHdr = "-", extHdr = "-";
};
FontDiag g_diag;

std::string hdrHex(const unsigned char* d) {
    char b[16];
    std::snprintf(b, sizeof b, "%02X%02X%02X%02X", d[0], d[1], d[2], d[3]);
    return b;
}
const float    kPixelHeight[4] = {20.f, 26.f, 34.f, 48.f};

std::string hex(u32 v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%X", (unsigned)v);
    return b;
}

bool initFont(std::string& err) {
    Result rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc)) {
        err = "plInitialize failed (rc=" + hex(rc) + ")";
        return false;
    }
    g_plReady = true;

    PlFontData fd;
    rc = plGetSharedFontByType(&fd, PlSharedFontType_Standard);
    if (R_FAILED(rc)) {
        err = "plGetSharedFontByType failed (rc=" + hex(rc) + ")";
        return false;
    }

    const unsigned char* data = static_cast<const unsigned char*>(fd.address);
    int offset = stbtt_GetFontOffsetForIndex(data, 0);
    g_diag.stdRc = rc;
    g_diag.stdSize = fd.size;
    g_diag.stdOffset = offset;
    g_diag.stdHdr = hdrHex(data);
    g_diag.stdInit = offset >= 0 && stbtt_InitFont(&g_info, data, offset) != 0;
    if (!g_diag.stdInit) {
        err = "stb_truetype could not parse the system font";
        return false;
    }

    // The button icons (A, B, X, Y, L, R, +, ...) live in a separate shared font.
    // Not fatal if it is missing: icons then show up as "?".
    PlFontData ed;
    std::memset(&ed, 0, sizeof ed);
    g_diag.extRc = plGetSharedFontByType(&ed, PlSharedFontType_NintendoExt);
    if (R_SUCCEEDED(g_diag.extRc)) {
        const unsigned char* extData = static_cast<const unsigned char*>(ed.address);
        g_diag.extSize   = ed.size;
        g_diag.extHdr    = hdrHex(extData);
        g_diag.extOffset = stbtt_GetFontOffsetForIndex(extData, 0);
        if (g_diag.extOffset >= 0) {
            g_diag.extInit = stbtt_InitFont(&g_extInfo, extData, g_diag.extOffset) != 0;
            g_extReady = g_diag.extInit;
        }
    }

    for (int i = 0; i < 4; i++) {
        FontFace& f = g_faces[i];
        f.scale = stbtt_ScaleForPixelHeight(&g_info, kPixelHeight[i]);
        if (g_extReady) {
            // Give the icon font the same em size (in pixels) as the text font.
            const float emPx = f.scale / stbtt_ScaleForMappingEmToPixels(&g_info, 1.0f);
            f.extScale = stbtt_ScaleForMappingEmToPixels(&g_extInfo, emPx);
        }
        int asc, desc, gap;
        stbtt_GetFontVMetrics(&g_info, &asc, &desc, &gap);
        f.ascent = (int)std::lround(asc * f.scale);
        f.lineH  = (int)std::lround((asc - desc + gap) * f.scale);
    }
    return true;
}

// Decodes one UTF-8 code point and advances `p`.
uint32_t nextCodepoint(const char*& p) {
    unsigned char c = (unsigned char)*p++;
    if (c < 0x80) return c;

    int      extra;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0)      { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else return 0xFFFD;

    while (extra--) {
        unsigned char n = (unsigned char)*p;
        if ((n & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (n & 0x3F);
        p++;
    }
    return cp;
}

const Glyph& getGlyph(FontFace& f, uint32_t cp) {
    auto it = f.cache.find(cp);
    if (it != f.cache.end()) return it->second;

    Glyph g;
    const stbtt_fontinfo* info  = &g_info;
    float                 scale = f.scale;
    int                   gi    = 0;

    if (g_extReady && cp >= 0xE000 && cp <= 0xF8FF) {
        gi = stbtt_FindGlyphIndex(&g_extInfo, (int)cp);
        if (gi != 0) {
            info  = &g_extInfo;
            scale = f.extScale;
        }
    } else {
        gi = stbtt_FindGlyphIndex(&g_info, (int)cp);
    }
    if (gi == 0) gi = stbtt_FindGlyphIndex(&g_info, '?');  // visible "missing glyph" marker

    int adv, lsb;
    stbtt_GetGlyphHMetrics(info, gi, &adv, &lsb);
    g.advance = adv * scale;

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(info, gi, scale, scale, &x0, &y0, &x1, &y1);
    g.w = x1 - x0;
    g.h = y1 - y0;
    g.xoff = x0;
    g.yoff = y0;
    if (g.w > 0 && g.h > 0) {
        g.bmp = (unsigned char*)std::malloc((size_t)g.w * (size_t)g.h);
        if (g.bmp) {
            stbtt_MakeGlyphBitmap(info, g.bmp, g.w, g.h, g.w, scale, scale, gi);
        } else {
            g.w = g.h = 0;
        }
    }
    return f.cache.emplace(cp, g).first->second;
}

}  // namespace

// ----------------------------------------------------------------- lifecycle

bool init(std::string& err) {
    if (!initFont(err)) return false;

    NWindow* win = nwindowGetDefault();
    Result rc = framebufferCreate(&g_fb, win, W, H, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) {
        err = "framebufferCreate failed (rc=" + hex(rc) + ")";
        return false;
    }
    // Linear mode = normal cached CPU memory. Required for alpha blending,
    // because blending reads the destination pixels back.
    framebufferMakeLinear(&g_fb);
    g_fbReady = true;
    return true;
}

void shutdown() {
    for (int i = 0; i < 4; i++) {
        for (auto& kv : g_faces[i].cache) std::free(kv.second.bmp);
        g_faces[i].cache.clear();
    }
    if (g_fbReady) {
        framebufferClose(&g_fb);
        g_fbReady = false;
    }
    if (g_plReady) {
        plExit();
        g_plReady = false;
    }
}

void beginFrame(u32 clearColor) {
    u32 strideBytes = 0;
    g_pix    = static_cast<u32*>(framebufferBegin(&g_fb, &strideBytes));
    g_stride = strideBytes / sizeof(u32);
    fillRect(0, 0, W, H, clearColor | 0xFF000000u);
}

void endFrame() {
    framebufferEnd(&g_fb);
    g_pix = nullptr;
}

// ---------------------------------------------------------------- primitives

void fillRect(int x, int y, int w, int h, u32 color) {
    int x0 = std::max(x, 0), y0 = std::max(y, 0);
    int x1 = std::min(x + w, W), y1 = std::min(y + h, H);
    if (x0 >= x1 || y0 >= y1) return;

    const bool opaque = (color >> 24) == 255;
    for (int py = y0; py < y1; py++) {
        u32* row = g_pix + (size_t)py * g_stride;
        if (opaque) {
            for (int px = x0; px < x1; px++) row[px] = color;
        } else {
            for (int px = x0; px < x1; px++) blend(&row[px], color, 255);
        }
    }
}

void fillRoundRect(int x, int y, int w, int h, int r, u32 color) {
    if (w <= 0 || h <= 0) return;
    r = std::max(0, std::min(r, std::min(w, h) / 2));
    if (r == 0) {
        fillRect(x, y, w, h, color);
        return;
    }

    // Straight parts.
    fillRect(x + r, y, w - 2 * r, h, color);
    fillRect(x, y + r, r, h - 2 * r, color);
    fillRect(x + w - r, y + r, r, h - 2 * r, color);

    // Anti-aliased corners.
    for (int cj = 0; cj < 2; cj++) {
        for (int ci = 0; ci < 2; ci++) {
            const int cx  = ci == 0 ? x + r : x + w - r;
            const int cy  = cj == 0 ? y + r : y + h - r;
            const int px0 = ci == 0 ? x : x + w - r;
            const int py0 = cj == 0 ? y : y + h - r;
            for (int j = 0; j < r; j++) {
                for (int i = 0; i < r; i++) {
                    float dx  = (px0 + i + 0.5f) - (float)cx;
                    float dy  = (py0 + j + 0.5f) - (float)cy;
                    float cov = (float)r - std::sqrt(dx * dx + dy * dy) + 0.5f;
                    if (cov <= 0.f) continue;
                    if (cov > 1.f) cov = 1.f;
                    plot(px0 + i, py0 + j, color, (u32)(cov * 255.f));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------- text

int lineHeight(FontSize size) { return g_faces[(int)size].lineH; }

int textWidth(const std::string& s, FontSize size) {
    FontFace& f = g_faces[(int)size];
    float w = 0.f;
    const char* p = s.c_str();
    while (*p) {
        uint32_t cp = nextCodepoint(p);
        if (cp < 0x20) continue;
        w += getGlyph(f, cp).advance;
    }
    return (int)std::lround(w);
}

int drawText(int x, int y, const std::string& s, FontSize size, u32 color) {
    FontFace& f = g_faces[(int)size];
    float pen = (float)x;
    const int baseline = y + f.ascent;

    const char* p = s.c_str();
    while (*p) {
        uint32_t cp = nextCodepoint(p);
        if (cp < 0x20) continue;  // control characters (newlines are handled by callers)

        const Glyph& g = getGlyph(f, cp);
        if (g.bmp) {
            const int gx = (int)std::lround(pen) + g.xoff;
            const int gy = baseline + g.yoff;
            for (int j = 0; j < g.h; j++) {
                const int py = gy + j;
                if (py < 0 || py >= H) continue;
                u32* row = g_pix + (size_t)py * g_stride;
                const unsigned char* src = g.bmp + (size_t)j * g.w;
                for (int i = 0; i < g.w; i++) {
                    const int px = gx + i;
                    if (px < 0 || px >= W) continue;
                    if (src[i]) blend(&row[px], color, src[i]);
                }
            }
        }
        pen += g.advance;
    }
    return (int)std::lround(pen - (float)x);
}

int drawTextRight(int rightX, int y, const std::string& s, FontSize size, u32 color) {
    int w = textWidth(s, size);
    drawText(rightX - w, y, s, size, color);
    return w;
}

int drawTextCentered(int centerX, int y, const std::string& s, FontSize size, u32 color) {
    int w = textWidth(s, size);
    drawText(centerX - w / 2, y, s, size, color);
    return w;
}

std::string ellipsize(const std::string& s, int maxWidth, FontSize size) {
    if (textWidth(s, size) <= maxWidth) return s;

    FontFace& f = g_faces[(int)size];
    const int dotsW = textWidth("...", size);

    std::string out;
    float w = 0.f;
    const char* p = s.c_str();
    while (*p) {
        const char* start = p;
        uint32_t cp = nextCodepoint(p);
        if (cp < 0x20) continue;
        float adv = getGlyph(f, cp).advance;
        if (w + adv + (float)dotsW > (float)maxWidth) break;
        out.append(start, (size_t)(p - start));
        w += adv;
    }
    return out + "...";
}

int drawTextClipped(int x, int y, int maxWidth, const std::string& s, FontSize size, u32 color) {
    return drawText(x, y, ellipsize(s, maxWidth, size), size, color);
}

// --------------------------------------------------------------------- icons

namespace {

uint32_t iconCodepoint(ButtonIcon i) {
    switch (i) {
        case ButtonIcon::A:     return 0xE0E0;
        case ButtonIcon::B:     return 0xE0E1;
        case ButtonIcon::X:     return 0xE0E2;
        case ButtonIcon::Y:     return 0xE0E3;
        case ButtonIcon::L:     return 0xE0E4;
        case ButtonIcon::R:     return 0xE0E5;
        case ButtonIcon::ZL:    return 0xE0E6;
        case ButtonIcon::ZR:    return 0xE0E7;
        case ButtonIcon::Plus:  return 0xE0EF;
        case ButtonIcon::Minus: return 0xE0F0;
    }
    return 0;
}

const char* iconLabel(ButtonIcon i) {
    switch (i) {
        case ButtonIcon::A:     return "A";
        case ButtonIcon::B:     return "B";
        case ButtonIcon::X:     return "X";
        case ButtonIcon::Y:     return "Y";
        case ButtonIcon::L:     return "L";
        case ButtonIcon::R:     return "R";
        case ButtonIcon::ZL:    return "ZL";
        case ButtonIcon::ZR:    return "ZR";
        case ButtonIcon::Plus:  return "+";
        case ButtonIcon::Minus: return "-";
    }
    return "?";
}

bool hasExtGlyph(uint32_t cp) {
    return g_extReady && stbtt_FindGlyphIndex(&g_extInfo, (int)cp) != 0;
}

}  // namespace

int drawButtonIcon(int x, int y, ButtonIcon icon, u32 color, u32 bgColor) {
    const FontSize sz = FontSize::Normal;
    const uint32_t cp = iconCodepoint(icon);

    if (hasExtGlyph(cp)) {
        // 3-byte UTF-8 encoding (all icon codepoints are in U+E000..U+F8FF).
        char buf[4] = {(char)(0xE0 | (cp >> 12)), (char)(0x80 | ((cp >> 6) & 0x3F)),
                       (char)(0x80 | (cp & 0x3F)), 0};
        return drawText(x, y, buf, sz, color);
    }

    // Fallback: draw the icon ourselves.
    const bool shoulder = icon == ButtonIcon::L || icon == ButtonIcon::R ||
                          icon == ButtonIcon::ZL || icon == ButtonIcon::ZR;
    const int d  = 28;
    const int w  = shoulder ? 44 : d;
    const int iy = y + (lineHeight(sz) - d) / 2;
    const int r  = shoulder ? 8 : d / 2;

    fillRoundRect(x, iy, w, d, r, color);
    fillRoundRect(x + 2, iy + 2, w - 4, d - 4, std::max(0, r - 2), bgColor | 0xFF000000u);

    if (icon == ButtonIcon::Plus || icon == ButtonIcon::Minus) {
        fillRect(x + w / 2 - 7, iy + d / 2 - 1, 14, 3, color);
        if (icon == ButtonIcon::Plus) fillRect(x + w / 2 - 1, iy + d / 2 - 7, 3, 14, color);
    } else {
        drawTextCentered(x + w / 2, iy + (d - lineHeight(FontSize::Small)) / 2, iconLabel(icon),
                         FontSize::Small, color);
    }
    return w;
}

// --------------------------------------------------------------------- debug

std::vector<std::string> debugLines() {
    char b[160];
    std::vector<std::string> out;

    std::snprintf(b, sizeof b, "Std font: rc=0x%X size=%llu hdr=%s off=%d init=%s", (unsigned)g_diag.stdRc,
                  (unsigned long long)g_diag.stdSize, g_diag.stdHdr.c_str(), g_diag.stdOffset,
                  g_diag.stdInit ? "OK" : "FAIL");
    out.push_back(b);

    std::snprintf(b, sizeof b, "Ext font: rc=0x%X size=%llu hdr=%s off=%d init=%s", (unsigned)g_diag.extRc,
                  (unsigned long long)g_diag.extSize, g_diag.extHdr.c_str(), g_diag.extOffset,
                  g_diag.extInit ? "OK" : "FAIL");
    out.push_back(b);

    std::snprintf(b, sizeof b, "Ext font: glyphs=%d cmap_offset=%d ready=%s", g_extInfo.numGlyphs,
                  g_extInfo.index_map, g_extReady ? "yes" : "no");
    out.push_back(b);

    const int giExt = g_extReady ? stbtt_FindGlyphIndex(&g_extInfo, 0xE0E0) : -1;
    const int giStd = stbtt_FindGlyphIndex(&g_info, 0xE0E0);
    std::snprintf(b, sizeof b, "U+E0E0 glyph index: ext=%d std=%d (0 = missing)", giExt, giStd);
    out.push_back(b);
    return out;
}


// ---------------------------------------------------------------------- wrap

std::vector<std::string> wrapText(const std::string& s, int maxWidth, FontSize size) {
    std::vector<WrappedLine> wl = wrapTextOffsets(s, maxWidth, size);
    std::vector<std::string> out;
    out.reserve(wl.size());
    for (size_t i = 0; i < wl.size(); i++) out.push_back(std::move(wl[i].text));
    return out;
}

std::vector<WrappedLine> wrapTextOffsets(const std::string& s, int maxWidth, FontSize size) {
    FontFace& f = g_faces[(int)size];
    std::vector<WrappedLine> lines;
    std::string          line;
    std::vector<size_t>  lineOff;  // parallel to `line`: source byte offset of each byte.
                                    // Tracked explicitly (rather than derived by arithmetic)
                                    // so a dropped '\r' never throws off later offsets.
    float  lineW = 0.f;
    size_t lastSpace = std::string::npos;  // byte index of the last space inside `line`
    float  widthAfterSpace = 0.f;          // lineW right after that space

    const char* base = s.c_str();
    const char* p    = base;
    auto pushLine = [&](size_t fallbackOffset) {
        lines.push_back({line, lineOff.empty() ? fallbackOffset : lineOff.front()});
    };

    while (*p) {
        const char*  charStart = p;
        const size_t charOff   = (size_t)(charStart - base);
        uint32_t     cp        = nextCodepoint(p);

        if (cp == '\n') {
            pushLine(charOff);
            line.clear();
            lineOff.clear();
            lineW = 0.f;
            lastSpace = std::string::npos;
            continue;
        }
        if (cp == '\r') continue;

        std::string ch(charStart, (size_t)(p - charStart));
        if (cp == '\t') {
            cp = ' ';
            ch = " ";
        }
        if (cp < 0x20) continue;

        const float adv = getGlyph(f, cp).advance;
        if (lineW + adv > (float)maxWidth && !line.empty()) {
            if (cp == ' ') {  // a space that does not fit simply ends the line
                pushLine(charOff);
                line.clear();
                lineOff.clear();
                lineW = 0.f;
                lastSpace = std::string::npos;
                continue;
            }
            if (lastSpace != std::string::npos) {
                std::string          rest    = line.substr(lastSpace + 1);
                std::vector<size_t>  restOff(lineOff.begin() + (long)(lastSpace + 1), lineOff.end());
                lineW -= widthAfterSpace;
                line.resize(lastSpace);
                lineOff.resize(lastSpace);
                pushLine(charOff);
                line    = rest;
                lineOff = restOff;
            } else {
                pushLine(charOff);
                line.clear();
                lineOff.clear();
                lineW = 0.f;
            }
            lastSpace = std::string::npos;
        }

        for (size_t k = 0; k < ch.size(); k++) lineOff.push_back(charOff + k);
        line += ch;
        lineW += adv;
        if (cp == ' ') {
            lastSpace = line.size() - 1;
            widthAfterSpace = lineW;
        }
    }
    pushLine((size_t)(p - base));
    return lines;
}

// --------------------------------------------------------------- screen chrome

void drawHeader(const std::string& title, FontSize titleSize, const std::string& right, u32 rightColor) {
    fillRect(0, 0, W, 84, col::header);
    fillRect(0, 84, W, 2, col::accent);
    const int rightW = right.empty() ? 0 : textWidth(right, FontSize::Normal);
    const int maxTitleW = W - 80 - rightW - 40;
    drawTextClipped(40, (84 - lineHeight(titleSize)) / 2, maxTitleW, title, titleSize, col::text);
    if (!right.empty())
        drawTextRight(W - 40, (84 - lineHeight(FontSize::Normal)) / 2, right, FontSize::Normal, rightColor);
}

void drawFooter(const std::vector<Hint>& hints, const std::string& rightText) {
    fillRect(0, 660, W, 60, col::header);
    const int fy = 660 + (60 - lineHeight(FontSize::Normal)) / 2;
    int x = 40;
    for (size_t i = 0; i < hints.size(); i++) {
        if (hints[i].hasIcon) x += drawButtonIcon(x, fy, hints[i].icon, col::accent, col::header) + 10;
        x += drawText(x, fy, hints[i].label, FontSize::Normal, col::textDim) + 30;
    }
    if (!rightText.empty()) drawTextRight(W - 40, fy, rightText, FontSize::Normal, col::textDim);
}

}  // namespace gui

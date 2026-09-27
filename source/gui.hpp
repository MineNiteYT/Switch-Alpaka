// gui.hpp - minimal software GUI for libnx (no SDL2)
//
// Renders into a linear RGBA8888 framebuffer (1280x720) and draws text with
// stb_truetype using the Switch system font (plGetSharedFontByType).
#pragma once

#include <switch.h>
#include <string>
#include <vector>

namespace gui {

constexpr int W = 1280;
constexpr int H = 720;

// Memory byte order for PIXEL_FORMAT_RGBA_8888 is R,G,B,A (little-endian u32).
constexpr u32 rgba(u8 r, u8 g, u8 b, u8 a = 255) {
    return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}

namespace col {
constexpr u32 bg      = rgba(15, 17, 24);
constexpr u32 header  = rgba(22, 26, 38);
constexpr u32 panel   = rgba(27, 32, 46);
constexpr u32 panelHi = rgba(40, 49, 72);
constexpr u32 accent  = rgba(96, 165, 250);
constexpr u32 text    = rgba(232, 236, 244);
constexpr u32 textDim = rgba(140, 150, 172);
constexpr u32 ok      = rgba(74, 222, 128);
constexpr u32 shadow  = rgba(0, 0, 0, 90);
}  // namespace col

enum class FontSize { Small = 0, Normal = 1, Large = 2, Title = 3 };

// Creates the framebuffer and loads the system font.
// On failure returns false and fills `err` with a readable reason.
bool init(std::string& err);
void shutdown();

// Frame handling. beginFrame() blocks until a buffer is free (vsync pacing).
void beginFrame(u32 clearColor);
void endFrame();

// Primitives (all clipped to the screen, alpha in the colour is respected).
void fillRect(int x, int y, int w, int h, u32 color);
void fillRoundRect(int x, int y, int w, int h, int radius, u32 color);

// Text (UTF-8). `y` is the top of the line box.
int  lineHeight(FontSize size);
int  textWidth(const std::string& s, FontSize size);
int  drawText(int x, int y, const std::string& s, FontSize size, u32 color);       // returns width
int  drawTextRight(int rightX, int y, const std::string& s, FontSize size, u32 color);
int  drawTextCentered(int centerX, int y, const std::string& s, FontSize size, u32 color);
std::string ellipsize(const std::string& s, int maxWidth, FontSize size);
// Word-wraps UTF-8 text to `maxWidth` pixels. Honours '\n'; breaks inside a word
// only when a single word is wider than the line. Always returns at least one line.
std::vector<std::string> wrapText(const std::string& s, int maxWidth, FontSize size);

// A wrapped line plus the byte offset (in the source string passed to
// wrapTextOffsets) where that line's text begins. Lets a caller map byte-range
// annotations of the source (e.g. markdown style runs) onto wrapped lines.
struct WrappedLine {
    std::string text;
    size_t      startOffset;
};
std::vector<WrappedLine> wrapTextOffsets(const std::string& s, int maxWidth, FontSize size);
int  drawTextClipped(int x, int y, int maxWidth, const std::string& s, FontSize size, u32 color);

// Controller button icons. Uses the system icon glyph when the Nintendo extension
// font provides it, otherwise draws the icon itself (ring + label).
// `bgColor` must be the opaque colour behind the icon. Returns the width used.
enum class ButtonIcon { A, B, X, Y, L, R, ZL, ZR, Plus, Minus };
int drawButtonIcon(int x, int y, ButtonIcon icon, u32 color, u32 bgColor);

// Shared screen chrome ---------------------------------------------------------

// Top bar (0..84) with a title on the left and a status text on the right.
void drawHeader(const std::string& title, FontSize titleSize, const std::string& right, u32 rightColor);

// One entry of the footer: an optional button icon followed by a label.
struct Hint {
    bool        hasIcon;
    ButtonIcon  icon;
    std::string label;
    Hint(ButtonIcon i, const std::string& l) : hasIcon(true), icon(i), label(l) {}
    explicit Hint(const std::string& l) : hasIcon(false), icon(ButtonIcon::A), label(l) {}
};

// Bottom bar (660..720) with button hints on the left and a text on the right.
void drawFooter(const std::vector<Hint>& hints, const std::string& rightText);

// Human-readable font diagnostics for an on-screen debug panel.
std::vector<std::string> debugLines();

}  // namespace gui





#pragma once

#include <switch.h>
#include <string>
#include <vector>

namespace gui {

constexpr int W = 1280;
constexpr int H = 720;


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
}

enum class FontSize { Small = 0, Normal = 1, Large = 2, Title = 3 };



bool init(std::string& err);
void shutdown();


void beginFrame(u32 clearColor);
void endFrame();


void fillRect(int x, int y, int w, int h, u32 color);
void fillRoundRect(int x, int y, int w, int h, int radius, u32 color);


int  lineHeight(FontSize size);
int  textWidth(const std::string& s, FontSize size);
int  drawText(int x, int y, const std::string& s, FontSize size, u32 color);
int  drawTextRight(int rightX, int y, const std::string& s, FontSize size, u32 color);
int  drawTextCentered(int centerX, int y, const std::string& s, FontSize size, u32 color);
std::string ellipsize(const std::string& s, int maxWidth, FontSize size);


std::vector<std::string> wrapText(const std::string& s, int maxWidth, FontSize size);




struct WrappedLine {
    std::string text;
    size_t      startOffset;
};
std::vector<WrappedLine> wrapTextOffsets(const std::string& s, int maxWidth, FontSize size);
int  drawTextClipped(int x, int y, int maxWidth, const std::string& s, FontSize size, u32 color);




enum class ButtonIcon { A, B, X, Y, L, R, ZL, ZR, Plus, Minus };
int drawButtonIcon(int x, int y, ButtonIcon icon, u32 color, u32 bgColor);




void drawHeader(const std::string& title, FontSize titleSize, const std::string& right, u32 rightColor);


struct Hint {
    bool        hasIcon;
    ButtonIcon  icon;
    std::string label;
    Hint(ButtonIcon i, const std::string& l) : hasIcon(true), icon(i), label(l) {}
    explicit Hint(const std::string& l) : hasIcon(false), icon(ButtonIcon::A), label(l) {}
};


void drawFooter(const std::vector<Hint>& hints, const std::string& rightText);


std::vector<std::string> debugLines();

}

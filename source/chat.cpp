#include "chat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "chat_store.hpp"
#include "gui.hpp"
#include "markdown.hpp"

using gui::FontSize;
namespace col = gui::col;

namespace {


const int kViewTop     = 100;
const int kViewBottom  = 584;
const int kInputY      = 596;
const int kInputH      = 52;
const int kMarginX     = 40;
const int kRightEdge   = 1228;
const int kMaxTextW    = 860;
const int kPadX        = 20;
const int kPadY        = 12;
const int kGap         = 16;
const FontSize kMsgFont = FontSize::Normal;

const u32 kUserBubble = gui::rgba(37, 99, 235);
const u32 kUserText   = gui::rgba(255, 255, 255);

struct Lock {
    Mutex& m;
    explicit Lock(Mutex& mm) : m(mm) { mutexLock(&m); }
    ~Lock() { mutexUnlock(&m); }
};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\n' || s[a] == '\t' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\n' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}


int drawStyledLine(int x, int y, const gui::WrappedLine& line, const std::vector<MdRun>& runs,
                   FontSize size, u32 normalColor) {
    const std::string& text = line.text;
    int    cursorX = x;
    size_t pos     = 0;
    while (pos < text.size()) {
        MdStyle style        = MdStyle::Normal;
        size_t  runEndInLine = text.size();
        for (size_t r = 0; r < runs.size(); r++) {
            const size_t abs = line.startOffset + pos;
            if (runs[r].start <= abs && abs < runs[r].start + runs[r].len) {
                style               = runs[r].style;
                const size_t runEndAbs = runs[r].start + runs[r].len;
                runEndInLine        = std::min(text.size(), pos + (runEndAbs - abs));
                break;
            }
        }

        const std::string seg = text.substr(pos, runEndInLine - pos);
        if (style == MdStyle::Code) {
            const int segW = gui::textWidth(seg, size);
            gui::fillRoundRect(cursorX - 4, y - 2, segW + 8, gui::lineHeight(size) + 4, 6, col::panelHi);
        }
        const u32 color = style == MdStyle::Code ? col::accent : normalColor;
        if (style == MdStyle::Bold) gui::drawText(cursorX + 1, y, seg, size, color);
        cursorX += gui::drawText(cursorX, y, seg, size, color);
        pos = runEndInLine;
    }
    return cursorX - x;
}

}



ChatScreen::ChatScreen(ChatBackend* backend, std::string modelStem, uint32_t chatId,
                       std::vector<ChatMessage> initialHistory)
    : modelStem_(std::move(modelStem)), chatId_(chatId), backend_(backend) {
    mutexInit(&mutex_);
    messages_ = std::move(initialHistory);
}

ChatScreen::~ChatScreen() {

    if (backend_) {
        backend_->cancel();
        backend_.reset();
    }
    flushPendingIntoMessages();
    saveIfDirty();
}



std::string ChatScreen::statusText(u32& color) const {
    char b[64];
    color = col::textDim;
    switch (state_) {
        case State::Idle:
            if (tokPerSec_ > 0.f) {
                std::snprintf(b, sizeof b, "Ready - last reply %.1f tok/s", tokPerSec_);
                return b;
            }
            return "Ready";
        case State::Waiting:
            color = col::accent;
            return "Thinking...";
        case State::Streaming:
            color = col::accent;
            if (tokPerSec_ > 0.f) {
                std::snprintf(b, sizeof b, "Generating - %.1f tok/s", tokPerSec_);
                return b;
            }
            return "Generating...";
        case State::Stopping:
            return "Stopping...";
    }
    return "";
}

void ChatScreen::startNewChat() {
    saveIfDirty();

    messages_.clear();
    layouts_.clear();
    contentH_ = 0;
    scroll_   = 0;
    follow_   = true;
    tokens_   = 0;
    tokPerSec_ = 0.f;
    chatId_   = 0;
    dirty_    = false;
    toast_ = "New chat";
    toastFrames_ = 90;
}

void ChatScreen::saveIfDirty() {
    if (!dirty_ || messages_.empty()) return;
    const uint32_t saved = chat_store::saveChat(modelStem_, chatId_, "", messages_);
    if (saved != 0) {
        chatId_ = saved;
        dirty_  = false;
    }

}

bool ChatScreen::askForText(std::string& out) {
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) {
        toast_ = "Could not open the keyboard";
        toastFrames_ = 150;
        return false;
    }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, "Message");
    swkbdConfigSetGuideText(&kbd, "Write a message to the model");
    swkbdConfigSetStringLenMax(&kbd, 500);

    char buf[2048];
    std::memset(buf, 0, sizeof buf);
    rc = swkbdShow(&kbd, buf, sizeof buf);
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;

    const std::string text = trim(buf);
    if (text.empty()) return false;
    out = text;
    return true;
}

void ChatScreen::sendMessage(const std::string& text) {
    ChatMessage u;
    u.role = ChatMessage::Role::User;
    u.text = text;
    messages_.push_back(u);

    ChatMessage a;
    a.role = ChatMessage::Role::Assistant;
    messages_.push_back(a);

    tokens_    = 0;
    tokPerSec_ = 0.f;
    state_     = State::Waiting;
    follow_    = true;
    dirty_     = true;


    std::vector<ChatMessage> history(messages_.begin(), messages_.end() - 1);

    {
        Lock l(mutex_);
        pendingText_.clear();
        pendingTokens_ = 0;
        pendingDone_   = false;
        pendingErr_.clear();
    }

    backend_->generate(
        history,
        [this](const std::string& piece) {
            Lock l(mutex_);
            pendingText_ += piece;
            pendingTokens_++;
        },
        [this](bool ok, const std::string& err) {
            Lock l(mutex_);
            pendingDone_ = true;
            pendingOk_   = ok;
            pendingErr_  = err;
        });
}

void ChatScreen::drainPending() {
    if (flushPendingIntoMessages()) saveIfDirty();
}

bool ChatScreen::flushPendingIntoMessages() {
    std::string text, err;
    int         toks = 0;
    bool        done = false, ok = true;
    {
        Lock l(mutex_);
        text.swap(pendingText_);
        toks           = pendingTokens_;
        pendingTokens_ = 0;
        done           = pendingDone_;
        pendingDone_   = false;
        ok             = pendingOk_;
        err            = pendingErr_;
    }

    if (toks > 0 && !messages_.empty()) {
        const u64 now = armGetSystemTick();
        if (tokens_ == 0) firstTokenTick_ = now;
        tokens_ += toks;
        messages_.back().text += text;
        dirty_ = true;
        if (state_ == State::Waiting) state_ = State::Streaming;

        const double secs = (double)armTicksToNs(now - firstTokenTick_) / 1e9;
        if (secs > 0.5) tokPerSec_ = (float)((double)tokens_ / secs);
    }

    if (done) {
        if (!messages_.empty() && messages_.back().role == ChatMessage::Role::Assistant) {
            std::string& t = messages_.back().text;
            if (ok) {

            } else if (err == "cancelled") {
                if (t.empty()) t = "[stopped]";
            } else {
                t += (t.empty() ? "" : "\n") + std::string("[error: ") + err + "]";
                toast_ = "Generation failed";
                toastFrames_ = 180;
            }
        }
        state_ = State::Idle;
    }
    return done;
}

void ChatScreen::relayout() {
    layouts_.resize(messages_.size());
    const int lh = gui::lineHeight(kMsgFont);

    int y = 0;
    for (size_t i = 0; i < messages_.size(); i++) {
        Layout&            L = layouts_[i];
        const std::string& t = messages_[i].text;
        if (L.textLen != t.size()) {
            const ParsedMarkdown pm = parseInlineMarkdown(t);
            L.lines = gui::wrapTextOffsets(pm.plain, kMaxTextW, kMsgFont);
            L.runs  = pm.runs;
            int w   = 0;
            for (size_t k = 0; k < L.lines.size(); k++)
                w = std::max(w, gui::textWidth(L.lines[k].text, kMsgFont));
            L.textW   = w;
            L.height  = (int)L.lines.size() * lh + 2 * kPadY;
            L.textLen = t.size();
        }
        L.y = y;
        y += L.height + kGap;
    }
    contentH_ = std::max(0, y - kGap);
}



ChatScreen::Action ChatScreen::update(const PadState& pad, u64 down, u64 held) {
    frame_++;
    if (toastFrames_ > 0) toastFrames_--;

    drainPending();
    relayout();


    const int viewH     = kViewBottom - kViewTop;
    const int maxScroll = std::max(0, contentH_ - viewH);

    float delta = 0.f;
    if (held & HidNpadButton_Up) delta -= 24.f;
    if (held & HidNpadButton_Down) delta += 24.f;
    const HidAnalogStickState rs = padGetStickPos(&pad, 1);
    if (std::abs(rs.y) > 6000) delta -= (float)rs.y / 32767.f * 32.f;
    if (down & HidNpadButton_L) delta -= (float)(viewH - 60);
    if (down & HidNpadButton_R) delta += (float)(viewH - 60);

    if (down & HidNpadButton_ZL) {
        scroll_ = 0;
        follow_ = false;
    }
    if (down & HidNpadButton_ZR) follow_ = true;

    if (delta != 0.f) {
        scroll_ += (int)std::lround(delta);
        if (delta < 0.f) follow_ = false;
    }
    scroll_ = std::max(0, std::min(scroll_, maxScroll));
    if (delta > 0.f && scroll_ >= maxScroll) follow_ = true;
    if (follow_) scroll_ = maxScroll;


    if (state_ == State::Idle) {
        if (down & HidNpadButton_A) {
            std::string text;
            if (askForText(text)) sendMessage(text);
        }
        if ((down & HidNpadButton_X) && !messages_.empty()) startNewChat();
        if (down & HidNpadButton_B) return Action::Back;
    } else if ((down & HidNpadButton_B) && state_ != State::Stopping) {
        state_ = State::Stopping;
        backend_->cancel();
    }
    return Action::None;
}



void ChatScreen::draw(int fps) {
    const int lh    = gui::lineHeight(kMsgFont);
    const int viewH = kViewBottom - kViewTop;


    if (messages_.empty()) {
        gui::drawTextCentered(gui::W / 2, 230, "Start a conversation", FontSize::Large, col::text);
        gui::drawTextCentered(gui::W / 2, 290, "Press A to write your first message", FontSize::Normal,
                              col::textDim);
    }

    for (size_t i = 0; i < messages_.size() && i < layouts_.size(); i++) {
        const Layout& L = layouts_[i];
        const int top = kViewTop + L.y - scroll_;
        if (top + L.height < kViewTop - 4 || top > kViewBottom + 4) continue;

        const bool user   = messages_[i].role == ChatMessage::Role::User;
        const bool isLast = i + 1 == messages_.size();
        const bool empty  = messages_[i].text.empty();

        std::string placeholder;
        int         bw = std::max(80, L.textW + 2 * kPadX);
        if (!user && isLast && empty && state_ != State::Idle) {
            placeholder = "Thinking" + std::string((size_t)((frame_ / 20) % 4), '.');
            bw = std::max(bw, gui::textWidth("Thinking...", kMsgFont) + 2 * kPadX);
        }

        const int bx = user ? kRightEdge - bw : kMarginX;
        gui::fillRoundRect(bx, top, bw, L.height, 16, user ? kUserBubble : col::panel);

        if (!placeholder.empty()) {
            gui::drawText(bx + kPadX, top + kPadY, placeholder, kMsgFont, col::textDim);
            continue;
        }

        const u32 fg = user ? kUserText : col::text;
        for (size_t k = 0; k < L.lines.size(); k++) {
            const int ly = top + kPadY + (int)k * lh;
            if (ly + lh < kViewTop || ly > kViewBottom) continue;
            drawStyledLine(bx + kPadX, ly, L.lines[k], L.runs, kMsgFont, fg);
        }


        if (!user && isLast && state_ == State::Streaming && ((frame_ / 30) % 2) == 0 &&
            !L.lines.empty()) {
            const int cx = bx + kPadX + gui::textWidth(L.lines.back().text, kMsgFont) + 3;
            const int cy = top + kPadY + (int)(L.lines.size() - 1) * lh + 5;
            gui::fillRect(cx, cy, 3, lh - 10, col::accent);
        }
    }


    if (contentH_ > viewH) {
        const int maxScroll = contentH_ - viewH;
        const int thumbH    = std::max(32, viewH * viewH / contentH_);
        const int thumbY    = kViewTop + (viewH - thumbH) * scroll_ / maxScroll;
        gui::fillRoundRect(1244, kViewTop, 6, viewH, 3, col::panel);
        gui::fillRoundRect(1244, thumbY, 6, thumbH, 3, col::accent);
    }


    if (!follow_ && contentH_ > viewH) {
        const int pw = 190, ph = 40;
        const int px = kRightEdge - pw, py = kViewBottom - ph - 12;
        gui::fillRoundRect(px, py, pw, ph, ph / 2, col::panelHi);
        const int iw = gui::drawButtonIcon(px + 16, py + (ph - lh) / 2, gui::ButtonIcon::ZR, col::accent,
                                           col::panelHi);
        gui::drawText(px + 16 + iw + 10, py + (ph - lh) / 2, "Latest", kMsgFont, col::text);
    }


    gui::fillRect(0, 0, gui::W, kViewTop, col::bg);
    gui::fillRect(0, kViewBottom, gui::W, gui::H - kViewBottom, col::bg);


    u32 statusColor = col::textDim;
    const std::string status = statusText(statusColor);
    gui::drawHeader(backend_->modelName(), FontSize::Large, status, statusColor);


    gui::fillRoundRect(kMarginX, kInputY, gui::W - 2 * kMarginX, kInputH, 14, col::panel);
    {
        const bool busy = state_ != State::Idle;
        const int  ty   = kInputY + (kInputH - lh) / 2;
        const int  iw   = gui::drawButtonIcon(kMarginX + 22, ty, busy ? gui::ButtonIcon::B : gui::ButtonIcon::A,
                                              col::accent, col::panel);
        gui::drawText(kMarginX + 22 + iw + 12, ty, busy ? "Generating... press to stop" : "Write a message",
                      kMsgFont, col::textDim);
    }


    if (toastFrames_ > 0) {
        const std::string t  = gui::ellipsize(toast_, 800, FontSize::Normal);
        const int         tw = gui::textWidth(t, FontSize::Normal) + 56;
        const int         th = gui::lineHeight(FontSize::Normal) + 20;
        const int         tx = (gui::W - tw) / 2, ty = kInputY - th - 14;
        gui::fillRoundRect(tx, ty, tw, th, th / 2, col::accent);
        gui::drawText(tx + 28, ty + 10, t, FontSize::Normal, col::bg);
    }


    std::vector<gui::Hint> hints;
    if (state_ == State::Idle) {
        hints.push_back(gui::Hint(gui::ButtonIcon::A, "Write"));
        if (!messages_.empty()) hints.push_back(gui::Hint(gui::ButtonIcon::X, "New chat"));
        hints.push_back(gui::Hint(gui::ButtonIcon::B, "Back"));
    } else {
        hints.push_back(gui::Hint(gui::ButtonIcon::B, "Stop"));
    }
    hints.push_back(gui::Hint(std::string("Scroll: D-Pad / R-Stick")));
    gui::drawFooter(hints, (fps < 0 ? std::string("--") : std::to_string(fps)) + " FPS");
}


#include "chat_list.hpp"

#include <algorithm>

#include "gui.hpp"

using gui::FontSize;
namespace col = gui::col;

namespace {
const int kRowH = 68, kRowGap = 8, kVisible = 7;
const int kListX = 40, kListY = 150, kListW = 1200;

bool repeatFire(u64 down, u64 held, u64 mask, int& counter) {
    if (down & mask) {
        counter = 0;
        return true;
    }
    if (held & mask) {
        counter++;
        return counter > 20 && (counter - 20) % 4 == 0;
    }
    counter = 0;
    return false;
}
}

ChatListScreen::ChatListScreen(std::string modelStem, std::string modelDisplayName)
    : modelStem_(std::move(modelStem)), modelDisplayName_(std::move(modelDisplayName)) {
    rescan();
}

void ChatListScreen::rescan() {
    chats_ = chat_store::listChats(modelStem_);
    const int n = (int)chats_.size() + 1;
    sel_        = std::min(sel_, n - 1);
    scroll_     = 0;
    confirmingDelete_ = false;
}

void ChatListScreen::notifyError(const std::string& message) {
    toast_       = message;
    toastFrames_ = 220;
}

ChatListScreen::Action ChatListScreen::update(u64 down, u64 held) {
    const int n = (int)chats_.size() + 1;

    if (repeatFire(down, held, HidNpadButton_Up, upCnt_)) {
        sel_ = std::max(0, sel_ - 1);
        confirmingDelete_ = false;
    }
    if (repeatFire(down, held, HidNpadButton_Down, downCnt_)) {
        sel_ = std::min(n - 1, sel_ + 1);
        confirmingDelete_ = false;
    }

    if (sel_ < scroll_) scroll_ = sel_;
    if (sel_ >= scroll_ + kVisible) scroll_ = sel_ - kVisible + 1;
    if (toastFrames_ > 0) toastFrames_--;

    if (down & HidNpadButton_A) {
        openId_ = (sel_ == 0) ? 0 : chats_[sel_ - 1].id;
        return Action::Open;
    }
    if (down & HidNpadButton_Y && sel_ > 0) {
        if (!confirmingDelete_) {
            confirmingDelete_ = true;
            toast_            = "Press Y again to delete \"" + chats_[sel_ - 1].title + "\"";
            toastFrames_      = 180;
        } else {
            chat_store::deleteChat(modelStem_, chats_[sel_ - 1].id);
            rescan();
            toast_       = "Chat deleted";
            toastFrames_ = 120;
        }
    }
    if (down & HidNpadButton_B) return Action::Back;
    return Action::None;
}

void ChatListScreen::draw(int fps) {
    gui::drawHeader(modelDisplayName_, FontSize::Large, "Saved chats", col::textDim);

    const int n = (int)chats_.size() + 1;
    const int last = std::min(n, scroll_ + kVisible);
    for (int i = scroll_; i < last; i++) {
        const int  y     = kListY + (i - scroll_) * (kRowH + kRowGap);
        const bool isSel = (i == sel_);
        gui::fillRoundRect(kListX, y, kListW, kRowH, 12, isSel ? col::panelHi : col::panel);
        if (isSel) gui::fillRoundRect(kListX, y + 12, 6, kRowH - 24, 3, col::accent);

        const int ty = y + (kRowH - gui::lineHeight(FontSize::Normal)) / 2;
        if (i == 0) {
            const int iw = gui::drawButtonIcon(kListX + 24, ty, gui::ButtonIcon::Plus,
                                               isSel ? col::accent : col::textDim, isSel ? col::panelHi : col::panel);
            gui::drawText(kListX + 24 + iw + 14, ty, "New chat", FontSize::Normal,
                         isSel ? col::text : col::textDim);
        } else {
            const ChatSummary& c     = chats_[i - 1];
            const std::string  count = std::to_string(c.messageCount) + (c.messageCount == 1 ? " message" : " messages");
            const int          countW = gui::textWidth(count, FontSize::Small);
            gui::drawTextClipped(kListX + 24, ty, kListW - 24 - countW - 48, c.title, FontSize::Normal,
                                 isSel ? col::text : col::textDim);
            gui::drawTextRight(kListX + kListW - 24, y + (kRowH - gui::lineHeight(FontSize::Small)) / 2,
                               count, FontSize::Small, col::textDim);
        }
    }

    if (chats_.empty()) {
        gui::drawText(kListX, kListY + kRowH + kRowGap + 20, "No saved chats yet for this model.",
                     FontSize::Normal, col::textDim);
    }

    if (n > kVisible) {
        const int trackH = kVisible * (kRowH + kRowGap) - kRowGap;
        const int thumbH = std::max(30, trackH * kVisible / n);
        const int thumbY = kListY + (trackH - thumbH) * scroll_ / (n - kVisible);
        gui::fillRoundRect(kListX + kListW + 12, kListY, 6, trackH, 3, col::panel);
        gui::fillRoundRect(kListX + kListW + 12, thumbY, 6, thumbH, 3, col::accent);
    }

    if (toastFrames_ > 0) {
        const std::string t  = gui::ellipsize(toast_, 1100, FontSize::Normal);
        const int         tw = gui::textWidth(t, FontSize::Normal) + 56;
        const int         th = gui::lineHeight(FontSize::Normal) + 24;
        const int         tx = (gui::W - tw) / 2, ty = 588;
        gui::fillRoundRect(tx, ty, tw, th, th / 2, col::accent);
        gui::drawText(tx + 28, ty + 12, t, FontSize::Normal, col::bg);
    }

    std::vector<gui::Hint> hints;
    hints.push_back(gui::Hint(gui::ButtonIcon::A, "Open"));
    if (sel_ > 0) hints.push_back(gui::Hint(gui::ButtonIcon::Y, "Delete"));
    hints.push_back(gui::Hint(gui::ButtonIcon::B, "Back"));
    gui::drawFooter(hints, (fps < 0 ? std::string("--") : std::to_string(fps)) + " FPS");
}

// chat_list.hpp - shows a model's saved chats (chat_store::listChats) plus a
// pinned "New chat" entry. Lets the picker hand off to the chat screen either
// with an existing chat's history or with none.
#pragma once

#include <switch.h>

#include <cstdint>
#include <string>
#include <vector>

#include "chat_store.hpp"

class ChatListScreen {
public:
    enum class Action { None, Open, Back };

    explicit ChatListScreen(std::string modelStem, std::string modelDisplayName);

    // Returns Action::Open when the user picked an entry - read openId() next
    // (0 means "start a new chat"). Action::Back means "return to the model
    // picker".
    Action update(u64 down, u64 held);
    void   draw(int fps);

    void rescan();  // call after returning from a chat, so deletions/saves show up
    void notifyError(const std::string& message);  // shows a toast (e.g. "model failed to load")

    uint32_t openId() const { return openId_; }

private:
    std::string             modelStem_;
    std::string             modelDisplayName_;
    std::vector<ChatSummary> chats_;  // index 0 in the on-screen list is always "New chat"
    int                      sel_ = 0, scroll_ = 0;
    int                      upCnt_ = 0, downCnt_ = 0;
    uint32_t                 openId_ = 0;
    std::string              toast_;
    int                      toastFrames_ = 0;
    bool                     confirmingDelete_ = false;
};

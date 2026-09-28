


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




    Action update(u64 down, u64 held);
    void   draw(int fps);

    void rescan();
    void notifyError(const std::string& message);

    uint32_t openId() const { return openId_; }

private:
    std::string             modelStem_;
    std::string             modelDisplayName_;
    std::vector<ChatSummary> chats_;
    int                      sel_ = 0, scroll_ = 0;
    int                      upCnt_ = 0, downCnt_ = 0;
    uint32_t                 openId_ = 0;
    std::string              toast_;
    int                      toastFrames_ = 0;
    bool                     confirmingDelete_ = false;
};

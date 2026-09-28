#pragma once

#include <switch.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "backend.hpp"
#include "gui.hpp"
#include "markdown.hpp"

class ChatScreen {
public:
    enum class Action { None, Back };


    ChatScreen(ChatBackend* backend, std::string modelStem, uint32_t chatId,
              std::vector<ChatMessage> initialHistory);
    ~ChatScreen();


    Action update(const PadState& pad, u64 down, u64 held);
    void   draw(int fps);

private:
    enum class State { Idle, Waiting, Streaming, Stopping };

    struct Layout {
        size_t                        textLen = (size_t)-1;
        std::vector<gui::WrappedLine> lines;
        std::vector<MdRun>            runs;
        int                           textW  = 0;
        int                           height = 0;
        int                           y      = 0;
    };

    void drainPending();
    bool flushPendingIntoMessages();
    void relayout();
    bool askForText(std::string& out);
    void sendMessage(const std::string& text);
    void startNewChat();
    void saveIfDirty();
    std::string statusText(u32& color) const;

    std::string   modelStem_;
    uint32_t      chatId_ = 0;
    bool          dirty_  = false;

    std::unique_ptr<ChatBackend> backend_;
    std::vector<ChatMessage>     messages_;
    std::vector<Layout>          layouts_;

    State state_    = State::Idle;
    int   contentH_ = 0;
    int   scroll_   = 0;
    bool  follow_   = true;


    Mutex       mutex_;
    std::string pendingText_;
    int         pendingTokens_ = 0;
    bool        pendingDone_   = false;
    bool        pendingOk_     = true;
    std::string pendingErr_;

    int   tokens_         = 0;
    u64   firstTokenTick_ = 0;
    float tokPerSec_      = 0.f;

    std::string toast_;
    int         toastFrames_ = 0;
    int         frame_       = 0;
};

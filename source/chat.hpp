// chat.hpp - the chat screen: message bubbles, streaming, scrolling, text input.
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

    // `modelStem` identifies the model's chat folder on the SD card (see
    // chat_store.hpp). `chatId` == 0 with an empty `initialHistory` starts a
    // brand-new, not-yet-saved chat; a non-zero id resumes a saved one.
    // Takes ownership of `backend`.
    ChatScreen(ChatBackend* backend, std::string modelStem, uint32_t chatId,
              std::vector<ChatMessage> initialHistory);
    ~ChatScreen();

    // Call once per frame before draw(): drains streamed tokens, handles input.
    Action update(const PadState& pad, u64 down, u64 held);
    void   draw(int fps);

private:
    enum class State { Idle, Waiting, Streaming, Stopping };

    struct Layout {
        size_t                        textLen = (size_t)-1;  // text length this layout was built for
        std::vector<gui::WrappedLine> lines;
        std::vector<MdRun>            runs;    // style runs, in the same (marker-stripped) coordinates as lines
        int                           textW  = 0;  // widest line in pixels
        int                           height = 0;  // bubble height
        int                           y      = 0;  // offset from the top of the content
    };

    void drainPending();
    bool flushPendingIntoMessages();  // returns true if a generation just finished; used by drainPending and ~ChatScreen
    void relayout();
    bool askForText(std::string& out);
    void sendMessage(const std::string& text);
    void startNewChat();
    void saveIfDirty();
    std::string statusText(u32& color) const;

    std::string   modelStem_;
    uint32_t      chatId_ = 0;       // 0 = not yet saved
    bool          dirty_  = false;   // true if messages_ changed since the last save

    std::unique_ptr<ChatBackend> backend_;
    std::vector<ChatMessage>     messages_;
    std::vector<Layout>          layouts_;

    State state_    = State::Idle;
    int   contentH_ = 0;
    int   scroll_   = 0;
    bool  follow_   = true;  // keep the view glued to the newest text

    // Filled by backend callbacks (worker thread), drained by the UI thread.
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

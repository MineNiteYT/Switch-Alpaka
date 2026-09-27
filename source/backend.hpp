// backend.hpp - interface between the chat UI and the inference engine.
//
// The UI only talks to this interface. Right now a mock implementation streams a
// canned reply so the UI can be tested on hardware; the llama.cpp glue code of
// Alpaka will implement the same interface later.
#pragma once

#include <functional>
#include <string>
#include <vector>

struct ChatMessage {
    enum class Role { User, Assistant };
    Role        role = Role::User;
    std::string text;
};

// Called for every generated piece of text (usually one token).
typedef std::function<void(const std::string& piece)> TokenCallback;
// Called exactly once per generate() call. ok=false with err="cancelled" means
// the generation was stopped by cancel().
typedef std::function<void(bool ok, const std::string& err)> DoneCallback;

class ChatBackend {
public:
    virtual ~ChatBackend() {}

    virtual std::string modelName() const = 0;

    // Starts generating the assistant reply for `history` (the last entry is the
    // new user message). Must return immediately; both callbacks may be invoked
    // from a worker thread.
    virtual void generate(const std::vector<ChatMessage>& history, TokenCallback onToken,
                          DoneCallback onDone) = 0;

    // Asks a running generation to stop. onDone must still be called afterwards.
    virtual void cancel() = 0;
};

// Mock backend: waits ~1 s ("prompt processing"), then streams a test reply at
// `tokensPerSecond`. The caller owns the returned object.
ChatBackend* createMockBackend(const std::string& modelName, int tokensPerSecond);

// Loads `modelPath` with llama.cpp (CPU-only) and returns a backend for it.
// Blocking: model load takes real time, so call this from a worker thread and
// show a loading screen rather than calling it from the render loop directly.
// Returns nullptr and fills `err` on failure. The caller owns the returned object.
ChatBackend* loadLlamaBackend(const std::string& modelPath, const std::string& displayName,
                              std::string& err);

#pragma once

#include <functional>
#include <string>
#include <vector>

struct ChatMessage {
    enum class Role { User, Assistant };
    Role        role = Role::User;
    std::string text;
};

typedef std::function<void(const std::string& piece)> TokenCallback;

typedef std::function<void(bool ok, const std::string& err)> DoneCallback;

class ChatBackend {
public:
    virtual ~ChatBackend() {}

    virtual std::string modelName() const = 0;


    virtual void generate(const std::vector<ChatMessage>& history, TokenCallback onToken,
                          DoneCallback onDone) = 0;


    virtual void cancel() = 0;
};


ChatBackend* createMockBackend(const std::string& modelName, int tokensPerSecond);

ChatBackend* loadLlamaBackend(const std::string& modelPath, const std::string& displayName,
                              std::string& err);

// chat_store.hpp - saves and lists multiple chats per model on the SD card.
//
// Layout: sdmc:/switch/llm/chats/<model-stem>/chat_<id>.bin, one binary file
// per chat, plus a tiny ".next_id" counter file per model directory. No wall
// clock is used (the Switch's RTC isn't initialised by this app and would be
// unreliable across reboots anyway) - "recency" is a monotonically increasing
// sequence number bumped by the counter file on every save, so the chat list
// can still be sorted "most recently touched first" without needing a real
// timestamp.
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "backend.hpp"  // ChatMessage

struct ChatSummary {
    uint32_t    id  = 0;
    uint32_t    seq = 0;  // higher = more recently created/updated
    std::string title;
    int         messageCount = 0;
};

namespace chat_store {

// Turns a model's display name into a filesystem-safe directory name.
std::string modelStem(const std::string& modelDisplayName);

// Lists a model's saved chats, most recently touched first. Cheap: only
// reads each file's header, not the message bodies.
std::vector<ChatSummary> listChats(const std::string& stem);

// Loads one chat's full message history and title.
bool loadChat(const std::string& stem, uint32_t id, std::vector<ChatMessage>& outMessages,
             std::string& outTitle);

// Saves a chat. Pass id=0 to create a new chat (its assigned id is returned);
// pass an existing id to overwrite that chat. `title` is only used when
// creating a new chat - passing an empty title auto-derives one from the
// first user message. Returns 0 on failure.
uint32_t saveChat(const std::string& stem, uint32_t id, const std::string& title,
                  const std::vector<ChatMessage>& messages);

void deleteChat(const std::string& stem, uint32_t id);

}  // namespace chat_store

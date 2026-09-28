








#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "backend.hpp"

struct ChatSummary {
    uint32_t    id  = 0;
    uint32_t    seq = 0;
    std::string title;
    int         messageCount = 0;
};

namespace chat_store {


std::string modelStem(const std::string& modelDisplayName);



std::vector<ChatSummary> listChats(const std::string& stem);


bool loadChat(const std::string& stem, uint32_t id, std::vector<ChatMessage>& outMessages,
             std::string& outTitle);





uint32_t saveChat(const std::string& stem, uint32_t id, const std::string& title,
                  const std::vector<ChatMessage>& messages);

void deleteChat(const std::string& stem, uint32_t id);

}

// chat_store.cpp
#include "chat_store.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chat_store {
namespace {

const char* const kRoot   = "sdmc:/switch/llm/chats";
const uint32_t     kMagic  = 0x4B504C41;  // "ALPK" (byte order doesn't matter - never leaves this device)
const uint32_t     kVersion = 1;
const size_t        kMaxTitleBytes = 200;

std::string dirFor(const std::string& stem) { return std::string(kRoot) + "/" + stem; }
std::string fileFor(const std::string& stem, uint32_t id) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "chat_%u.bin", (unsigned)id);
    return dirFor(stem) + "/" + buf;
}
std::string counterFile(const std::string& stem) { return dirFor(stem) + "/.next_id"; }

void ensureDirs(const std::string& stem) {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/llm", 0777);
    mkdir(kRoot, 0777);
    mkdir(dirFor(stem).c_str(), 0777);
}

// Reads-and-increments a small text counter file. Missing file -> starts at 1.
uint32_t nextSeq(const std::string& stem) {
    const std::string path = counterFile(stem);
    uint32_t          n    = 1;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
        unsigned v = 0;
        if (std::fscanf(f, "%u", &v) == 1) n = v;
        std::fclose(f);
    }
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fprintf(f, "%u", (unsigned)(n + 1));
        std::fclose(f);
    }
    return n;
}

// Truncates to at most `maxBytes`, never splitting a UTF-8 sequence.
std::string safeTruncate(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && (((unsigned char)s[cut]) & 0xC0) == 0x80) cut--;  // back up off a continuation byte
    return s.substr(0, cut);
}

std::string deriveTitle(const std::vector<ChatMessage>& messages) {
    for (size_t i = 0; i < messages.size(); i++) {
        if (messages[i].role != ChatMessage::Role::User) continue;
        std::string t = messages[i].text;
        size_t      nl = t.find('\n');
        if (nl != std::string::npos) t = t.substr(0, nl);
        return safeTruncate(t, kMaxTitleBytes);
    }
    return "New chat";
}

bool writeU32(FILE* f, uint32_t v) { return std::fwrite(&v, sizeof v, 1, f) == 1; }
bool readU32(FILE* f, uint32_t& v) { return std::fread(&v, sizeof v, 1, f) == 1; }

bool writeString(FILE* f, const std::string& s) {
    if (!writeU32(f, (uint32_t)s.size())) return false;
    if (s.empty()) return true;
    return std::fwrite(s.data(), 1, s.size(), f) == s.size();
}

bool readString(FILE* f, std::string& s, size_t capBytes) {
    uint32_t len = 0;
    if (!readU32(f, len)) return false;
    if (len > capBytes) return false;  // corrupt/foreign file - refuse rather than allocate wildly
    s.resize(len);
    if (len == 0) return true;
    return std::fread(&s[0], 1, len, f) == len;
}

}  // namespace

std::string modelStem(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = (unsigned char)name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.') {
            out += (char)c;
        } else {
            out += '_';
        }
    }
    if (out.empty()) out = "model";
    return out;
}

std::vector<ChatSummary> listChats(const std::string& stem) {
    std::vector<ChatSummary> out;
    DIR*                     d = opendir(dirFor(stem).c_str());
    if (!d) return out;

    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() < 10 || n.compare(0, 5, "chat_") != 0) continue;  // "chat_X.bin", X >= 1 digit

        FILE* f = std::fopen((dirFor(stem) + "/" + n).c_str(), "rb");
        if (!f) continue;

        ChatSummary s;
        uint32_t    magic = 0, version = 0, msgCount = 0;
        bool        ok = readU32(f, magic) && magic == kMagic && readU32(f, version) &&
                 version == kVersion && readU32(f, s.id) && readU32(f, s.seq) &&
                 readString(f, s.title, kMaxTitleBytes) && readU32(f, msgCount);
        std::fclose(f);
        if (!ok) continue;

        s.messageCount = (int)msgCount;
        out.push_back(s);
    }
    closedir(d);

    std::sort(out.begin(), out.end(), [](const ChatSummary& a, const ChatSummary& b) { return a.seq > b.seq; });
    return out;
}

bool loadChat(const std::string& stem, uint32_t id, std::vector<ChatMessage>& outMessages,
             std::string& outTitle) {
    FILE* f = std::fopen(fileFor(stem, id).c_str(), "rb");
    if (!f) return false;

    uint32_t magic = 0, version = 0, fileId = 0, seq = 0, msgCount = 0;
    bool     ok = readU32(f, magic) && magic == kMagic && readU32(f, version) && version == kVersion &&
              readU32(f, fileId) && readU32(f, seq) && readString(f, outTitle, kMaxTitleBytes) &&
              readU32(f, msgCount);
    if (!ok) {
        std::fclose(f);
        return false;
    }

    outMessages.clear();
    outMessages.reserve(msgCount);
    const size_t kMaxMessageBytes = 1u << 20;  // 1 MiB/message sanity cap against corrupt files
    for (uint32_t i = 0; i < msgCount && ok; i++) {
        uint8_t roleByte = 0;
        ok               = std::fread(&roleByte, sizeof roleByte, 1, f) == 1;
        ChatMessage m;
        m.role = roleByte == 0 ? ChatMessage::Role::User : ChatMessage::Role::Assistant;
        ok     = ok && readString(f, m.text, kMaxMessageBytes);
        if (ok) outMessages.push_back(m);
    }
    std::fclose(f);
    return ok;
}

uint32_t saveChat(const std::string& stem, uint32_t id, const std::string& title,
                  const std::vector<ChatMessage>& messages) {
    ensureDirs(stem);
    const uint32_t seq = nextSeq(stem);
    if (id == 0) {
        // Chat ids and the recency counter share one sequence: fine, ids just
        // won't be consecutive. Simpler than keeping two counter files in sync.
        id = seq;
    }

    FILE* f = std::fopen(fileFor(stem, id).c_str(), "wb");
    if (!f) return 0;

    const std::string finalTitle = !title.empty() ? safeTruncate(title, kMaxTitleBytes) : deriveTitle(messages);

    bool ok = writeU32(f, kMagic) && writeU32(f, kVersion) && writeU32(f, id) && writeU32(f, seq) &&
             writeString(f, finalTitle) && writeU32(f, (uint32_t)messages.size());
    for (size_t i = 0; ok && i < messages.size(); i++) {
        uint8_t roleByte = messages[i].role == ChatMessage::Role::User ? 0 : 1;
        ok               = std::fwrite(&roleByte, sizeof roleByte, 1, f) == 1;
        ok               = ok && writeString(f, messages[i].text);
    }
    std::fclose(f);
    if (!ok) {
        std::remove(fileFor(stem, id).c_str());  // don't leave a half-written file behind
        return 0;
    }
    return id;
}

void deleteChat(const std::string& stem, uint32_t id) { std::remove(fileFor(stem, id).c_str()); }

}  // namespace chat_store

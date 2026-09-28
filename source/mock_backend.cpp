
#include "backend.hpp"

#include <switch.h>

#include <atomic>
#include <memory>

namespace {

struct Job {
    std::string        reply;
    TokenCallback      onToken;
    DoneCallback       onDone;
    int                tokenDelayMs = 200;
    std::atomic<bool>* cancel       = nullptr;
};


void sleepMs(int ms, const std::atomic<bool>* cancel) {
    while (ms > 0 && !cancel->load()) {
        const int slice = ms < 20 ? ms : 20;
        svcSleepThread((s64)slice * 1000000LL);
        ms -= slice;
    }
}

void workerMain(void* arg) {
    Job* j = static_cast<Job*>(arg);

    sleepMs(900, j->cancel);

    const std::string& r = j->reply;
    size_t i = 0;
    while (i < r.size() && !j->cancel->load()) {


        const size_t start = i;
        if (r[i] == '\n') {
            i++;
        } else {
            while (i < r.size() && r[i] == ' ') i++;
            while (i < r.size() && r[i] != ' ' && r[i] != '\n') i++;
        }
        j->onToken(r.substr(start, i - start));
        sleepMs(j->tokenDelayMs, j->cancel);
    }

    if (j->cancel->load())
        j->onDone(false, "cancelled");
    else
        j->onDone(true, "");
}

std::string buildReply(const std::vector<ChatMessage>& history) {
    std::string lastUser;
    for (size_t i = history.size(); i > 0; i--) {
        if (history[i - 1].role == ChatMessage::Role::User) {
            lastUser = history[i - 1].text;
            break;
        }
    }

    std::string r;
    r += "Sure! This is a test reply from the mock backend, so we can check the chat layout.\n\n";
    r += "The quick brown fox jumps over the lazy dog. Gr\xC3\xBC\xC3\x9F" "e aus Hessen - umlauts like "
         "\xC3\xA4, \xC3\xB6, \xC3\xBC and \xC3\x9F should render fine.\n\n";
    r += "Things to check:\n";
    r += "- word wrapping: this line is long enough that it has to wrap around at least once or twice "
         "inside the bubble\n";
    r += "- scrolling with the D-Pad and the right stick while text is still streaming in\n";
    r += "- a very long word: Supercalifragilisticexpialidocious_Supercalifragilisticexpialidocious_"
         "Supercalifragilisticexpialidocious_Supercalifragilisticexpialidocious\n\n";
    r += "You said: \"" + lastUser + "\"";
    return r;
}

class MockBackend : public ChatBackend {
public:
    MockBackend(const std::string& name, int tokensPerSecond)
        : name_(name), delayMs_(1000 / (tokensPerSecond < 1 ? 1 : tokensPerSecond)) {}

    ~MockBackend() override {
        cancel();
        join();
    }

    std::string modelName() const override { return name_; }

    void generate(const std::vector<ChatMessage>& history, TokenCallback onToken,
                  DoneCallback onDone) override {
        join();
        cancel_.store(false);

        job_.reset(new Job());
        job_->reply        = buildReply(history);
        job_->onToken      = onToken;
        job_->onDone       = onDone;
        job_->tokenDelayMs = delayMs_;
        job_->cancel       = &cancel_;

        Result rc = threadCreate(&thread_, &workerMain, job_.get(), NULL, 0x20000, 0x2C, -2);
        if (R_SUCCEEDED(rc)) rc = threadStart(&thread_);
        if (R_FAILED(rc)) {
            onDone(false, "could not start worker thread");
            return;
        }
        running_ = true;
    }

    void cancel() override { cancel_.store(true); }

private:
    void join() {
        if (running_) {
            threadWaitForExit(&thread_);
            threadClose(&thread_);
            running_ = false;
        }
    }

    std::string       name_;
    int               delayMs_;
    std::atomic<bool> cancel_{false};
    std::unique_ptr<Job> job_;
    Thread            thread_;
    bool              running_ = false;
};

}

ChatBackend* createMockBackend(const std::string& modelName, int tokensPerSecond) {
    return new MockBackend(modelName, tokensPerSecond);
}

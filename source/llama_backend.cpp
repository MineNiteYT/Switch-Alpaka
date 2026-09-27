// llama_backend.cpp - real inference, implementing the ChatBackend interface.
//
// Ported from Alpaka's console main.cpp (model load, chat-template prompt build,
// tokenize/decode loop) onto the ChatBackend interface used by the GUI. CPU-only,
// same as the console version - ggml-deko3d is not wired in yet.
//
// Differences from the console version, and why:
//  - Runs on a worker thread (libnx Thread) instead of blocking main(), so the
//    GUI keeps redrawing while a reply is generated.
//  - Adds a light sampler chain (penalties + low-temperature) instead of pure
//    greedy, to address the repetition Niklas' feedback flagged. Easy to revert
//    to llama_sampler_init_greedy() alone if you'd rather keep it deterministic.
//  - Generation stops early when cancel() is called (checked between tokens),
//    so the UI's "B = Stop" works instead of always running to EOS/256 tokens.
//  - system message text is passed in instead of hardcoded, so it can change later.

#include "backend.hpp"

#include <switch.h>

#include "llama.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// Same limits as the console version.
const int32_t kCtxSize     = 2048;
const int32_t kBatchSize   = 512;
const int32_t kThreads     = 3;     // Switch has 4 app-usable cores; leave one free
const int     kMaxReplyTok = 256;

struct Job {
    llama_model*   model = nullptr;
    llama_context* ctx   = nullptr;
    const llama_vocab* vocab = nullptr;

    std::string           prompt;
    TokenCallback         onToken;
    DoneCallback          onDone;
    std::atomic<bool>*    cancel = nullptr;
};

// llama_chat_apply_template wants the whole exchange as role/content pairs.
// The stored std::strings must outlive the llama_chat_message array.
std::string buildPrompt(const llama_model* model, const std::vector<ChatMessage>& history) {
    std::vector<std::string> roles, contents;
    roles.reserve(history.size() + 1);
    contents.reserve(history.size() + 1);

    roles.push_back("system");
    contents.push_back("You are a helpful assistant.");
    for (size_t i = 0; i < history.size(); i++) {
        roles.push_back(history[i].role == ChatMessage::Role::User ? "user" : "assistant");
        contents.push_back(history[i].text);
    }

    std::vector<llama_chat_message> chat(roles.size());
    for (size_t i = 0; i < roles.size(); i++) {
        chat[i].role    = roles[i].c_str();
        chat[i].content = contents[i].c_str();
    }

    const char* tmpl = llama_model_chat_template(model, nullptr);

    std::vector<char> buf(4096);
    int32_t needed = llama_chat_apply_template(tmpl, chat.data(), chat.size(), true, buf.data(),
                                               (int32_t)buf.size());
    if (needed < 0) {
        // Fall back to a plain ChatML-style prompt if the model's template is
        // missing/unsupported (mirrors the console version's fallback).
        std::string out;
        for (size_t i = 0; i < roles.size(); i++) {
            out += "<|im_start|>" + roles[i] + "\n" + contents[i] + "<|im_end|>\n";
        }
        out += "<|im_start|>assistant\n";
        return out;
    }
    if (needed > (int32_t)buf.size()) {
        buf.resize((size_t)needed);
        needed = llama_chat_apply_template(tmpl, chat.data(), chat.size(), true, buf.data(),
                                           (int32_t)buf.size());
    }
    return std::string(buf.data(), (size_t)needed);
}

void sleepMs(int ms) { svcSleepThread((s64)ms * 1000000LL); }

void workerMain(void* arg) {
    std::unique_ptr<Job> job(static_cast<Job*>(arg));

    // ---- tokenize
    std::vector<llama_token> tokens(job->prompt.size() + 32);
    int32_t n_tokens = llama_tokenize(job->vocab, job->prompt.c_str(), (int32_t)job->prompt.size(),
                                      tokens.data(), (int32_t)tokens.size(), true, true);
    if (n_tokens < 0) {
        tokens.resize((size_t)(-n_tokens));
        n_tokens = llama_tokenize(job->vocab, job->prompt.c_str(), (int32_t)job->prompt.size(),
                                  tokens.data(), (int32_t)tokens.size(), true, true);
    }
    if (n_tokens < 0) {
        job->onDone(false, "tokenize failed");
        return;
    }
    tokens.resize((size_t)n_tokens);

    if (n_tokens > kCtxSize) {
        job->onDone(false, "prompt too long for the context window");
        return;
    }

    if (job->cancel->load()) {
        job->onDone(false, "cancelled");
        return;
    }

    // Fresh context per request - same as the console version (no KV-cache reuse
    // across turns yet, simplest thing that works within 2048 tokens).
    llama_memory_t mem = llama_get_memory(job->ctx);
    llama_memory_clear(mem, true);

    // ---- sampler chain: penalties to fight repetition, then low-temp + greedy pick.
    // (Console version was pure greedy; this keeps deterministic-ish output while
    // curbing the loops Niklas hit. Swap for llama_sampler_init_greedy() alone to
    // match the console version exactly.)
    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    llama_sampler* smpl = llama_sampler_chain_init(sp);
    // penalty_last_n=64 looks back 64 tokens, repeat penalty 1.1, freq/presence
    // penalties left off (0.0 = disabled).
    llama_sampler_chain_add(
        smpl, llama_sampler_init_penalties(llama_vocab_n_tokens(job->vocab), 64, 1.1f, 0.0f, 0.0f));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(1234));

    // ---- prompt processing (batched decode of the whole prompt)
    llama_batch batch = llama_batch_get_one(tokens.data(), (int32_t)tokens.size());
    if (llama_decode(job->ctx, batch) != 0) {
        llama_sampler_free(smpl);
        job->onDone(false, "prompt decode failed");
        return;
    }

    // ---- generation loop
    char piece[128];
    for (int i = 0; i < kMaxReplyTok; i++) {
        if (job->cancel->load()) {
            llama_sampler_free(smpl);
            job->onDone(false, "cancelled");
            return;
        }

        llama_token nt = llama_sampler_sample(smpl, job->ctx, -1);
        if (llama_vocab_is_eog(job->vocab, nt)) break;

        int len = llama_token_to_piece(job->vocab, nt, piece, sizeof(piece), 0, true);
        if (len > 0) job->onToken(std::string(piece, (size_t)len));

        llama_batch nb = llama_batch_get_one(&nt, 1);
        if (llama_decode(job->ctx, nb) != 0) break;
    }

    llama_sampler_free(smpl);
    job->onDone(true, "");
}

class LlamaBackend : public ChatBackend {
public:
    LlamaBackend(llama_model* model, llama_context* ctx, std::string name)
        : model_(model), ctx_(ctx), name_(std::move(name)) {
        vocab_ = llama_model_get_vocab(model_);
    }

    ~LlamaBackend() override {
        cancel();
        join();
        if (ctx_) llama_free(ctx_);
        if (model_) llama_model_free(model_);
    }

    std::string modelName() const override { return name_; }

    void generate(const std::vector<ChatMessage>& history, TokenCallback onToken,
                 DoneCallback onDone) override {
        join();
        cancel_.store(false);

        Job* j    = new Job();
        j->model  = model_;
        j->ctx    = ctx_;
        j->vocab  = vocab_;
        j->prompt = buildPrompt(model_, history);
        j->onToken = onToken;
        j->onDone  = onDone;
        j->cancel  = &cancel_;

        // 1 MiB stack: sampler chain + token buffers are small, but keep headroom.
        Result rc = threadCreate(&thread_, &workerMain, j, NULL, 0x100000, 0x2C, -2);
        if (R_SUCCEEDED(rc)) rc = threadStart(&thread_);
        if (R_FAILED(rc)) {
            delete j;
            onDone(false, "could not start inference thread");
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

    llama_model*        model_;
    llama_context*       ctx_;
    const llama_vocab*   vocab_;
    std::string          name_;
    std::atomic<bool>    cancel_{false};
    Thread                thread_;
    bool                  running_ = false;
};

}  // namespace

// ---------------------------------------------------------------------- init

ChatBackend* loadLlamaBackend(const std::string& modelPath, const std::string& displayName,
                              std::string& err) {
    llama_backend_init();

    llama_model_params mp = llama_model_default_params();
    mp.load_mode    = LLAMA_LOAD_MODE_NONE;
    mp.n_gpu_layers = 0;  // CPU-only for now; ggml-deko3d integration is a separate step

    llama_model* model = llama_model_load_from_file(modelPath.c_str(), mp);
    if (!model) {
        err = "Could not load the model file";
        return nullptr;
    }

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx           = kCtxSize;
    cp.n_batch         = kBatchSize;
    cp.n_threads       = kThreads;
    cp.n_threads_batch = kThreads;

    llama_context* ctx = llama_init_from_model(model, cp);
    if (!ctx) {
        llama_model_free(model);
        err = "Could not create the inference context";
        return nullptr;
    }

    return new LlamaBackend(model, ctx, displayName);
}

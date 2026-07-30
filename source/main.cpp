#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <algorithm>
#include <sys/stat.h>

#include "llama.h"

static const std::string MODELS_DIR = "sdmc:/switch/llm/models";
static const std::string CHATS_DIR  = "sdmc:/switch/llm/chats";

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

struct ModelEntry {
    std::string filename;
    std::string full_path;
};

static std::vector<ModelEntry> scan_models(const std::string & dir) {
    std::vector<ModelEntry> result;
    DIR * d = opendir(dir.c_str());
    if (!d) return result;

    struct dirent * entry;
    while ((entry = readdir(d)) != NULL) {
        std::string name = entry->d_name;
        if (name.size() > 5 && name.substr(name.size() - 5) == ".gguf") {
            result.push_back({ name, dir + "/" + name });
        }
    }
    closedir(d);
    std::sort(result.begin(), result.end(),
              [](const ModelEntry & a, const ModelEntry & b) { return a.filename < b.filename; });
    return result;
}

static void ensure_dir(const std::string & path) {
    mkdir(path.c_str(), 0777);
}

static std::vector<std::string> scan_chats(const std::string & dir) {
    std::vector<std::string> result;
    DIR * d = opendir(dir.c_str());
    if (!d) return result;

    struct dirent * entry;
    while ((entry = readdir(d)) != NULL) {
        std::string name = entry->d_name;
        if (name.size() > 4 && name.substr(name.size() - 4) == ".txt") {
            result.push_back(name);
        }
    }
    closedir(d);
    std::sort(result.begin(), result.end());
    return result;
}

// Opens the Switch software keyboard and returns the typed text.
static std::string ask_question() {
    char buf[512];
    memset(buf, 0, sizeof(buf));

    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) return "";

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, "Frage an das LLM eingeben");
    swkbdConfigSetStringLenMax(&kbd, sizeof(buf) - 1);

    rc = swkbdShow(&kbd, buf, sizeof(buf));
    swkbdClose(&kbd);

    if (R_FAILED(rc)) return "";
    return std::string(buf);
}

// One entry of chat history, either "user" or "assistant".
struct ChatMessage {
    std::string role;
    std::string text;
};

static std::vector<ChatMessage> load_chat(const std::string & path) {
    std::vector<ChatMessage> msgs;
    FILE * f = fopen(path.c_str(), "r");
    if (!f) return msgs;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        std::string l(line);
        if (!l.empty() && l.back() == '\n') l.pop_back();
        size_t tab = l.find('\t');
        if (tab == std::string::npos) continue;
        ChatMessage m;
        m.role = l.substr(0, tab);
        m.text = l.substr(tab + 1);
        msgs.push_back(m);
    }
    fclose(f);
    return msgs;
}

static void append_chat(const std::string & path, const std::string & role, const std::string & text) {
    FILE * f = fopen(path.c_str(), "a");
    if (!f) return;
    // Replace tabs/newlines in text so the simple format never breaks.
    std::string clean = text;
    for (auto & c : clean) if (c == '\t' || c == '\n') c = ' ';
    fprintf(f, "%s\t%s\n", role.c_str(), clean.c_str());
    fclose(f);
}

// Builds a Qwen-style chat-template prompt from the full message history.
static std::string build_prompt(const std::vector<ChatMessage> & history) {
    std::string prompt = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n";
    for (auto & m : history) {
        prompt += "<|im_start|>" + m.role + "\n" + m.text + "<|im_end|>\n";
    }
    prompt += "<|im_start|>assistant\n";
    return prompt;
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

enum class Screen { MODEL_PICKER, CHAT_PICKER, CHAT };

int main(int argc, char * argv[]) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    ensure_dir(CHATS_DIR);

    std::vector<ModelEntry> models = scan_models(MODELS_DIR);
    int model_sel = 0;

    Screen screen = Screen::MODEL_PICKER;

    std::string model_chats_dir;
    std::vector<std::string> chat_files;
    int chat_sel = 0;

    std::string current_chat_path;
    std::vector<ChatMessage> history;

    llama_model * model = nullptr;
    llama_context * ctx = nullptr;
    const llama_vocab * vocab = nullptr;
    llama_sampler * smpl = nullptr;

    bool running = true;
    while (running && appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) {
            running = false;
            break;
        }

        // -------------------------------------------------------------
        if (screen == Screen::MODEL_PICKER) {
            consoleClear();
            printf("=== Modell auswaehlen ===\n\n");
            if (models.empty()) {
                printf("Keine .gguf Dateien gefunden in:\n%s\n", MODELS_DIR.c_str());
            } else {
                for (size_t i = 0; i < models.size(); i++) {
                    printf("%s %s\n", ((int)i == model_sel ? ">" : " "), models[i].filename.c_str());
                }
            }
            printf("\nSteuerkreuz: waehlen   A: bestaetigen   +: beenden\n");
            consoleUpdate(NULL);

            if (!models.empty()) {
                if (kDown & HidNpadButton_Down) model_sel = (model_sel + 1) % (int)models.size();
                if (kDown & HidNpadButton_Up)   model_sel = (model_sel - 1 + (int)models.size()) % (int)models.size();
                if (kDown & HidNpadButton_A) {
                    // Prepare per-model chat folder
                    model_chats_dir = CHATS_DIR + "/" + models[model_sel].filename;
                    ensure_dir(model_chats_dir);
                    chat_files = scan_chats(model_chats_dir);
                    chat_sel = 0;
                    screen = Screen::CHAT_PICKER;
                }
            }
        }
        // -------------------------------------------------------------
        else if (screen == Screen::CHAT_PICKER) {
            consoleClear();
            printf("=== Chat auswaehlen (%s) ===\n\n", models[model_sel].filename.c_str());
            printf("%s Neuer Chat\n", (chat_sel == 0 ? ">" : " "));
            for (size_t i = 0; i < chat_files.size(); i++) {
                printf("%s %s\n", ((int)(i + 1) == chat_sel ? ">" : " "), chat_files[i].c_str());
            }
            printf("\nSteuerkreuz: waehlen   A: bestaetigen   B: zurueck   +: beenden\n");
            consoleUpdate(NULL);

            int total = (int)chat_files.size() + 1;
            if (kDown & HidNpadButton_Down) chat_sel = (chat_sel + 1) % total;
            if (kDown & HidNpadButton_Up)   chat_sel = (chat_sel - 1 + total) % total;
            if (kDown & HidNpadButton_B)    screen = Screen::MODEL_PICKER;

            if (kDown & HidNpadButton_A) {
                if (chat_sel == 0) {
                    // New chat: filename = timestamp
                    time_t t = time(NULL);
                    char fname[64];
                    strftime(fname, sizeof(fname), "%Y%m%d_%H%M%S.txt", localtime(&t));
                    current_chat_path = model_chats_dir + "/" + fname;
                    history.clear();
                } else {
                    current_chat_path = model_chats_dir + "/" + chat_files[chat_sel - 1];
                    history = load_chat(current_chat_path);
                }

                // Load the model now, only once we actually need it.
                printf("\nLade Modell, bitte warten...\n");
                consoleUpdate(NULL);

                llama_backend_init();
                llama_model_params mp = llama_model_default_params();
                mp.load_mode = LLAMA_LOAD_MODE_NONE;
                mp.n_gpu_layers = 0;
                model = llama_model_load_from_file(models[model_sel].full_path.c_str(), mp);

                if (!model) {
                    printf("FEHLER: Modell konnte nicht geladen werden.\n");
                    consoleUpdate(NULL);
                    screen = Screen::MODEL_PICKER;
                    continue;
                }

                vocab = llama_model_get_vocab(model);
                llama_context_params cp = llama_context_default_params();
                cp.n_ctx = 2048;
                cp.n_batch = 512;
                cp.n_threads = 3;
                cp.n_threads_batch = 3;
                ctx = llama_init_from_model(model, cp);

                auto sp = llama_sampler_chain_default_params();
                smpl = llama_sampler_chain_init(sp);
                llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

                screen = Screen::CHAT;
                consoleClear();
                printf("=== Chat (%s) ===\n\n", models[model_sel].filename.c_str());
                for (auto & m : history) {
                    printf("%s: %s\n\n", m.role.c_str(), m.text.c_str());
                }
                printf("A: Frage eingeben   B: zurueck zur Chatliste   +: beenden\n");
                consoleUpdate(NULL);
            }
        }
        // -------------------------------------------------------------
        else if (screen == Screen::CHAT) {
            if (kDown & HidNpadButton_B) {
                llama_sampler_free(smpl);  smpl = nullptr;
                llama_free(ctx);           ctx = nullptr;
                llama_model_free(model);   model = nullptr;
                llama_backend_free();
                chat_files = scan_chats(model_chats_dir);
                chat_sel = 0;
                screen = Screen::CHAT_PICKER;
                continue;
            }

            if (kDown & HidNpadButton_A) {
                std::string question = ask_question();
                if (!question.empty()) {
                    history.push_back({ "user", question });
                    append_chat(current_chat_path, "user", question);

                    printf("\nuser: %s\n\nassistant: ", question.c_str());
                    consoleUpdate(NULL);

                    std::string prompt = build_prompt(history);
                    std::vector<llama_token> tokens(2048);
                    int n_tokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(),
                                                   tokens.data(), tokens.size(), true, true);
                    if (n_tokens < 0) {
                        tokens.resize(-n_tokens);
                        n_tokens = llama_tokenize(vocab, prompt.c_str(), prompt.size(),
                                                   tokens.data(), tokens.size(), true, true);
                    }
                    tokens.resize(n_tokens);

                    llama_memory_t mem = llama_get_memory(ctx);
                    llama_memory_clear(mem, true);

                    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
                    if (llama_decode(ctx, batch) != 0) {
                        printf("\nFEHLER: decode fehlgeschlagen.\n");
                        consoleUpdate(NULL);
                    } else {
                        std::string answer;
                        for (int i = 0; i < 256; i++) {
                            llama_token nt = llama_sampler_sample(smpl, ctx, -1);
                            if (llama_vocab_is_eog(vocab, nt)) break;

                            char piece[128];
                            int len = llama_token_to_piece(vocab, nt, piece, sizeof(piece), 0, true);
                            if (len > 0) {
                                fwrite(piece, 1, len, stdout);
                                consoleUpdate(NULL);
                                answer.append(piece, len);
                            }

                            llama_batch nb = llama_batch_get_one(&nt, 1);
                            if (llama_decode(ctx, nb) != 0) break;
                        }
                        history.push_back({ "assistant", answer });
                        append_chat(current_chat_path, "assistant", answer);
                    }

                    printf("\n\nA: Frage eingeben   B: zurueck   +: beenden\n");
                    consoleUpdate(NULL);
                }
            }
        }

        consoleUpdate(NULL);
    }

    if (smpl) llama_sampler_free(smpl);
    if (ctx)  llama_free(ctx);
    if (model) llama_model_free(model);
    if (model) llama_backend_free();

    consoleExit(NULL);
    return 0;
}

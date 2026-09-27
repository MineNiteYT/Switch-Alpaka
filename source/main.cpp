// Alpaka GUI - Stage 2
//   * model picker (scans sdmc:/switch/llm/models/*.gguf)
//   * chat screen with streaming, wrapping, scrolling and swkbd input
//   * a MOCK backend streams a canned reply - the llama.cpp glue comes later
//
// Controls: see the footer of each screen. Debug panel: - (minus). Exit: + (plus).

#include <switch.h>

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "backend.hpp"
#include "chat.hpp"
#include "chat_list.hpp"
#include "chat_store.hpp"
#include "gui.hpp"

namespace {

using gui::FontSize;
namespace col = gui::col;

const char* const kModelDir = "sdmc:/switch/llm/models";

// ---------------------------------------------------------------- model list

struct ModelEntry {
    std::string name;
    std::string path;
    u64         size = 0;
};

std::vector<ModelEntry> scanModels() {
    std::vector<ModelEntry> out;
    DIR* d = opendir(kModelDir);
    if (!d) return out;

    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() < 5) continue;
        std::string ext = n.substr(n.size() - 5);
        for (size_t i = 0; i < ext.size(); i++) ext[i] = (char)std::tolower((unsigned char)ext[i]);
        if (ext != ".gguf") continue;

        ModelEntry m;
        m.name = n;
        m.path = std::string(kModelDir) + "/" + n;
        struct stat st;
        if (stat(m.path.c_str(), &st) == 0) m.size = (u64)st.st_size;
        out.push_back(m);
    }
    closedir(d);

    std::sort(out.begin(), out.end(),
              [](const ModelEntry& a, const ModelEntry& b) { return a.name < b.name; });
    return out;
}

std::string formatSize(u64 bytes) {
    char buf[32];
    if (bytes >= (1ULL << 30))
        std::snprintf(buf, sizeof buf, "%.2f GiB", (double)bytes / (double)(1ULL << 30));
    else
        std::snprintf(buf, sizeof buf, "%.1f MiB", (double)bytes / (double)(1ULL << 20));
    return buf;
}

std::string stripExtension(const std::string& n) {
    if (n.size() > 5) return n.substr(0, n.size() - 5);  // ".gguf"
    return n;
}

// Fires on the initial press, then repeats while the button stays held (~60 fps).
bool repeatFire(u64 down, u64 held, u64 mask, int& counter) {
    if (down & mask) {
        counter = 0;
        return true;
    }
    if (held & mask) {
        counter++;
        return counter > 20 && (counter - 20) % 4 == 0;
    }
    counter = 0;
    return false;
}

// -------------------------------------------------------------- picker screen

const int kRowH = 64, kRowGap = 6, kVisible = 8;
const int kListX = 40, kListY = 150, kListW = 720;

struct PickerState {
    std::vector<ModelEntry> models;
    int                     sel = 0, scroll = 0;
    int                     upCnt = 0, downCnt = 0;
    std::string             toast;
    int                     toastFrames = 0;
};

// Returns true when the user picked a model (A).
bool updatePicker(PickerState& ps, u64 down, u64 held) {
    const int n = (int)ps.models.size();
    bool open = false;

    if (n > 0) {
        if (repeatFire(down, held, HidNpadButton_Up, ps.upCnt)) ps.sel = std::max(0, ps.sel - 1);
        if (repeatFire(down, held, HidNpadButton_Down, ps.downCnt)) ps.sel = std::min(n - 1, ps.sel + 1);
        if (down & HidNpadButton_L) ps.sel = std::max(0, ps.sel - kVisible);
        if (down & HidNpadButton_R) ps.sel = std::min(n - 1, ps.sel + kVisible);
        if (down & HidNpadButton_A) open = true;
    }
    if (down & HidNpadButton_Y) {
        ps.models = scanModels();
        ps.sel = ps.scroll = 0;
        ps.toast = "Model list refreshed (" + std::to_string(ps.models.size()) + " found)";
        ps.toastFrames = 120;
    }

    if (ps.sel < ps.scroll) ps.scroll = ps.sel;
    if (ps.sel >= ps.scroll + kVisible) ps.scroll = ps.sel - kVisible + 1;
    if (ps.toastFrames > 0) ps.toastFrames--;
    return open;
}

void drawPicker(const PickerState& ps, int fps) {
    const int n = (int)ps.models.size();

    gui::drawHeader("Alpaka", FontSize::Title, "Local LLMs on Nintendo Switch", col::textDim);

    gui::drawText(kListX, 106, "MODELS", FontSize::Small, col::textDim);
    if (n > 0) {
        gui::drawTextRight(kListX + kListW, 106,
                           std::to_string(ps.sel + 1) + " / " + std::to_string(n), FontSize::Small,
                           col::textDim);
    }

    if (n == 0) {
        const int cx = kListX + kListW / 2;
        gui::fillRoundRect(kListX, kListY, kListW, 300, 14, col::panel);
        gui::drawTextCentered(cx, kListY + 90, "No models found", FontSize::Large, col::text);
        gui::drawTextCentered(cx, kListY + 150, "Copy .gguf files to", FontSize::Normal, col::textDim);
        gui::drawTextCentered(cx, kListY + 186, kModelDir, FontSize::Normal, col::accent);
        gui::drawTextCentered(cx, kListY + 236, "then press Y to rescan", FontSize::Normal, col::textDim);
    } else {
        const int last = std::min(n, ps.scroll + kVisible);
        for (int i = ps.scroll; i < last; i++) {
            const int  y     = kListY + (i - ps.scroll) * (kRowH + kRowGap);
            const bool isSel = (i == ps.sel);

            gui::fillRoundRect(kListX, y, kListW, kRowH, 10, isSel ? col::panelHi : col::panel);
            if (isSel) gui::fillRoundRect(kListX, y + 10, 6, kRowH - 20, 3, col::accent);

            const std::string size  = formatSize(ps.models[i].size);
            const int         sizeW = gui::textWidth(size, FontSize::Small);
            const int         ty    = y + (kRowH - gui::lineHeight(FontSize::Normal)) / 2;
            gui::drawTextClipped(kListX + 24, ty, kListW - 24 - sizeW - 48, ps.models[i].name,
                                 FontSize::Normal, isSel ? col::text : col::textDim);
            gui::drawTextRight(kListX + kListW - 24,
                               y + (kRowH - gui::lineHeight(FontSize::Small)) / 2, size, FontSize::Small,
                               col::textDim);
        }

        if (n > kVisible) {
            const int trackH = kVisible * (kRowH + kRowGap) - kRowGap;
            const int thumbH = std::max(30, trackH * kVisible / n);
            const int thumbY = kListY + (trackH - thumbH) * ps.scroll / (n - kVisible);
            gui::fillRoundRect(kListX + kListW + 12, kListY, 6, trackH, 3, col::panel);
            gui::fillRoundRect(kListX + kListW + 12, thumbY, 6, thumbH, 3, col::accent);
        }
    }

    // Detail panel
    const int dx = 820, dy = 106, dw = gui::W - 40 - dx, dh = 530;
    gui::fillRoundRect(dx, dy, dw, dh, 14, col::panel);
    gui::drawText(dx + 28, dy + 20, "DETAILS", FontSize::Small, col::textDim);
    if (n > 0) {
        const ModelEntry& m = ps.models[ps.sel];
        int               y = dy + 64;
        gui::drawText(dx + 28, y, "File", FontSize::Small, col::textDim);
        y += gui::lineHeight(FontSize::Small) + 2;
        const FontSize nameSize =
            gui::textWidth(m.name, FontSize::Normal) <= dw - 56 ? FontSize::Normal : FontSize::Small;
        gui::drawTextClipped(dx + 28, y, dw - 56, m.name, nameSize, col::text);
        y += gui::lineHeight(FontSize::Normal) + 26;

        gui::drawText(dx + 28, y, "Size", FontSize::Small, col::textDim);
        y += gui::lineHeight(FontSize::Small) + 2;
        gui::drawText(dx + 28, y, formatSize(m.size), FontSize::Normal, col::text);
        y += gui::lineHeight(FontSize::Normal) + 26;

        gui::drawText(dx + 28, y, "Location", FontSize::Small, col::textDim);
        y += gui::lineHeight(FontSize::Small) + 2;
        gui::drawTextClipped(dx + 28, y, dw - 56, kModelDir, FontSize::Small, col::text);
    } else {
        gui::drawText(dx + 28, dy + 64, "Nothing selected", FontSize::Normal, col::textDim);
    }

    // Toast
    if (ps.toastFrames > 0) {
        const std::string t  = gui::ellipsize(ps.toast, 900, FontSize::Normal);
        const int         tw = gui::textWidth(t, FontSize::Normal) + 56;
        const int         th = gui::lineHeight(FontSize::Normal) + 24;
        const int         tx = (gui::W - tw) / 2, ty = 588;
        gui::fillRoundRect(tx, ty, tw, th, th / 2, col::accent);
        gui::drawText(tx + 28, ty + 12, t, FontSize::Normal, col::bg);
    }

    std::vector<gui::Hint> hints;
    hints.push_back(gui::Hint(gui::ButtonIcon::A, "Open chat"));
    hints.push_back(gui::Hint(gui::ButtonIcon::Y, "Rescan"));
    hints.push_back(gui::Hint(gui::ButtonIcon::L, "Page up"));
    hints.push_back(gui::Hint(gui::ButtonIcon::R, "Page down"));
    hints.push_back(gui::Hint(gui::ButtonIcon::Plus, "Exit"));
    gui::drawFooter(hints, (fps < 0 ? std::string("--") : std::to_string(fps)) + " FPS");
}

// ------------------------------------------------------------- model loading
//
// llama_model_load_from_file() blocks for real time (reading a multi-GB file
// from the SD card), so it runs on a worker thread while the GUI keeps drawing
// a spinner. Only one load can be in flight at a time.

struct Loader {
    Thread              thread{};
    bool                running = false;
    std::atomic<bool>   done{false};
    ChatBackend*         result = nullptr;
    std::string          err;
    std::string          modelName;   // for the spinner text
    std::string          path, name;  // arguments captured for the worker

    // Passed straight through to the constructed ChatScreen once loading finishes.
    std::string             modelStem;
    uint32_t                chatId = 0;
    std::vector<ChatMessage> initialHistory;

    static void threadMain(void* arg) {
        Loader* l  = static_cast<Loader*>(arg);
        l->result  = loadLlamaBackend(l->path, l->name, l->err);
        l->done.store(true);
    }

    // `stem`/`id`/`history` describe which saved chat (if any) to resume;
    // pass id=0 and an empty history for a brand-new chat.
    void start(const std::string& modelPath, const std::string& displayName, const std::string& stem,
              uint32_t id, std::vector<ChatMessage> history) {
        path           = modelPath;
        name           = displayName;
        modelName      = displayName;
        modelStem      = stem;
        chatId         = id;
        initialHistory = std::move(history);
        err.clear();
        result = nullptr;
        done.store(false);

        Result rc = threadCreate(&thread, &threadMain, this, NULL, 0x40000, 0x2C, -2);
        if (R_SUCCEEDED(rc)) rc = threadStart(&thread);
        if (R_FAILED(rc)) {
            err  = "could not start the loading thread";
            done.store(true);
            return;
        }
        running = true;
    }

    // Call once done.load() is true to release the thread handle.
    void join() {
        if (running) {
            threadWaitForExit(&thread);
            threadClose(&thread);
            running = false;
        }
    }
};

void drawLoading(const std::string& modelName, int frame) {
    gui::drawHeader("Alpaka", FontSize::Title, "Local LLMs on Nintendo Switch", col::textDim);

    const int cx = gui::W / 2, cy = 340;
    const int n = 10, r = 46;
    for (int i = 0; i < n; i++) {
        const float ang = (float)i / n * 6.2831853f;
        const int   x   = cx + (int)(r * __builtin_cosf(ang));
        const int   y   = cy + (int)(r * __builtin_sinf(ang));
        const int   age = (i - frame / 4) % n;
        const int   a   = 60 + 195 * ((age + n) % n) / n;
        gui::fillRoundRect(x - 6, y - 6, 12, 12, 6, gui::rgba(96, 165, 250, (u8)a));
    }

    gui::drawTextCentered(cx, cy + 90, "Loading model...", FontSize::Large, col::text);
    gui::drawTextCentered(cx, cy + 136, modelName, FontSize::Normal, col::textDim);
    gui::drawTextCentered(cx, cy + 176, "This can take a while for larger models", FontSize::Small,
                          col::textDim);

    std::vector<gui::Hint> hints;
    hints.push_back(gui::Hint("Please wait"));
    gui::drawFooter(hints, "");
}

void fatalConsole(const std::string& msg) {
    consoleInit(NULL);
    std::printf("Alpaka GUI failed to start:\n\n%s\n\nPress + to exit.\n", msg.c_str());
    consoleUpdate(NULL);

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
}

}  // namespace

// ----------------------------------------------------------------------- main

int main(int, char**) {
    std::string err;
    if (!gui::init(err)) {
        fatalConsole(err);
        gui::shutdown();
        return 1;
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    PickerState                  picker;
    picker.models = scanModels();
    std::unique_ptr<ChatScreen>     chat;
    std::unique_ptr<ChatListScreen> chatList;
    Loader                          loader;
    bool                            loading = false;

    int  fps = -1, frames = 0, totalFrames = 0;
    u64  lastElapsedMs = 0;
    u64  fpsStart = armGetSystemTick();
    bool showDebug = false;

    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        const u64 held = padGetButtons(&pad);
        if (down & HidNpadButton_Plus) break;
        if (down & HidNpadButton_Minus) showDebug = !showDebug;

        // ------------------------------------------------------------ update
        if (loading) {
            if (loader.done.load()) {
                loader.join();
                loading = false;
                if (loader.result) {
                    chat.reset(new ChatScreen(loader.result, loader.modelStem, loader.chatId,
                                              loader.initialHistory));
                    chatList.reset();  // freed; rebuilt (rescanned) when we come back
                } else if (chatList) {
                    chatList->notifyError("Load failed: " + loader.err);
                } else {
                    picker.toast       = "Load failed: " + loader.err;
                    picker.toastFrames = 220;
                }
            }
        } else if (chat) {
            if (chat->update(pad, down, held) == ChatScreen::Action::Back) {
                chat.reset();  // destructor saves the chat
                if (chatList) chatList->rescan();
            }
        } else if (chatList) {
            const ChatListScreen::Action a = chatList->update(down, held);
            if (a == ChatListScreen::Action::Back) {
                chatList.reset();
            } else if (a == ChatListScreen::Action::Open) {
                const ModelEntry& m    = picker.models[picker.sel];
                const std::string stem = chat_store::modelStem(stripExtension(m.name));
                std::vector<ChatMessage> history;
                std::string              title;  // unused - ChatScreen re-derives it on save
                if (chatList->openId() != 0) chat_store::loadChat(stem, chatList->openId(), history, title);
                loader.start(m.path, stripExtension(m.name), stem, chatList->openId(), history);
                loading = true;
            }
        } else if (updatePicker(picker, down, held)) {
            const ModelEntry& m = picker.models[picker.sel];
            chatList.reset(new ChatListScreen(chat_store::modelStem(stripExtension(m.name)),
                                              stripExtension(m.name)));
        }

        // ------------------------------------------------------------ render
        gui::beginFrame(col::bg);
        if (loading)
            drawLoading(loader.modelName, totalFrames);
        else if (chat)
            chat->draw(fps);
        else if (chatList)
            chatList->draw(fps);
        else
            drawPicker(picker, fps);

        if (showDebug) {
            std::vector<std::string> lines = gui::debugLines();
            char                     b[160];
            std::snprintf(b, sizeof b, "Frames: %d  fps: %d  last window: %llu ms", totalFrames, fps,
                          (unsigned long long)lastElapsedMs);
            lines.push_back(b);

            const int px = 40, pw = 1200;
            const int lh = gui::lineHeight(FontSize::Small);
            const int ph = (int)lines.size() * lh + 32;
            const int py = 640 - ph;
            gui::fillRoundRect(px, py, pw, ph, 12, gui::rgba(0, 0, 0, 235));
            for (size_t i = 0; i < lines.size(); i++)
                gui::drawTextClipped(px + 16, py + 16 + (int)i * lh, pw - 32, lines[i], FontSize::Small,
                                     col::ok);
        }
        gui::endFrame();

        // ------------------------------------------------------- FPS counter
        frames++;
        totalFrames++;
        const u64 now       = armGetSystemTick();
        const u64 elapsedNs = armTicksToNs(now - fpsStart);
        lastElapsedMs       = elapsedNs / 1000000ULL;
        if (elapsedNs >= 500000000ULL) {
            fps      = (int)((u64)frames * 1000000000ULL / elapsedNs);
            frames   = 0;
            fpsStart = now;
        }
    }

    chat.reset();  // joins the worker thread
    gui::shutdown();
    return 0;
}

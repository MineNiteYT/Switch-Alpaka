// markdown.cpp
#include "markdown.hpp"

ParsedMarkdown parseInlineMarkdown(const std::string& raw) {
    ParsedMarkdown out;
    out.plain.reserve(raw.size());

    bool   inBold   = false;
    bool   inCode   = false;  // single-backtick inline `code`
    bool   inFence  = false;  // triple-backtick ```fenced code block```
    size_t runStart = 0;

    auto closeRun = [&](MdStyle style) {
        if (out.plain.size() > runStart) out.runs.push_back({runStart, out.plain.size() - runStart, style});
        runStart = out.plain.size();
    };
    auto currentStyle = [&]() {
        return (inFence || inCode) ? MdStyle::Code : (inBold ? MdStyle::Bold : MdStyle::Normal);
    };

    size_t i = 0;
    while (i < raw.size()) {
        const unsigned char c = (unsigned char)raw[i];

        // "```" opens/closes a fenced code block, and always takes priority over a
        // lone "`" - checked first so a fence is never mistaken for three separate
        // inline-code toggles (which would swallow everything after it as "code").
        if (c == '`' && i + 3 <= raw.size() && raw[i + 1] == '`' && raw[i + 2] == '`') {
            closeRun(currentStyle());
            inFence = !inFence;
            i += 3;
            // The rest of the fence's own line (an optional language tag on open, or
            // any trailing junk on close) isn't meant to be displayed - skip it.
            while (i < raw.size() && raw[i] != '\n') i++;
            if (i < raw.size()) i++;  // consume the newline itself too
            continue;
        }

        if (!inFence) {
            // "**" toggles bold, but never inside inline code (code shows markers literally).
            if (!inCode && c == '*' && i + 1 < raw.size() && raw[i + 1] == '*') {
                closeRun(currentStyle());
                inBold = !inBold;
                i += 2;
                continue;
            }
            // A lone "`" toggles inline code.
            if (c == '`') {
                closeRun(currentStyle());
                inCode = !inCode;
                i += 1;
                continue;
            }
        }
        // Inside a fence, a stray "`" or "**" is just literal text - fall through.

        out.plain += (char)c;
        i++;
    }
    closeRun(currentStyle());  // flush the final run, even if something was never closed
    return out;
}


#include "markdown.hpp"

ParsedMarkdown parseInlineMarkdown(const std::string& raw) {
    ParsedMarkdown out;
    out.plain.reserve(raw.size());

    bool   inBold   = false;
    bool   inCode   = false;
    bool   inFence  = false;
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




        if (c == '`' && i + 3 <= raw.size() && raw[i + 1] == '`' && raw[i + 2] == '`') {
            closeRun(currentStyle());
            inFence = !inFence;
            i += 3;


            while (i < raw.size() && raw[i] != '\n') i++;
            if (i < raw.size()) i++;
            continue;
        }

        if (!inFence) {

            if (!inCode && c == '*' && i + 1 < raw.size() && raw[i + 1] == '*') {
                closeRun(currentStyle());
                inBold = !inBold;
                i += 2;
                continue;
            }

            if (c == '`') {
                closeRun(currentStyle());
                inCode = !inCode;
                i += 1;
                continue;
            }
        }


        out.plain += (char)c;
        i++;
    }
    closeRun(currentStyle());
    return out;
}

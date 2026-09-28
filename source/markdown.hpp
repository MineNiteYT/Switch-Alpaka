
















#pragma once

#include <cstddef>
#include <string>
#include <vector>

enum class MdStyle { Normal, Bold, Code };

struct MdRun {
    size_t  start;
    size_t  len;
    MdStyle style;
};

struct ParsedMarkdown {
    std::string        plain;
    std::vector<MdRun> runs;
};

ParsedMarkdown parseInlineMarkdown(const std::string& raw);

// markdown.hpp - lightweight inline markdown for chat bubbles.
//
// Deliberately small: most LLM output only uses **bold**, `code`, and the
// occasional ```fenced code block```. This strips the markers and records
// which byte ranges of the resulting plain text should be drawn how, so the
// renderer (no bold/mono font available - see chat.cpp) can fake it. A fenced
// block's opening line (the ``` plus any language tag, e.g. ```python) is
// dropped entirely rather than shown, since it's not meant to be read; the
// block's content renders with the same style as inline `code`.
//
// NOT handled on purpose (falls back to showing the raw markers as text):
//  - single-asterisk/underscore italics - faking italics without a slanted
//    font is unreliable, and *foo* is ambiguous with plain multiplication
//  - headers, lists, links, block quotes, horizontal rules - would need
//    per-line font-size and layout changes beyond "style this substring
//    differently"
//  - escaped markers ("\*\*not bold\*\*") - rare in LLM output
#pragma once

#include <cstddef>
#include <string>
#include <vector>

enum class MdStyle { Normal, Bold, Code };

struct MdRun {
    size_t  start;  // byte offset into ParsedMarkdown::plain
    size_t  len;
    MdStyle style;
};

struct ParsedMarkdown {
    std::string        plain;  // markers stripped
    std::vector<MdRun> runs;   // contiguous, sorted, covers all of `plain`
};

ParsedMarkdown parseInlineMarkdown(const std::string& raw);

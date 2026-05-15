#pragma once

#include <cstddef>
#include <string>

namespace vanta {

struct TextPosition {
    int line = 0;
    int character = 0;
};

struct TextRange {
    TextPosition start;
    TextPosition end;
};

struct TextEdit {
    TextRange range;
    std::string replacementText;
};

std::size_t offsetForPosition(const std::string& text, TextPosition position);
std::string applyTextEdit(const std::string& text, const TextEdit& edit);

}

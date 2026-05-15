#include "vanta/core/text.h"

#include <algorithm>

namespace vanta {

std::size_t offsetForPosition(const std::string& text, TextPosition position) {
    std::size_t offset = 0;
    int currentLine = 0;
    while (offset < text.size() && currentLine < position.line) {
        if (text[offset] == '\n') {
            ++currentLine;
        }
        ++offset;
    }

    const std::size_t lineStart = offset;
    while (offset < text.size() && text[offset] != '\n' && offset - lineStart < static_cast<std::size_t>(std::max(0, position.character))) {
        ++offset;
    }
    return offset;
}

std::string applyTextEdit(const std::string& text, const TextEdit& edit) {
    const std::size_t start = offsetForPosition(text, edit.range.start);
    const std::size_t end = offsetForPosition(text, edit.range.end);
    std::string result;
    result.reserve(text.size() + edit.replacementText.size());
    result.append(text.substr(0, start));
    result.append(edit.replacementText);
    result.append(text.substr(std::min(end, text.size())));
    return result;
}

}

#include "vanta/platform/json.h"

#include <charconv>
#include <cctype>
#include <cmath>
#include <sstream>

namespace vanta {
namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    Json parse() {
        skipWhitespace();
        Json value = parseValue();
        skipWhitespace();
        if (!eof()) {
            fail("Unexpected trailing characters");
        }
        return value;
    }

private:
    Json parseValue() {
        skipWhitespace();
        if (eof()) {
            fail("Unexpected end of input");
        }

        const char ch = peek();
        if (ch == 'n') {
            consumeLiteral("null");
            return Json(nullptr);
        }
        if (ch == 't') {
            consumeLiteral("true");
            return Json(true);
        }
        if (ch == 'f') {
            consumeLiteral("false");
            return Json(false);
        }
        if (ch == '"') {
            return Json(parseString());
        }
        if (ch == '[') {
            return Json(parseArray());
        }
        if (ch == '{') {
            return Json(parseObject());
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            return parseNumber();
        }

        fail("Unexpected token");
    }

    Json::Array parseArray() {
        expect('[');
        Json::Array result;
        skipWhitespace();
        if (consumeIf(']')) {
            return result;
        }

        while (true) {
            result.push_back(parseValue());
            skipWhitespace();
            if (consumeIf(']')) {
                break;
            }
            expect(',');
        }

        return result;
    }

    Json::Object parseObject() {
        expect('{');
        Json::Object result;
        skipWhitespace();
        if (consumeIf('}')) {
            return result;
        }

        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                fail("Expected object key");
            }
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            result.emplace(std::move(key), parseValue());
            skipWhitespace();
            if (consumeIf('}')) {
                break;
            }
            expect(',');
        }

        return result;
    }

    std::string parseString() {
        expect('"');
        std::string result;
        while (!eof()) {
            char ch = advance();
            if (ch == '"') {
                return result;
            }
            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }
            if (eof()) {
                fail("Unexpected end of escape sequence");
            }
            char escape = advance();
            switch (escape) {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u':
                parseBasicUnicodeEscape(result);
                break;
            default:
                fail("Invalid escape sequence");
            }
        }
        fail("Unterminated string");
    }

    void parseBasicUnicodeEscape(std::string& result) {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            if (eof()) {
                fail("Unexpected end of unicode escape");
            }
            char ch = advance();
            value <<= 4U;
            if (ch >= '0' && ch <= '9') {
                value += static_cast<unsigned>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value += static_cast<unsigned>(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                value += static_cast<unsigned>(ch - 'A' + 10);
            } else {
                fail("Invalid unicode escape");
            }
        }

        if (value <= 0x7F) {
            result.push_back(static_cast<char>(value));
        } else if (value <= 0x7FF) {
            result.push_back(static_cast<char>(0xC0 | ((value >> 6U) & 0x1F)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xE0 | ((value >> 12U) & 0x0F)));
            result.push_back(static_cast<char>(0x80 | ((value >> 6U) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
        }
    }

    Json parseNumber() {
        const std::size_t start = offset_;
        consumeIf('-');
        if (consumeIf('0')) {
            // A leading zero is complete unless a fraction or exponent follows.
        } else {
            consumeDigits();
        }

        bool floating = false;
        if (consumeIf('.')) {
            floating = true;
            consumeDigits();
        }
        if (consumeIf('e') || consumeIf('E')) {
            floating = true;
            consumeIf('+') || consumeIf('-');
            consumeDigits();
        }

        const std::string token = text_.substr(start, offset_ - start);
        if (floating) {
            char* end = nullptr;
            double value = std::strtod(token.c_str(), &end);
            if (end == token.c_str()) {
                fail("Invalid number");
            }
            return Json(value);
        }

        std::int64_t value = 0;
        auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
        if (ec != std::errc() || ptr != token.data() + token.size()) {
            fail("Invalid integer");
        }
        return Json(value);
    }

    void consumeDigits() {
        if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) {
            fail("Expected digit");
        }
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }

    void consumeLiteral(const char* literal) {
        while (*literal != '\0') {
            if (eof() || advance() != *literal) {
                fail("Invalid literal");
            }
            ++literal;
        }
    }

    void skipWhitespace() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) {
            ++offset_;
        }
    }

    void expect(char expected) {
        if (eof() || advance() != expected) {
            fail(std::string("Expected '") + expected + "'");
        }
    }

    bool consumeIf(char expected) {
        if (!eof() && peek() == expected) {
            ++offset_;
            return true;
        }
        return false;
    }

    char advance() {
        return text_[offset_++];
    }

    char peek() const {
        return text_[offset_];
    }

    bool eof() const {
        return offset_ >= text_.size();
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw JsonError(message + " at byte " + std::to_string(offset_));
    }

    const std::string& text_;
    std::size_t offset_ = 0;
};

std::string escapeString(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (char ch : value) {
        switch (ch) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                std::ostringstream stream;
                stream << "\\u";
                stream.width(4);
                stream.fill('0');
                stream << std::hex << static_cast<int>(static_cast<unsigned char>(ch));
                result += stream.str();
            } else {
                result.push_back(ch);
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

}

JsonError::JsonError(const std::string& message) : std::runtime_error(message) {}

Json::Json() : value_(nullptr) {}
Json::Json(std::nullptr_t) : value_(nullptr) {}
Json::Json(bool value) : value_(value) {}
Json::Json(std::int64_t value) : value_(value) {}
Json::Json(double value) : value_(value) {}
Json::Json(const char* value) : value_(std::string(value)) {}
Json::Json(std::string value) : value_(std::move(value)) {}
Json::Json(Array value) : value_(std::move(value)) {}
Json::Json(Object value) : value_(std::move(value)) {}
Json::Json(Value value) : value_(std::move(value)) {}

Json Json::object(Object value) {
    return Json(std::move(value));
}

Json Json::array(Array value) {
    return Json(std::move(value));
}

Json Json::parse(const std::string& text) {
    return Parser(text).parse();
}

bool Json::isNull() const {
    return std::holds_alternative<std::nullptr_t>(value_);
}

bool Json::isBool() const {
    return std::holds_alternative<bool>(value_);
}

bool Json::isInt() const {
    return std::holds_alternative<std::int64_t>(value_);
}

bool Json::isDouble() const {
    return std::holds_alternative<double>(value_);
}

bool Json::isNumber() const {
    return isInt() || isDouble();
}

bool Json::isString() const {
    return std::holds_alternative<std::string>(value_);
}

bool Json::isArray() const {
    return std::holds_alternative<Array>(value_);
}

bool Json::isObject() const {
    return std::holds_alternative<Object>(value_);
}

bool Json::asBool() const {
    return std::get<bool>(value_);
}

std::int64_t Json::asInt() const {
    return std::get<std::int64_t>(value_);
}

double Json::asDouble() const {
    if (isInt()) {
        return static_cast<double>(asInt());
    }
    return std::get<double>(value_);
}

const std::string& Json::asString() const {
    return std::get<std::string>(value_);
}

const Json::Array& Json::asArray() const {
    return std::get<Array>(value_);
}

const Json::Object& Json::asObject() const {
    return std::get<Object>(value_);
}

Json::Array& Json::asArray() {
    return std::get<Array>(value_);
}

Json::Object& Json::asObject() {
    return std::get<Object>(value_);
}

const Json& Json::at(const std::string& key) const {
    return asObject().at(key);
}

const Json& Json::at(std::size_t index) const {
    return asArray().at(index);
}

const Json& Json::operator[](const std::string& key) const {
    static const Json nullValue;
    const auto& object = asObject();
    auto it = object.find(key);
    if (it == object.end()) {
        return nullValue;
    }
    return it->second;
}

Json& Json::operator[](const std::string& key) {
    if (!isObject()) {
        value_ = Object{};
    }
    return asObject()[key];
}

bool Json::contains(const std::string& key) const {
    if (!isObject()) {
        return false;
    }
    return asObject().contains(key);
}

std::optional<std::string> Json::stringValue(const std::string& key) const {
    if (!contains(key)) {
        return std::nullopt;
    }
    const Json& value = (*this)[key];
    if (!value.isString()) {
        return std::nullopt;
    }
    return value.asString();
}

std::string Json::dump() const {
    if (isNull()) {
        return "null";
    }
    if (isBool()) {
        return asBool() ? "true" : "false";
    }
    if (isInt()) {
        return std::to_string(asInt());
    }
    if (isDouble()) {
        std::ostringstream stream;
        stream << asDouble();
        return stream.str();
    }
    if (isString()) {
        return escapeString(asString());
    }
    if (isArray()) {
        std::string result = "[";
        bool first = true;
        for (const Json& item : asArray()) {
            if (!first) {
                result += ",";
            }
            first = false;
            result += item.dump();
        }
        result += "]";
        return result;
    }

    std::string result = "{";
    bool first = true;
    for (const auto& [key, value] : asObject()) {
        if (!first) {
            result += ",";
        }
        first = false;
        result += escapeString(key);
        result += ":";
        result += value.dump();
    }
    result += "}";
    return result;
}

}

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace vanta {

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json();
    Json(std::nullptr_t);
    Json(bool value);
    Json(std::int64_t value);
    Json(double value);
    Json(const char* value);
    Json(std::string value);
    Json(Array value);
    Json(Object value);

    static Json object(Object value = {});
    static Json array(Array value = {});
    static Json parse(const std::string& text);

    bool isNull() const;
    bool isBool() const;
    bool isInt() const;
    bool isDouble() const;
    bool isNumber() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    bool asBool() const;
    std::int64_t asInt() const;
    double asDouble() const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;
    Array& asArray();
    Object& asObject();

    const Json& at(const std::string& key) const;
    const Json& at(std::size_t index) const;
    const Json& operator[](const std::string& key) const;
    Json& operator[](const std::string& key);
    bool contains(const std::string& key) const;
    std::optional<std::string> stringValue(const std::string& key) const;

    std::string dump() const;

private:
    using Value = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object>;

    explicit Json(Value value);

    Value value_;
};

class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string& message);
};

}

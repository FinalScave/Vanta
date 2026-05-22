#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/value.h"
#include "vanta/platform/process.h"
#include "vanta/core/text.h"
#include "vanta/vfs/uri.h"

namespace vanta {

struct LspRequestResult {
    int id = 0;
    std::string method;
    std::string raw_request;
    std::string raw_response;
    bool timed_out = false;
};

class LspClient {
public:
    bool Start(const std::filesystem::path& server_path, const std::filesystem::path& workspace_root, std::string* error_message = nullptr);
    bool Running() const;
    void Stop();

    LspRequestResult Initialize(const std::filesystem::path& workspace_root);
    LspRequestResult Completion(const Uri& file, TextPosition position);
    LspRequestResult Hover(const Uri& file, TextPosition position);
    LspRequestResult Definition(const Uri& file, TextPosition position);
    LspRequestResult SemanticTokensFull(const Uri& file);
    void Notify(std::string method, Value params);
    std::vector<Value> DrainNotifications();

private:
    LspRequestResult SendRequest(std::string method, Value params);
    std::optional<std::string> ReadMessageBody();
    std::optional<std::string> WaitForResponseBody(int request_id);
    static Value TextDocumentPositionParams(const Uri& file, TextPosition position);
    static std::string FileUri(const std::filesystem::path& file);

    ChildProcess process_;
    std::mutex mutex_;
    std::string read_buffer_;
    std::vector<Value> notifications_;
    int next_request_id_ = 1;
};

}

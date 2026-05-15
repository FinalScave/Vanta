#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "vanta/platform/async.h"
#include "vanta/platform/json.h"
#include "vanta/platform/process.h"
#include "vanta/core/text.h"
#include "vanta/vfs/uri.h"

namespace vanta {

struct LspRequestResult {
    int id = 0;
    std::string method;
    std::string rawRequest;
    std::string rawResponse;
    bool timedOut = false;
};

using LspResultCallback = std::function<void(LspRequestResult)>;

class LspClient {
public:
    bool start(const std::filesystem::path& serverPath, const std::filesystem::path& workspaceRoot, std::string* errorMessage = nullptr);
    bool running() const;
    void stop();

    LspRequestResult initialize(const std::filesystem::path& workspaceRoot);
    LspRequestResult completion(const Uri& file, TextPosition position);
    LspRequestResult hover(const Uri& file, TextPosition position);
    LspRequestResult definition(const Uri& file, TextPosition position);
    LspRequestResult semanticTokensFull(const Uri& file);
    void completionAsync(AsyncRuntime& runtime, Uri file, TextPosition position, LspResultCallback callback);
    void hoverAsync(AsyncRuntime& runtime, Uri file, TextPosition position, LspResultCallback callback);
    void notify(std::string method, Json params);
    std::vector<Json> drainNotifications();

private:
    LspRequestResult sendRequest(std::string method, Json params);
    std::optional<std::string> readMessageBody();
    std::optional<std::string> waitForResponseBody(int requestId);
    static Json textDocumentPositionParams(const Uri& file, TextPosition position);
    static std::string fileUri(const std::filesystem::path& file);

    ChildProcess process_;
    std::mutex mutex_;
    std::string readBuffer_;
    std::vector<Json> notifications_;
    int nextRequestId_ = 1;
};

}

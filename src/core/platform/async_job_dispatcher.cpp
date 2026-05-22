#include "vanta/platform/async_job_dispatcher.h"

#include <utility>

#include "vanta/platform/async.h"

namespace vanta {

JobDispatcher AsyncJobDispatcher(AsyncRuntime& runtime) {
    return {
        .worker = [&runtime](JobTask task) {
            runtime.PostWorker(std::move(task));
        },
        .main = [&runtime](JobTask task) {
            runtime.PostMain(std::move(task));
        },
    };
}

}

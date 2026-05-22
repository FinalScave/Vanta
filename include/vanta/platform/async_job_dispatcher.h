#pragma once

#include "vanta/execution/job_service.h"

namespace vanta {

class AsyncRuntime;

JobDispatcher AsyncJobDispatcher(AsyncRuntime& runtime);

}

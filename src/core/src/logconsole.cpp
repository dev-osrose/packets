// Copyright 2016 Chirstopher Torres (Raven), L3nn0x
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http ://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "logconsole.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#ifdef _WIN32
#include <spdlog/sinks/msvc_sink.h>
#endif
#include <vector>
#include <mutex>

namespace Core {

namespace {

// Logger names for each log_type
static const char* kLoggerNames[] = {
    "general",
    "network",
    "db",
    "file",
};

static std::mutex              g_initMutex;
static bool                    g_initialised = false;
static std::shared_ptr<spdlog::logger> g_loggers[static_cast<int>(log_type::MAX_LOG_TYPE)];

void initialise() {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initialised) return;

    // Build shared sinks
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#ifdef _WIN32
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

    const int count = static_cast<int>(log_type::MAX_LOG_TYPE);
    for (int i = 0; i < count; ++i) {
        auto logger = std::make_shared<spdlog::logger>(kLoggerNames[i], sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);
        spdlog::register_logger(logger);
        g_loggers[i] = logger;
    }

    g_initialised = true;
}

} // anonymous namespace

/*static*/
std::weak_ptr<spdlog::logger> CLog::GetLogger(log_type type) {
    initialise();
    const int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(log_type::MAX_LOG_TYPE))
        return {};
    return g_loggers[idx];
}

/*static*/
void CLog::SetLevel(spdlog::level::level_enum level) {
    initialise();
    for (auto& lg : g_loggers) {
        if (lg) lg->set_level(level);
    }
}

} // namespace Core

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

#ifndef LOGCONSOLE_H_
#define LOGCONSOLE_H_

#include <memory>
#include <spdlog/spdlog.h>

namespace Core {

enum class log_type {
    GENERAL = 0,
    NETWORK,
    DB,
    FILE,
    MAX_LOG_TYPE
};

/**
 * \class CLog
 * \brief Thin wrapper around spdlog that provides named loggers per log_type.
 *
 * Usage:
 *   auto logger = CLog::GetLogger(Core::log_type::NETWORK).lock();
 *   if (logger) logger->info("Connected.");
 */
class CLog {
public:
    static std::weak_ptr<spdlog::logger> GetLogger(log_type type);
    static void SetLevel(spdlog::level::level_enum level);

private:
    static void EnsureInitialised();
};

} // namespace Core

#endif // LOGCONSOLE_H_

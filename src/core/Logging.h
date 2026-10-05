#pragma once

#include <QString>

namespace velox::Logging {

// Installs a Qt message handler that writes timestamped lines to a rotating
// log file under the application's log directory and to stderr. Safe to call
// more than once; later calls are ignored.
void install();

// Flushes and detaches the handler. Called during application shutdown.
void shutdown();

// Absolute path of the active log file (empty before install()).
QString logFilePath();

} // namespace velox::Logging

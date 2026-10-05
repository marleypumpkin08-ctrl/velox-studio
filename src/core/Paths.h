#pragma once

#include <QString>

namespace velox::Paths {

// Per-user locations. Every function creates the directory on demand and
// returns an absolute path with no trailing slash.
QString dataDirectory();
QString logDirectory();
QString configDirectory();
QString updatesDirectory();
QString recoveryDirectory();

// Directory that contains the running executable.
QString applicationDirectory();

} // namespace velox::Paths

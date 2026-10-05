#pragma once

#include <QString>

namespace velox::BuildInfo {

QString applicationName();
QString versionString();
QString revision();
QString buildDate();
QString buildConfig();
QString architecture();
QString qtVersion();
QString compilerDescription();

// Multi-line, human readable summary for the About and Diagnostics dialogs.
QString summary();

} // namespace velox::BuildInfo

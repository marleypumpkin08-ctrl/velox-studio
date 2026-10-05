#pragma once

#include <QString>
#include <QVariant>

namespace velox::Settings {

// Thin wrapper over QSettings (INI file in the per-user config directory) so
// every module reads and writes preferences the same way.
QVariant value(const QString& key, const QVariant& fallback = {});
void setValue(const QString& key, const QVariant& v);
void remove(const QString& key);
void sync();

// Well-known keys.
inline const QString kUpdateChannel = QStringLiteral("update/channel");
inline const QString kLastUpdateCheck = QStringLiteral("update/lastCheck");
inline const QString kSkippedVersion = QStringLiteral("update/skippedVersion");
inline const QString kWindowGeometry = QStringLiteral("window/geometry");
inline const QString kWindowState = QStringLiteral("window/state");

} // namespace velox::Settings

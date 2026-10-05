#pragma once

#include "Version.h"

#include <QByteArray>
#include <QUrl>

#include <optional>

namespace velox {

struct UpdateManifest {
    Version version;
    QString releaseNotes;
    QUrl releaseUrl;
    QUrl downloadUrl;
    QString sha256;
};

namespace Update {

std::optional<UpdateManifest> parseLatestRelease(const QByteArray& payload,
                                                 QString* error = nullptr);

} // namespace Update
} // namespace velox

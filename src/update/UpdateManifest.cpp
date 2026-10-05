#include "UpdateManifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace velox::Update {
namespace {

void setError(QString* error, const QString& message)
{
    if (error != nullptr) {
        *error = message;
    }
}

bool isGitHubUrl(const QUrl& url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https")
        && url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0;
}

} // namespace

std::optional<UpdateManifest> parseLatestRelease(const QByteArray& payload, QString* error)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("GitHub returned an invalid release response."));
        return std::nullopt;
    }

    const QJsonObject release = document.object();
    if (release.value(QStringLiteral("draft")).toBool()) {
        setError(error, QStringLiteral("GitHub returned a draft release."));
        return std::nullopt;
    }

    const auto version = Version::parse(release.value(QStringLiteral("tag_name")).toString());
    if (!version) {
        setError(error, QStringLiteral("The latest release has an invalid semantic version tag."));
        return std::nullopt;
    }

    const QUrl releaseUrl(release.value(QStringLiteral("html_url")).toString());
    if (!isGitHubUrl(releaseUrl)) {
        setError(error, QStringLiteral("The release URL is not a trusted GitHub URL."));
        return std::nullopt;
    }

    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& value : assets) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject asset = value.toObject();
        if (asset.value(QStringLiteral("name")).toString()
                != QStringLiteral("VeloxStudio-windows-x64.zip")) {
            continue;
        }

        const QUrl downloadUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        if (!isGitHubUrl(downloadUrl)) {
            setError(error, QStringLiteral("The Windows update package URL is invalid."));
            return std::nullopt;
        }

        QString digest = asset.value(QStringLiteral("digest")).toString();
        if (digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
            digest.remove(0, 7);
        }
        static const QRegularExpression digestPattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
        if (!digestPattern.match(digest).hasMatch()) {
            setError(error, QStringLiteral(
                "The Windows update package does not have a valid SHA-256 digest."));
            return std::nullopt;
        }

        UpdateManifest manifest;
        manifest.version = *version;
        manifest.releaseNotes = release.value(QStringLiteral("body")).toString();
        manifest.releaseUrl = releaseUrl;
        manifest.downloadUrl = downloadUrl;
        manifest.sha256 = digest.toLower();
        return manifest;
    }

    setError(error, QStringLiteral(
        "The release is missing VeloxStudio-windows-x64.zip with its SHA-256 digest."));
    return std::nullopt;
}

} // namespace velox::Update

#include "UpdateService.h"

#include "Paths.h"
#include "UpdateManifest.h"
#include "Version.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUuid>

namespace velox {
namespace {

QUrl latestReleaseUrl()
{
#ifndef VELOX_GITHUB_REPOSITORY
#define VELOX_GITHUB_REPOSITORY "marleypumpkin08-ctrl/velox-studio"
#endif
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                    .arg(QString::fromLatin1(VELOX_GITHUB_REPOSITORY)));
}

} // namespace

UpdateService::UpdateService(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_downloadHash(QCryptographicHash::Sha256)
{
}

void UpdateService::checkForUpdates()
{
    if (m_checkReply || m_downloadReply) {
        return;
    }

    QNetworkRequest request(latestReleaseUrl());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VeloxStudio"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_checkReply = m_network->get(request);

    connect(m_checkReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_checkReply;
        m_checkReply = nullptr;
        const QByteArray payload = reply->readAll();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString networkMessage = reply->errorString();
        reply->deleteLater();

        if (networkError != QNetworkReply::NoError) {
            Q_EMIT checkFailed(networkMessage);
            return;
        }

        QString parseMessage;
        const auto release = Update::parseLatestRelease(payload, &parseMessage);
        if (!release) {
            Q_EMIT checkFailed(parseMessage);
            return;
        }
        if (release->version <= Version::current()) {
            Q_EMIT upToDate();
            return;
        }

        Q_EMIT updateAvailable(release->version.toString(), release->releaseNotes,
                               release->downloadUrl, release->sha256, release->releaseUrl);
    });
}

void UpdateService::downloadAndInstall(const QUrl& url, const QString& sha256,
                                      const QString& version)
{
    if (m_downloadReply || !url.isValid() || url.scheme() != QStringLiteral("https")
        || url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) != 0
        || sha256.size() != 64) {
        Q_EMIT downloadFailed(tr("The update package metadata is invalid."));
        return;
    }

    m_releaseVersion = version;
    m_expectedDigest = sha256.toLower();
    m_writeError.clear();
    m_downloadHash.reset();
    const QString archivePath = Paths::updatesDirectory()
        + QStringLiteral("/VeloxStudio-%1-%2.zip")
              .arg(version, QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_downloadFile.setFileName(archivePath);
    if (!m_downloadFile.open(QIODevice::WriteOnly)) {
        Q_EMIT downloadFailed(tr("Could not create the update package file: %1")
                                  .arg(m_downloadFile.errorString()));
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VeloxStudio"));
    request.setTransferTimeout(120000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_downloadReply = m_network->get(request);
    connect(m_downloadReply, &QNetworkReply::readyRead,
            this, &UpdateService::writeDownloadChunk);
    connect(m_downloadReply, &QNetworkReply::downloadProgress,
            this, &UpdateService::downloadProgress);
    connect(m_downloadReply, &QNetworkReply::finished,
            this, &UpdateService::finishDownload);
}

void UpdateService::writeDownloadChunk()
{
    if (!m_downloadReply || !m_downloadFile.isOpen() || !m_writeError.isEmpty()) {
        return;
    }
    const QByteArray chunk = m_downloadReply->readAll();
    if (m_downloadFile.write(chunk) != chunk.size()) {
        m_writeError = tr("Could not write the update package: %1")
                           .arg(m_downloadFile.errorString());
        m_downloadReply->abort();
        return;
    }
    m_downloadHash.addData(chunk);
}

void UpdateService::finishDownload()
{
    QNetworkReply* reply = m_downloadReply;
    m_downloadReply = nullptr;
    const QByteArray tail = reply->readAll();
    if (m_writeError.isEmpty() && !tail.isEmpty()) {
        if (m_downloadFile.write(tail) != tail.size()) {
            m_writeError = tr("Could not finish writing the update package: %1")
                               .arg(m_downloadFile.errorString());
        } else {
            m_downloadHash.addData(tail);
        }
    }

    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkMessage = reply->errorString();
    reply->deleteLater();

    if (!m_writeError.isEmpty() || networkError != QNetworkReply::NoError) {
        const QString error = m_writeError.isEmpty() ? networkMessage : m_writeError;
        m_downloadFile.cancelWriting();
        Q_EMIT downloadFailed(error);
        return;
    }
    if (m_downloadHash.result().toHex() != m_expectedDigest.toLatin1()) {
        m_downloadFile.cancelWriting();
        Q_EMIT downloadFailed(tr("The update package SHA-256 digest did not match."));
        return;
    }
    if (!m_downloadFile.commit()) {
        Q_EMIT downloadFailed(tr("Could not finalize the downloaded package: %1")
                                  .arg(m_downloadFile.errorString()));
        return;
    }

    const QString appDirectory = QCoreApplication::applicationDirPath();
    const QString updaterPath = appDirectory + QStringLiteral("/VeloxUpdater.exe");
    if (!QFileInfo::exists(updaterPath)) {
        Q_EMIT downloadFailed(tr("The standalone updater is missing: %1").arg(updaterPath));
        return;
    }

    const QString appPath = QCoreApplication::applicationFilePath();
    const QStringList arguments{
        QStringLiteral("--wait-pid"),
        QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("--archive"),
        m_downloadFile.fileName(),
        QStringLiteral("--target"),
        appDirectory,
        QStringLiteral("--restart"),
        appPath
    };
    if (!QProcess::startDetached(updaterPath, arguments, appDirectory)) {
        Q_EMIT downloadFailed(tr("Could not start the standalone updater."));
        return;
    }
    Q_EMIT updaterStarted();
}

} // namespace velox

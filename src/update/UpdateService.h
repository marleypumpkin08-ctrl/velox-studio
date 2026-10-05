#pragma once

#include <QCryptographicHash>
#include <QObject>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace velox {

class UpdateService final : public QObject
{
    Q_OBJECT

public:
    explicit UpdateService(QObject* parent = nullptr);

public Q_SLOTS:
    void checkForUpdates();
    void downloadAndInstall(const QUrl& url, const QString& sha256,
                            const QString& version);

Q_SIGNALS:
    void updateAvailable(QString version, QString notes, QUrl downloadUrl,
                         QString digest, QUrl releaseUrl);
    void upToDate();
    void checkFailed(QString error);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFailed(QString error);
    void updaterStarted();

private:
    void writeDownloadChunk();
    void finishDownload();

    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_checkReply;
    QPointer<QNetworkReply> m_downloadReply;
    QSaveFile m_downloadFile;
    QCryptographicHash m_downloadHash;
    QString m_expectedDigest;
    QString m_releaseVersion;
    QString m_writeError;
};

} // namespace velox

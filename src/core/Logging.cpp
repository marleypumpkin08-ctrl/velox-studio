#include "Logging.h"

#include "Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>

namespace velox::Logging {
namespace {

constexpr qint64 kMaxLogBytes = 2 * 1024 * 1024;
constexpr int kKeptLogs = 5;

QMutex g_mutex;
QFile g_file;
QString g_path;
QtMessageHandler g_previous = nullptr;
bool g_installed = false;

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO ";
    case QtWarningMsg: return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg: return "FATAL";
    }
    return "?????";
}

void rotateLocked()
{
    g_file.close();
    const QString dir = QFileInfo(g_path).absolutePath();
    const QString base = QFileInfo(g_path).completeBaseName();
    const QString ext = QFileInfo(g_path).suffix();
    for (int i = kKeptLogs - 1; i >= 1; --i) {
        const QString from = QStringLiteral("%1/%2.%3.%4").arg(dir, base).arg(i).arg(ext);
        const QString to = QStringLiteral("%1/%2.%3.%4").arg(dir, base).arg(i + 1).arg(ext);
        QFile::remove(to);
        QFile::rename(from, to);
    }
    const QString first = QStringLiteral("%1/%2.1.%3").arg(dir, base, ext);
    QFile::remove(first);
    QFile::rename(g_path, first);
    g_file.setFileName(g_path);
    g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString line = QStringLiteral("%1 [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QString::fromLatin1(levelName(type)), message);
    {
        QMutexLocker lock(&g_mutex);
        if (g_file.isOpen()) {
            if (g_file.size() > kMaxLogBytes) {
                rotateLocked();
            }
            g_file.write(line.toUtf8());
            g_file.flush();
        }
    }
    std::fputs(line.toLocal8Bit().constData(), stderr);
    (void)context;
    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

void install()
{
    QMutexLocker lock(&g_mutex);
    if (g_installed) {
        return;
    }
    g_path = Paths::logDirectory() + QStringLiteral("/velox-studio.log");
    g_file.setFileName(g_path);
    g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    g_previous = qInstallMessageHandler(handler);
    g_installed = true;
}

void shutdown()
{
    QMutexLocker lock(&g_mutex);
    if (!g_installed) {
        return;
    }
    qInstallMessageHandler(g_previous);
    g_previous = nullptr;
    g_file.close();
    g_installed = false;
}

QString logFilePath()
{
    QMutexLocker lock(&g_mutex);
    return g_path;
}

} // namespace velox::Logging

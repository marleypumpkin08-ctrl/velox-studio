#include "Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace velox::Paths {
namespace {

QString ensure(const QString& path)
{
    QDir().mkpath(path);
    return QDir::cleanPath(path);
}

QString baseDataDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.velox-studio");
    }
    return base;
}

} // namespace

QString dataDirectory()
{
    return ensure(baseDataDir());
}

QString logDirectory()
{
    return ensure(baseDataDir() + QStringLiteral("/logs"));
}

QString configDirectory()
{
    return ensure(baseDataDir() + QStringLiteral("/config"));
}

QString updatesDirectory()
{
    return ensure(baseDataDir() + QStringLiteral("/updates"));
}

QString recoveryDirectory()
{
    return ensure(baseDataDir() + QStringLiteral("/recovery"));
}

QString applicationDirectory()
{
    return QDir::cleanPath(QCoreApplication::applicationDirPath());
}

} // namespace velox::Paths

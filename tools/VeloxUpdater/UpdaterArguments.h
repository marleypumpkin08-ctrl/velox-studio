#pragma once

#include <QString>
#include <QStringList>

namespace velox::updater {

struct Arguments {
    qint64 waitPid = 0;
    QString archivePath;
    QString targetDirectory;
    QString restartExecutable;
};

bool parseArguments(const QStringList& arguments, Arguments* parsed, QString* error);

} // namespace velox::updater

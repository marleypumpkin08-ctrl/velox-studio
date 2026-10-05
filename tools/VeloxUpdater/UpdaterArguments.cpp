#include "UpdaterArguments.h"

#include <QRegularExpression>

namespace velox::updater {

bool parseArguments(const QStringList& arguments, Arguments* parsed, QString* error)
{
    if (parsed == nullptr || error == nullptr) {
        return false;
    }

    Arguments result;
    bool hasPid = false;
    bool hasArchive = false;
    bool hasTarget = false;
    bool hasRestart = false;

    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString option = arguments.at(index);
        if (index + 1 >= arguments.size()) {
            *error = QStringLiteral("Missing value for %1").arg(option);
            return false;
        }
        const QString value = arguments.at(++index);
        if (option == QStringLiteral("--wait-pid") && !hasPid) {
            static const QRegularExpression pidPattern(QStringLiteral("^[1-9][0-9]*$"));
            bool ok = false;
            const qint64 pid = value.toLongLong(&ok);
            if (!ok || !pidPattern.match(value).hasMatch()) {
                *error = QStringLiteral("--wait-pid must be a positive process ID.");
                return false;
            }
            result.waitPid = pid;
            hasPid = true;
        } else if (option == QStringLiteral("--archive") && !hasArchive) {
            result.archivePath = value;
            hasArchive = true;
        } else if (option == QStringLiteral("--target") && !hasTarget) {
            result.targetDirectory = value;
            hasTarget = true;
        } else if (option == QStringLiteral("--restart") && !hasRestart) {
            result.restartExecutable = value;
            hasRestart = true;
        } else {
            *error = QStringLiteral("Unknown or duplicate option: %1").arg(option);
            return false;
        }
    }

    if (!hasPid || !hasArchive || !hasTarget || !hasRestart) {
        *error = QStringLiteral(
            "Usage: VeloxUpdater --wait-pid PID --archive FILE --target DIR --restart EXE");
        return false;
    }

    *parsed = std::move(result);
    return true;
}

} // namespace velox::updater

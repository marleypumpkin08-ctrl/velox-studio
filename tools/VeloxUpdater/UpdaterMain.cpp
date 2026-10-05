#include "UpdaterArguments.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

bool waitForParent(qint64 processId, QString* error)
{
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(processId));
    if (process == nullptr) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) {
            return true;
        }
        *error = QStringLiteral("Could not open the Velox Studio process to wait for shutdown.");
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(process, 120000);
    CloseHandle(process);
    if (waitResult != WAIT_OBJECT_0) {
        *error = waitResult == WAIT_TIMEOUT
            ? QStringLiteral("Timed out waiting for Velox Studio to close.")
            : QStringLiteral("Failed while waiting for Velox Studio to close.");
        return false;
    }
    return true;
#else
    Q_UNUSED(processId);
    *error = QStringLiteral("The updater is supported only on Windows.");
    return false;
#endif
}

bool isInside(const QString& directory, const QString& path)
{
    const QString relative = QDir(directory).relativeFilePath(path);
    return relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !relative.startsWith(QStringLiteral("..\\"))
        && !QDir::isAbsolutePath(relative);
}

bool extractArchive(const QString& archive, const QString& destination, QString* error)
{
    QProcess tar;
    tar.start(QStringLiteral("tar.exe"),
              {QStringLiteral("-xf"), archive, QStringLiteral("-C"), destination});
    if (!tar.waitForStarted()) {
        *error = QStringLiteral("Could not start Windows tar.exe: %1").arg(tar.errorString());
        return false;
    }
    if (!tar.waitForFinished(180000)) {
        tar.kill();
        tar.waitForFinished();
        *error = QStringLiteral("Timed out extracting the update package.");
        return false;
    }
    if (tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        *error = QStringLiteral("The update archive could not be extracted: %1")
                     .arg(QString::fromLocal8Bit(tar.readAllStandardError()).trimmed());
        return false;
    }
    return true;
}

QString packageRoot(const QString& extractedDirectory)
{
    const QString executable = QStringLiteral("VeloxStudio.exe");
    if (QFileInfo(QDir(extractedDirectory).filePath(executable)).isFile()) {
        return extractedDirectory;
    }

    const QStringList children = QDir(extractedDirectory)
        .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (children.size() == 1) {
        const QString nested = QDir(extractedDirectory).filePath(children.constFirst());
        if (QFileInfo(QDir(nested).filePath(executable)).isFile()) {
            return nested;
        }
    }
    return {};
}

bool copyPackage(const QString& sourceDirectory, const QString& destinationDirectory,
                 QString* error)
{
    const QDir source(sourceDirectory);
    QDirIterator iterator(sourceDirectory,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourcePath = iterator.next();
        const QFileInfo sourceInfo = iterator.fileInfo();
        const QString relativePath = source.relativeFilePath(sourcePath);
        if (sourceInfo.isSymLink() || relativePath.startsWith(QStringLiteral(".."))
            || QDir::isAbsolutePath(relativePath)
            || !isInside(sourceDirectory, sourceInfo.canonicalFilePath())) {
            *error = QStringLiteral("The update archive contains an unsafe path.");
            return false;
        }

        const QString destinationPath = QDir(destinationDirectory).filePath(relativePath);
        if (sourceInfo.isDir()) {
            if (!QDir().mkpath(destinationPath)) {
                *error = QStringLiteral("Could not create update directory: %1")
                             .arg(destinationPath);
                return false;
            }
            continue;
        }
        if (!sourceInfo.isFile() || !QDir().mkpath(QFileInfo(destinationPath).absolutePath())
            || !QFile::copy(sourcePath, destinationPath)) {
            *error = QStringLiteral("Could not stage update file: %1").arg(relativePath);
            return false;
        }
    }
    return true;
}

bool installUpdate(const velox::updater::Arguments& arguments, QString* error)
{
    const QFileInfo archiveInfo(arguments.archivePath);
    const QFileInfo targetInfo(arguments.targetDirectory);
    const QFileInfo restartInfo(arguments.restartExecutable);
    if (!archiveInfo.isFile() || targetInfo.canonicalFilePath().isEmpty()
        || !restartInfo.isFile()
        || restartInfo.fileName().compare(QStringLiteral("VeloxStudio.exe"),
                                          Qt::CaseInsensitive) != 0
        || QDir::cleanPath(restartInfo.canonicalPath())
            .compare(QDir::cleanPath(targetInfo.canonicalFilePath()),
                     Qt::CaseInsensitive) != 0) {
        *error = QStringLiteral("The update package or installation paths are invalid.");
        return false;
    }

    const QString target = targetInfo.canonicalFilePath();
    const QString parent = QFileInfo(target).absolutePath();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString workName = QStringLiteral(".velox-update-%1").arg(id);
    const QString candidateName = QStringLiteral(".velox-install-%1").arg(id);
    const QString backupName = QStringLiteral(".velox-backup-%1").arg(id);
    const QString workPath = QDir(parent).filePath(workName);
    const QString candidatePath = QDir(parent).filePath(candidateName);
    const QString backupPath = QDir(parent).filePath(backupName);
    struct Cleanup {
        QString work;
        QString candidate;
        ~Cleanup()
        {
            QDir(work).removeRecursively();
            QDir(candidate).removeRecursively();
        }
    } cleanup{workPath, candidatePath};

    if (!QDir().mkpath(workPath)
        || !extractArchive(archiveInfo.canonicalFilePath(), workPath, error)) {
        return false;
    }

    const QString source = packageRoot(workPath);
    if (source.isEmpty() || !QDir().mkpath(candidatePath)
        || !copyPackage(source, candidatePath, error)) {
        if (error->isEmpty()) {
            *error = QStringLiteral("The update archive does not contain VeloxStudio.exe.");
        }
        return false;
    }

    const QString targetName = QFileInfo(target).fileName();
    QDir parentDirectory(parent);
    if (!parentDirectory.rename(targetName, backupName)) {
        *error = QStringLiteral("Could not safely move the existing installation aside.");
        return false;
    }
    if (!parentDirectory.rename(candidateName, targetName)) {
        if (!parentDirectory.rename(backupName, targetName)) {
            *error = QStringLiteral(
                "Could not install the update or restore the previous installation.");
        } else {
            *error = QStringLiteral("Could not install the update; the previous version was restored.");
        }
        return false;
    }

    if (!QDir(backupPath).removeRecursively()) {
        qWarning("VeloxUpdater: the previous-version backup could not be removed.");
    }
    return true;
}

} // namespace

namespace {

void reportError(const QString& error)
{
    const QByteArray message = error.toLocal8Bit();
    std::fwrite(message.constData(), 1, static_cast<size_t>(message.size()), stderr);
    std::fputc('\n', stderr);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VeloxUpdater"));

    velox::updater::Arguments arguments;
    QString error;
    if (!velox::updater::parseArguments(app.arguments(), &arguments, &error)) {
        reportError(error);
        return 2;
    }
    if (arguments.waitPid == QCoreApplication::applicationPid()) {
        reportError(QStringLiteral("VeloxUpdater cannot wait for itself."));
        return 2;
    }
    if (!waitForParent(arguments.waitPid, &error)) {
        reportError(error);
        return 3;
    }
    if (!installUpdate(arguments, &error)) {
        reportError(error);
        return 4;
    }

    const QString targetDirectory = QFileInfo(arguments.targetDirectory).canonicalFilePath();
    const QString executable = QDir(targetDirectory).filePath(QStringLiteral("VeloxStudio.exe"));
    if (!QProcess::startDetached(executable, {}, targetDirectory)) {
        reportError(QStringLiteral("The update was installed, but Velox Studio could not be restarted."));
        return 5;
    }
    return 0;
}

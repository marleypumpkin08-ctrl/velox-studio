#include "BuildInfo.h"

#include "velox/Version.generated.h"

#include <QSysInfo>
#include <QtGlobal>

namespace velox::BuildInfo {

QString applicationName()
{
    return QStringLiteral("Velox Studio");
}

QString versionString()
{
    return QStringLiteral(VELOX_VERSION_STRING);
}

QString revision()
{
    return QStringLiteral(VELOX_BUILD_REVISION);
}

QString buildDate()
{
    return QStringLiteral(VELOX_BUILD_DATE);
}

QString buildConfig()
{
    return QStringLiteral(VELOX_BUILD_CONFIG);
}

QString architecture()
{
    return QSysInfo::buildCpuArchitecture();
}

QString qtVersion()
{
    return QString::fromLatin1(qVersion());
}

QString compilerDescription()
{
#if defined(_MSC_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_VER);
#elif defined(clang)
    return QStringLiteral("Clang %1.%2").arg(clang_major).arg(clang_minor);
#elif defined(GNUC)
    return QStringLiteral("GCC %1.%2").arg(GNUC).arg(GNUC_MINOR);
#else
    return QStringLiteral("unknown compiler");
#endif
}

QString summary()
{
    return QStringLiteral(
               "%1 %2\n"
               "Revision: %3\n"
               "Built: %4 (%5, %6)\n"
               "Qt: %7\n"
               "Compiler: %8")
        .arg(applicationName(), versionString(), revision(), buildDate(), buildConfig(),
             architecture(), qtVersion())
        .arg(compilerDescription());
}

} // namespace velox::BuildInfo

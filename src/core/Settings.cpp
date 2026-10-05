#include "Settings.h"

#include "Paths.h"

#include <QSettings>

namespace velox::Settings {
namespace {

QSettings& store()
{
    static QSettings instance(Paths::configDirectory() + QStringLiteral("/velox-studio.ini"),
                              QSettings::IniFormat);
    return instance;
}

} // namespace

QVariant value(const QString& key, const QVariant& fallback)
{
    return store().value(key, fallback);
}

void setValue(const QString& key, const QVariant& v)
{
    store().setValue(key, v);
}

void remove(const QString& key)
{
    store().remove(key);
}

void sync()
{
    store().sync();
}

} // namespace velox::Settings

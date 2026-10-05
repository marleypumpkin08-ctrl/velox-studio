#include "Sha256.h"

#include <QCryptographicHash>
#include <QFile>

namespace velox::Sha256 {

QString hex(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString fileHex(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool equals(const QString& a, const QString& b)
{
    return !a.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0;
}

} // namespace velox::Sha256

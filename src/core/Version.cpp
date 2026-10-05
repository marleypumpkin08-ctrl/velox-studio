#include "Version.h"

#include "velox/Version.generated.h"

#include <QStringList>

namespace velox {
namespace {

bool isNumeric(const QString& s)
{
    if (s.isEmpty()) {
        return false;
    }
    for (const QChar c : s) {
        if (!c.isDigit()) {
            return false;
        }
    }
    return true;
}

std::optional<int> parseNumber(const QString& s)
{
    if (!isNumeric(s)) {
        return std::nullopt;
    }
    bool ok = false;
    const int value = s.toInt(&ok, 10);
    if (!ok || value < 0) {
        return std::nullopt;
    }
    return value;
}

int compareIdentifier(const QString& a, const QString& b)
{
    const bool aNumeric = isNumeric(a);
    const bool bNumeric = isNumeric(b);
    if (aNumeric && bNumeric) {
        const qulonglong x = a.toULongLong();
        const qulonglong y = b.toULongLong();
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (aNumeric) {
        return -1; // numeric identifiers sort before alphanumeric ones
    }
    if (bNumeric) {
        return 1;
    }
    const int c = a.compare(b);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

int comparePrerelease(const QString& a, const QString& b)
{
    if (a.isEmpty() && b.isEmpty()) {
        return 0;
    }
    if (a.isEmpty()) {
        return 1; // a release outranks any of its prereleases
    }
    if (b.isEmpty()) {
        return -1;
    }
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    const qsizetype shared = qMin(pa.size(), pb.size());
    for (qsizetype i = 0; i < shared; ++i) {
        const int c = compareIdentifier(pa.at(i), pb.at(i));
        if (c != 0) {
            return c;
        }
    }
    if (pa.size() == pb.size()) {
        return 0;
    }
    return pa.size() < pb.size() ? -1 : 1;
}

} // namespace

std::optional<Version> Version::parse(const QString& text)
{
    QString t = text.trimmed();
    if (t.startsWith(QLatin1Char('v')) || t.startsWith(QLatin1Char('V'))) {
        t.remove(0, 1);
    }

    // Build metadata (+...) never affects precedence.
    const qsizetype plus = t.indexOf(QLatin1Char('+'));
    if (plus >= 0) {
        t.truncate(plus);
    }

    QString pre;
    const qsizetype dash = t.indexOf(QLatin1Char('-'));
    if (dash >= 0) {
        pre = t.mid(dash + 1);
        t.truncate(dash);
        if (pre.isEmpty()) {
            return std::nullopt;
        }
    }

    const QStringList parts = t.split(QLatin1Char('.'));
    if (parts.size() != 3) {
        return std::nullopt;
    }

    const std::optional<int> maj = parseNumber(parts.at(0));
    const std::optional<int> min = parseNumber(parts.at(1));
    const std::optional<int> pat = parseNumber(parts.at(2));
    if (!maj || !min || !pat) {
        return std::nullopt;
    }

    Version v;
    v.major = *maj;
    v.minor = *min;
    v.patch = *pat;
    v.prerelease = pre;
    return v;
}

Version Version::current()
{
    Version v;
    v.major = VELOX_VERSION_MAJOR;
    v.minor = VELOX_VERSION_MINOR;
    v.patch = VELOX_VERSION_PATCH;
    return v;
}

QString Version::toString() const
{
    QString s = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    if (!prerelease.isEmpty()) {
        s += QLatin1Char('-') + prerelease;
    }
    return s;
}

int Version::compare(const Version& other) const
{
    if (major != other.major) {
        return major < other.major ? -1 : 1;
    }
    if (minor != other.minor) {
        return minor < other.minor ? -1 : 1;
    }
    if (patch != other.patch) {
        return patch < other.patch ? -1 : 1;
    }
    return comparePrerelease(prerelease, other.prerelease);
}

} // namespace velox

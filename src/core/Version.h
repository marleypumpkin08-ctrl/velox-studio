#pragma once

#include <QString>
#include <optional>

namespace velox {

// Semantic version (major.minor.patch[-prerelease]). The updater uses it to
// decide whether a GitHub release is newer than the running build.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    QString prerelease;

    // Parses 1.2.3, v1.2.3 or 1.2.3-beta.1. Returns nullopt on bad input.
    static std::optional<Version> parse(const QString& text);

    // The version of the running application (from the generated header).
    static Version current();

    QString toString() const;

    // Semantic-version precedence: a prerelease sorts before its release.
    int compare(const Version& other) const;

    bool operator==(const Version& other) const { return compare(other) == 0; }
    bool operator<(const Version& other) const { return compare(other) < 0; }
    bool operator>(const Version& other) const { return compare(other) > 0; }
    bool operator<=(const Version& other) const { return compare(other) <= 0; }
    bool operator>=(const Version& other) const { return compare(other) >= 0; }
};

} // namespace velox

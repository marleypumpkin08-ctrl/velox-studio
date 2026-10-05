#pragma once

#include <QByteArray>
#include <QString>

namespace velox::Sha256 {

// Lowercase hex SHA-256 of a buffer.
QString hex(const QByteArray& data);

// Streams a file through SHA-256 so large downloads are never fully loaded
// into memory. Returns an empty string if the file cannot be read.
QString fileHex(const QString& path);

// Case-insensitive comparison of two hex digests.
bool equals(const QString& a, const QString& b);

} // namespace velox::Sha256

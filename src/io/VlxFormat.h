#pragma once

#include "Document.h"

#include <QString>

#include <memory>

namespace velox::VlxFormat {

// Native lossless project container.
//
// Layout (little endian):
//   magic     4 bytes  "VLX1"
//   version   u32      VELOX_VLX_FORMAT_VERSION
//   jsonSize  u32      size of the JSON header in bytes
//   json      bytes    document + layer metadata (UTF-8)
//   blobs     for each layer in order: u32 size + PNG bytes (lossless)
//
// Brush strokes and image layers are stored with the document metadata.

struct SaveResult {
    bool ok = false;
    QString error;
};

SaveResult save(const Document& document, const QString& path);

// Returns nullptr on failure and fills error when provided.
std::unique_ptr<Document> load(const QString& path, QString* error = nullptr);

} // namespace velox::VlxFormat

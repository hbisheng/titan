#pragma once

#include <cstdint>
#include <limits>

namespace rocksdb {
namespace titandb {

// Reserved array entry for an obsolete index whose blob is no longer available.
constexpr uint64_t kUnknownBlobRawSize = std::numeric_limits<uint64_t>::max();

// Only valid during the synchronous TablePropertiesCollector::AddUserKey call
// made by TitanTableBuilder. Returns false for builders without raw-size
// support. A true result may contain kUnknownBlobRawSize; it must not use the
// global compression factor in that case.
bool GetCurrentBlobRawSize(uint64_t* size);

}  // namespace titandb
}  // namespace rocksdb

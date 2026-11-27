#pragma once

#include "rocksdb/cache.h"
#include "titan/blob_raw_size.h"

namespace rocksdb {
namespace titandb {

extern const char kBlobRawSizesProperty[];
// Each sorted entry encodes file number, offset, and raw size as fixed64.
constexpr size_t kBlobRawSizeEntrySize = 24;

class BlobRawSizeScope {
 public:
  explicit BlobRawSizeScope(const uint64_t* size);
  ~BlobRawSizeScope();

 private:
  const uint64_t* previous_;
};

// Per-CF, disposable state. Keys are legacy encoded BlobIndices, whose physical
// addresses are immutable. No coupling to memtable or snapshot lifetimes.
class BlobRawSizeState {
 public:
  BlobRawSizeState() : cache(NewLRUCache(8 << 20)) {}
  void Put(const Slice& index, uint64_t size);
  bool Get(const Slice& index, uint64_t* size);
  // Bounded GC-to-flush cache. Eviction only causes a blob read.
  const std::shared_ptr<Cache> cache;
};

}  // namespace titandb
}  // namespace rocksdb

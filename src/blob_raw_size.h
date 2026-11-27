#pragma once

#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "rocksdb/cache.h"
#include "rocksdb/listener.h"
#include "titan/blob_raw_size.h"

namespace rocksdb {
namespace titandb {

extern const char kBlobRawSizesProperty[];
using BlobRawSizes = std::unordered_map<std::string, uint64_t>;

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
  void Begin(const Options& options, const CompactionJobInfo& info);
  void End(const CompactionJobInfo& info);
  std::vector<std::shared_ptr<const BlobRawSizes>> Inputs();

  // Bounded GC-to-flush cache. Eviction only causes a blob read.
  const std::shared_ptr<Cache> cache;

 private:
  std::mutex mutex_;
  std::map<std::pair<uint64_t, int>, std::shared_ptr<const BlobRawSizes>>
      inputs_;
};

}  // namespace titandb
}  // namespace rocksdb

#include "blob_raw_size.h"

#include "rocksdb/advanced_cache.h"
#include "util.h"

namespace rocksdb {
namespace titandb {

const char kBlobRawSizesProperty[] = "TitanDB.blob_raw_sizes_by_address.v1";
static thread_local const uint64_t* current_raw_size = nullptr;

bool GetCurrentBlobRawSize(uint64_t* size) {
  if (!current_raw_size) return false;
  *size = *current_raw_size;
  return true;
}

BlobRawSizeScope::BlobRawSizeScope(const uint64_t* size)
    : previous_(current_raw_size) {
  current_raw_size = size;
}
BlobRawSizeScope::~BlobRawSizeScope() { current_raw_size = previous_; }

void BlobRawSizeState::Put(const Slice& index, uint64_t size) {
  static const Cache::CacheItemHelper helper(CacheEntryRole::kMisc,
                                             &DeleteCacheValue<uint64_t>);
  cache->Insert(index, new uint64_t(size), &helper, sizeof(uint64_t));
}
bool BlobRawSizeState::Get(const Slice& index, uint64_t* size) {
  auto* handle = cache->Lookup(index);
  if (!handle) return false;
  *size = *static_cast<uint64_t*>(cache->Value(handle));
  cache->Release(handle);
  return true;
}

}  // namespace titandb
}  // namespace rocksdb

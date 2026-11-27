#include "blob_file_size_collector.h"

#include "base_db_listener.h"

namespace rocksdb {
namespace titandb {

TablePropertiesCollector*
BlobFileSizeCollectorFactory::CreateTablePropertiesCollector(
    rocksdb::TablePropertiesCollectorFactory::Context /* context */) {
  return new BlobFileSizeCollector(raw_sizes_);
}

const std::string BlobFileSizeCollector::kPropertiesName =
    "TitanDB.blob_discardable_size";

bool BlobFileSizeCollector::Encode(
    const std::map<uint64_t, uint64_t>& blob_files_size, std::string* result) {
  PutVarint32(result, static_cast<uint32_t>(blob_files_size.size()));
  for (const auto& bfs : blob_files_size) {
    PutVarint64(result, bfs.first);
    PutVarint64(result, bfs.second);
  }
  return true;
}
bool BlobFileSizeCollector::Decode(
    Slice* slice, std::map<uint64_t, uint64_t>* blob_files_size) {
  uint32_t num = 0;
  if (!GetVarint32(slice, &num)) {
    return false;
  }
  uint64_t file_number;
  uint64_t size;
  for (uint32_t i = 0; i < num; ++i) {
    if (!GetVarint64(slice, &file_number)) {
      return false;
    }
    if (!GetVarint64(slice, &size)) {
      return false;
    }
    (*blob_files_size)[file_number] = size;
  }
  return true;
}

Status BlobFileSizeCollector::AddUserKey(const Slice& key, const Slice& value,
                                         EntryType type, SequenceNumber seq,
                                         uint64_t /* file_size */) {
  // Range tombstones are in a separate block, outside the data iterator.
  if (collect_raw_sizes_ && type != kEntryRangeDeletion) ++data_entries_;
  if (type != kEntryBlobIndex) {
    return Status::OK();
  }

  if (collect_raw_sizes_) {
    // Anchor at the first blob after each 128 data entries. Any unanchored
    // blob is then fewer than 128 entries from its preceding anchor, even
    // when long stretches of inline values separate the blobs.
    if (data_entries_ >= next_anchor_at_) {
      InternalKey anchor(key, seq, kTypeTitanBlobIndex);
      PutLengthPrefixedSlice(&raw_size_anchors_, anchor.Encode());
      PutFixed64(&raw_size_anchors_, raw_sizes_.size() / sizeof(uint64_t));
      next_anchor_at_ = data_entries_ + 128;
    }
    uint64_t size = kUnknownBlobRawSize;
    GetCurrentBlobRawSize(&size);
    PutFixed64(&raw_sizes_, size);
  }
  BlobIndex index;
  auto s = index.DecodeFrom(const_cast<Slice*>(&value));
  if (!s.ok()) {
    return s;
  }

  auto iter = blob_files_size_.find(index.file_number);
  if (iter == blob_files_size_.end()) {
    blob_files_size_[index.file_number] = index.blob_handle.size;
  } else {
    iter->second += index.blob_handle.size;
  }

  return Status::OK();
}

Status BlobFileSizeCollector::Finish(UserCollectedProperties* properties) {
  if (blob_files_size_.empty()) return Status::OK();
  if (collect_raw_sizes_) {
    properties->emplace(kBlobRawSizesProperty, std::move(raw_sizes_));
    properties->emplace(kBlobRawSizeAnchorsProperty,
                        std::move(raw_size_anchors_));
  }

  std::string res;
  bool ok __attribute__((__unused__)) = Encode(blob_files_size_, &res);
  assert(ok);
  assert(!res.empty());
  properties->emplace(std::make_pair(kPropertiesName, res));
  return Status::OK();
}

}  // namespace titandb
}  // namespace rocksdb

#include "blob_file_size_collector.h"

#include <algorithm>
#include <tuple>

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

Status BlobFileSizeCollector::AddUserKey(const Slice& /* key */,
                                         const Slice& value, EntryType type,
                                         SequenceNumber /* seq */,
                                         uint64_t /* file_size */) {
  if (type != kEntryBlobIndex) return Status::OK();
  BlobIndex index;
  Slice encoded = value;
  auto s = index.DecodeFrom(&encoded);
  if (!s.ok()) return s;
  if (collect_raw_sizes_) {
    uint64_t raw_size = kUnknownBlobRawSize;
    GetCurrentBlobRawSize(&raw_size);
    raw_sizes_.push_back(
        {index.file_number, index.blob_handle.offset, raw_size});
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
    // Known sizes sort before unknown ones for duplicate addresses.
    std::sort(raw_sizes_.begin(), raw_sizes_.end(),
              [](const auto& a, const auto& b) {
                return std::tie(a.file_number, a.offset, a.raw_size) <
                       std::tie(b.file_number, b.offset, b.raw_size);
              });
    std::string encoded;
    encoded.reserve(raw_sizes_.size() * kBlobRawSizeEntrySize);
    for (const auto& entry : raw_sizes_) {
      PutFixed64(&encoded, entry.file_number);
      PutFixed64(&encoded, entry.offset);
      PutFixed64(&encoded, entry.raw_size);
    }
    properties->emplace(kBlobRawSizesProperty, std::move(encoded));
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

#pragma once

#include "rocksdb/listener.h"
#include "rocksdb/table_properties.h"
#include "util/coding.h"

#include "blob_file_set.h"
#include "db_impl.h"

namespace rocksdb {
namespace titandb {

class BlobFileSizeCollectorFactory final
    : public TablePropertiesCollectorFactory {
 public:
  explicit BlobFileSizeCollectorFactory(bool raw_sizes = false)
      : raw_sizes_(raw_sizes) {}
  TablePropertiesCollector* CreateTablePropertiesCollector(
      TablePropertiesCollectorFactory::Context context) override;

  const char* Name() const override { return "BlobFileSizeCollector"; }

 private:
  bool raw_sizes_;
};

class BlobFileSizeCollector final : public TablePropertiesCollector {
 public:
  explicit BlobFileSizeCollector(bool raw_sizes = false)
      : collect_raw_sizes_(raw_sizes) {}
  const static std::string kPropertiesName;

  static bool Encode(const std::map<uint64_t, uint64_t>& blob_files_size,
                     std::string* result);
  static bool Decode(Slice* slice,
                     std::map<uint64_t, uint64_t>* blob_files_size);

  Status AddUserKey(const Slice& key, const Slice& value, EntryType type,
                    SequenceNumber seq, uint64_t file_size) override;
  Status Finish(UserCollectedProperties* properties) override;
  UserCollectedProperties GetReadableProperties() const override {
    return UserCollectedProperties();
  }
  const char* Name() const override { return "BlobFileSizeCollector"; }

 private:
  bool collect_raw_sizes_;
  std::string raw_sizes_;
  std::map<uint64_t, uint64_t> blob_files_size_;
};

}  // namespace titandb
}  // namespace rocksdb

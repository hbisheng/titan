#include "blob_raw_size.h"

#include "db/dbformat.h"
#include "file/random_access_file_reader.h"
#include "options/cf_options.h"
#include "rocksdb/advanced_cache.h"
#include "table/table_builder.h"
#include "table/table_reader.h"
#include "titan_logging.h"
#include "util.h"
#include "util/coding.h"

namespace rocksdb {
namespace titandb {

const char kBlobRawSizesProperty[] = "TitanDB.blob_raw_sizes.v1";
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

// Zip the array with physical entries, including retained internal versions.
// The public SstFileReader iterator filters versions and cannot be used here.
static Status LoadRawSizes(const Options& options, const std::string& path,
                           BlobRawSizes* result) {
  ImmutableOptions ioptions(options);
  MutableCFOptions moptions{ColumnFamilyOptions(options)};
  EnvOptions env_options(options);
  FileOptions file_options(env_options);
  const auto& fs = options.env->GetFileSystem();
  uint64_t file_size;
  std::unique_ptr<FSRandomAccessFile> file;
  Status s =
      fs->GetFileSize(path, file_options.io_options, &file_size, nullptr);
  if (s.ok()) s = fs->NewRandomAccessFile(path, file_options, &file, nullptr);
  if (!s.ok()) return s;
  std::unique_ptr<RandomAccessFileReader> file_reader(
      new RandomAccessFileReader(std::move(file), path));
  TableReaderOptions reader_options(ioptions, moptions.prefix_extractor,
                                    env_options, ioptions.internal_comparator,
                                    moptions.block_protection_bytes_per_key);
  reader_options.largest_seqno = kMaxSequenceNumber;
  std::unique_ptr<TableReader> reader;
  s = options.table_factory->NewTableReader(
      reader_options, std::move(file_reader), file_size, &reader);
  if (!s.ok()) return s;
  auto properties = reader->GetTableProperties();
  auto array =
      properties->user_collected_properties.find(kBlobRawSizesProperty);
  if (array == properties->user_collected_properties.end()) return Status::OK();
  Slice sizes(array->second);
  if (sizes.size() % sizeof(uint64_t) != 0) {
    return Status::Corruption("invalid blob raw-size array");
  }
  ReadOptions read_options;
  read_options.fill_cache = false;
  std::unique_ptr<InternalIterator> iter(
      reader->NewIterator(read_options, moptions.prefix_extractor.get(),
                          nullptr, false, TableReaderCaller::kCompaction));
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    ParsedInternalKey key;
    s = ParseInternalKey(iter->key(), &key, false);
    if (!s.ok()) return s;
    if (key.type != kTypeTitanBlobIndex) continue;
    if (sizes.empty()) return Status::Corruption("short blob raw-size array");
    uint64_t size = DecodeFixed64(sizes.data());
    sizes.remove_prefix(sizeof(uint64_t));
    if (size != kUnknownBlobRawSize) {
      auto inserted = result->emplace(iter->value().ToString(), size);
      if (!inserted.second && inserted.first->second != size) {
        return Status::Corruption("conflicting blob raw sizes");
      }
    }
  }
  if (!iter->status().ok()) return iter->status();
  return sizes.empty() ? Status::OK()
                       : Status::Corruption("long blob raw-size array");
}

void BlobRawSizeState::Begin(const Options& options,
                             const CompactionJobInfo& info) {
  auto sizes = std::make_shared<BlobRawSizes>();
  for (const auto& path : info.input_files) {
    auto table = info.table_properties.find(path);
    if (table != info.table_properties.end() &&
        table->second->user_collected_properties.count(kBlobRawSizesProperty) ==
            0) {
      continue;  // Legacy SST: backfill on output.
    }
    BlobRawSizes file_sizes;
    auto s = LoadRawSizes(options, path, &file_sizes);
    if (s.ok()) {
      sizes->insert(file_sizes.begin(), file_sizes.end());
    } else {
      // Never trust a partially matched array. Output builders will read blobs.
      TITAN_LOG_WARN(options.info_log, "Loading blob raw sizes from %s: %s",
                     path.c_str(), s.ToString().c_str());
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  inputs_[{info.thread_id, info.job_id}] = std::move(sizes);
}

void BlobRawSizeState::End(const CompactionJobInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);
  inputs_.erase({info.thread_id, info.job_id});
}

std::vector<std::shared_ptr<const BlobRawSizes>> BlobRawSizeState::Inputs() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::shared_ptr<const BlobRawSizes>> result;
  for (const auto& input : inputs_) result.push_back(input.second);
  return result;
}

}  // namespace titandb
}  // namespace rocksdb

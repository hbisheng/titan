#include "raw_size_iterator.h"

#include "blob_format.h"
#include "blob_raw_size.h"
#include "test_util/sync_point.h"
#include "util/coding.h"

namespace rocksdb {
namespace titandb {
namespace {
class RawSizeIterator final : public InternalIterator {
 public:
  RawSizeIterator(InternalIterator* input,
                  std::shared_ptr<const TableProperties> properties,
                  Slice sizes)
      : input_(input), properties_(std::move(properties)), sizes_(sizes) {}
  bool Valid() const override { return input_->Valid(); }
  Slice key() const override { return input_->key(); }
  Slice value() const override { return input_->value(); }
  Status status() const override { return input_->status(); }
  bool PrepareValue() override { return input_->PrepareValue(); }
  void SetPinnedItersMgr(PinnedIteratorsManager* manager) override {
    input_->SetPinnedItersMgr(manager);
  }
  bool IsKeyPinned() const override { return input_->IsKeyPinned(); }
  bool IsValuePinned() const override { return input_->IsValuePinned(); }
  void SeekToFirst() override { input_->SeekToFirst(); }
  void Seek(const Slice& key) override { input_->Seek(key); }
  void Next() override { input_->Next(); }
  // CompactionMergingIterator only uses forward iteration.
  void SeekForPrev(const Slice&) override { assert(false); }
  void SeekToLast() override { assert(false); }
  void Prev() override { assert(false); }
  uint64_t GetTitanBlobRawSize() const override {
    if (!Valid() || key().size() < 8 ||
        ExtractValueType(key()) != kTypeTitanBlobIndex)
      return kUnknownBlobRawSize;
    BlobIndex index;
    Slice encoded = value();
    if (!index.DecodeFrom(&encoded).ok()) return kUnknownBlobRawSize;
    size_t left = 0, right = sizes_.size() / kBlobRawSizeEntrySize;
    while (left < right) {
      const size_t mid = left + (right - left) / 2;
      const char* entry = sizes_.data() + mid * kBlobRawSizeEntrySize;
      const uint64_t file = DecodeFixed64(entry),
                     offset = DecodeFixed64(entry + 8);
      if (file < index.file_number ||
          (file == index.file_number && offset < index.blob_handle.offset))
        left = mid + 1;
      else
        right = mid;
    }
    if (left == sizes_.size() / kBlobRawSizeEntrySize)
      return kUnknownBlobRawSize;
    const char* entry = sizes_.data() + left * kBlobRawSizeEntrySize;
    return DecodeFixed64(entry) == index.file_number &&
                   DecodeFixed64(entry + 8) == index.blob_handle.offset
               ? DecodeFixed64(entry + 16)
               : kUnknownBlobRawSize;
  }

 private:
  std::unique_ptr<InternalIterator> input_;
  std::shared_ptr<const TableProperties> properties_;
  Slice sizes_;
};
}  // namespace

InternalIterator* NewRawSizeIterator(
    InternalIterator* input,
    std::shared_ptr<const TableProperties> properties) {
  if (!properties) return input;
  const auto& props = properties->user_collected_properties;
  auto sizes = props.find(kBlobRawSizesProperty);
  if (sizes == props.end() || sizes->second.empty() ||
      sizes->second.size() % kBlobRawSizeEntrySize != 0)
    return input;
  // Search the table reader's buffer directly; no decoded table or cursor
  // state.
  TEST_SYNC_POINT("RawSizeIterator::Created");
  return new RawSizeIterator(input, std::move(properties), sizes->second);
}
}  // namespace titandb
}  // namespace rocksdb

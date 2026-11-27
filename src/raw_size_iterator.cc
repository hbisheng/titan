#include "raw_size_iterator.h"

#include <algorithm>

#include "blob_raw_size.h"
#include "test_util/sync_point.h"
#include "util/coding.h"

namespace rocksdb {
namespace titandb {
namespace {
struct Anchor {
  Slice key;
  uint64_t index;
};

class RawSizeIterator final : public InternalIterator {
 public:
  RawSizeIterator(InternalIterator* input,
                  const InternalKeyComparator& comparator,
                  std::shared_ptr<const TableProperties> properties,
                  Slice sizes, std::vector<Anchor> anchors)
      : input_(input),
        comparator_(comparator),
        properties_(std::move(properties)),
        sizes_(sizes),
        anchors_(std::move(anchors)) {}
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
  void SeekToFirst() override {
    input_->SeekToFirst();
    index_ = 0;
    positioned_ = true;
  }
  void Seek(const Slice& key) override {
    input_->Seek(key);
    positioned_ = false;
    Locate();
  }
  // CompactionMergingIterator only uses forward iteration.
  void SeekForPrev(const Slice&) override { assert(false); }
  void SeekToLast() override { assert(false); }
  void Prev() override { assert(false); }
  void Next() override {
    if (positioned_ && IsBlob()) ++index_;
    input_->Next();
    if (!positioned_) Locate();
  }
  uint64_t GetTitanBlobRawSize() const override {
    if (!positioned_ || !IsBlob() ||
        index_ >= sizes_.size() / sizeof(uint64_t)) {
      return kUnknownBlobRawSize;
    }
    return DecodeFixed64(sizes_.data() + index_ * sizeof(uint64_t));
  }

 private:
  bool IsBlob() const {
    return Valid() && input_->key().size() >= 8 &&
           ExtractValueType(input_->key()) == kTypeTitanBlobIndex;
  }
  void Locate() {
    // Inline entries require no array position. Resolve only on the first blob
    // after a seek, using an anchor fewer than 128 physical data entries away.
    if (!IsBlob()) return;
    std::string target = input_->key().ToString();
    auto anchor =
        std::upper_bound(anchors_.begin(), anchors_.end(), Slice(target),
                         [&](const Slice& key, const Anchor& entry) {
                           return comparator_.Compare(key, entry.key) < 0;
                         });
    if (anchor == anchors_.begin()) return;
    --anchor;
    input_->Seek(anchor->key);
    if (Valid() && input_->key() == anchor->key) {
      index_ = anchor->index;
      unsigned steps = 0;
      while (Valid() && comparator_.Compare(input_->key(), target) < 0 &&
             steps++ < 128) {
        if (IsBlob()) ++index_;
        input_->Next();
        TEST_SYNC_POINT("RawSizeIterator::Locate:Next");
      }
      if (Valid() && input_->key() == target) {
        positioned_ = true;
        return;
      }
    }
    // Bad/obsolete metadata (e.g. rewritten global seqnos) must never move the
    // data cursor or invent a size. The builder can backfill the unknown size.
    input_->Seek(target);
  }
  std::unique_ptr<InternalIterator> input_;
  const InternalKeyComparator& comparator_;
  std::shared_ptr<const TableProperties> properties_;
  Slice sizes_;
  std::vector<Anchor> anchors_;
  uint64_t index_ = 0;
  bool positioned_ = false;
};
}  // namespace

InternalIterator* NewRawSizeIterator(
    InternalIterator* input, const InternalKeyComparator& comparator,
    std::shared_ptr<const TableProperties> properties) {
  if (!properties) return input;
  const auto& props = properties->user_collected_properties;
  auto sizes = props.find(kBlobRawSizesProperty);
  if (sizes == props.end() || sizes->second.empty()) return input;
  auto checkpoints = props.find(kBlobRawSizeAnchorsProperty);
  if (checkpoints == props.end() || sizes->second.size() % 8 != 0) return input;
  // Properties are already owned by the table reader. Keep slices, not copies
  // of the raw-size array or checkpoint keys.
  Slice encoded(checkpoints->second);
  std::vector<Anchor> anchors;
  while (!encoded.empty()) {
    Slice key;
    if (!GetLengthPrefixedSlice(&encoded, &key) || key.size() < 8 ||
        ExtractValueType(key) != kTypeTitanBlobIndex || encoded.size() < 8)
      return input;
    uint64_t index = DecodeFixed64(encoded.data());
    encoded.remove_prefix(8);
    if (index >= sizes->second.size() / 8 ||
        (anchors.empty()
             ? index != 0
             : index <= anchors.back().index ||
                   comparator.Compare(anchors.back().key, key) >= 0)) {
      return input;
    }
    anchors.push_back({key, index});
  }
  if (anchors.empty()) return input;
  TEST_SYNC_POINT("RawSizeIterator::Created");
  return new RawSizeIterator(input, comparator, std::move(properties),
                             sizes->second, std::move(anchors));
}
}  // namespace titandb
}  // namespace rocksdb

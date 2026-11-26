#include "blob_format.h"

#include "test_util/testharness.h"

#include "testutil.h"
#include "util.h"

namespace rocksdb {
namespace titandb {

class BlobFormatTest : public testing::Test {};

TEST(BlobFormatTest, BlobRecord) {
  BlobRecord input;
  CheckCodec(input);
  input.key = "hello";
  input.value = "world";
  CheckCodec(input);
}

TEST(BlobFormatTest, BlobHandle) {
  BlobHandle input;
  CheckCodec(input);
  input.offset = 2;
  input.size = 3;
  CheckCodec(input);
}

TEST(BlobFormatTest, BlobIndex) {
  BlobIndex input;
  CheckCodec(input);
  input.file_number = 1;
  input.blob_handle.offset = 2;
  input.blob_handle.size = 3;
  CheckCodec(input);
}

TEST(BlobFormatTest, RawValueSize) {
  BlobIndex index;
  index.file_number = 1;
  index.blob_handle.offset = 2;
  index.blob_handle.size = 3;
  std::string legacy;
  index.EncodeTo(&legacy);
  ASSERT_EQ(legacy, std::string("\x01\x01\x02\x03", 4));
  for (uint64_t size :
       {uint64_t(0), uint64_t(1), uint64_t(1000000), UINT64_MAX}) {
    index.has_raw_value_size = true;
    index.raw_value_size = size;
    std::string encoded;
    index.EncodeTo(&encoded);
    ASSERT_EQ(encoded[0], BlobIndex::kBlobRecordWithRawSize);
    BlobIndex decoded;
    ASSERT_OK(DecodeInto(encoded, &decoded));
    ASSERT_TRUE(decoded.has_raw_value_size);
    ASSERT_EQ(decoded.raw_value_size, size);
    ASSERT_EQ(decoded, index);
    ASSERT_OK(DecodeInto(legacy, &decoded));
    ASSERT_FALSE(decoded.has_raw_value_size);
    ASSERT_EQ(decoded.raw_value_size, 0);
    // GC must recognize the same record before and after size backfill.
    ASSERT_EQ(decoded, index);
    encoded.pop_back();
    ASSERT_TRUE(DecodeInto(encoded, &decoded).IsCorruption());
  }
  BlobIndex decoded;
  ASSERT_TRUE(
      DecodeInto(std::string("\x03\x01\x02\x03", 4), &decoded).IsCorruption());
  ASSERT_TRUE(DecodeInto(legacy + "x", &decoded).IsCorruption());
}

TEST(BlobFormatTest, BlobFileMeta) {
  BlobFileMeta input(2, 3, 0, 0, "0", "9");
  CheckCodec(input);
}

TEST(BlobFormatTest, BlobFileFooter) {
  BlobFileFooter input;
  CheckCodec(input);
  input.meta_index_handle.set_offset(123);
  input.meta_index_handle.set_size(321);
  CheckCodec(input);
}

TEST(BlobFormatTest, BlobFileStateTransit) {
  BlobFileMeta blob_file;
  ASSERT_EQ(blob_file.file_state(), BlobFileMeta::FileState::kNone);
  blob_file.FileStateTransit(BlobFileMeta::FileEvent::kDbStart);
  ASSERT_EQ(blob_file.file_state(), BlobFileMeta::FileState::kPendingInit);
  blob_file.FileStateTransit(BlobFileMeta::FileEvent::kDbInit);
  ASSERT_EQ(blob_file.file_state(), BlobFileMeta::FileState::kNormal);
  blob_file.FileStateTransit(BlobFileMeta::FileEvent::kGCBegin);
  ASSERT_EQ(blob_file.file_state(), BlobFileMeta::FileState::kBeingGC);
  blob_file.FileStateTransit(BlobFileMeta::FileEvent::kGCCompleted);

  BlobFileMeta compaction_output;
  ASSERT_EQ(compaction_output.file_state(), BlobFileMeta::FileState::kNone);
  compaction_output.FileStateTransit(
      BlobFileMeta::FileEvent::kFlushOrCompactionOutput);
  ASSERT_EQ(compaction_output.file_state(),
            BlobFileMeta::FileState::kPendingLSM);
  compaction_output.FileStateTransit(
      BlobFileMeta::FileEvent::kCompactionCompleted);
  ASSERT_EQ(compaction_output.file_state(), BlobFileMeta::FileState::kNormal);
}

TEST(BlobFormatTest, BlobCompressionLZ4) {
  BlobEncoder encoder(kLZ4Compression);
  BlobDecoder decoder;

  BlobRecord record;
  record.key = "key1";
  record.value = "value1";

  encoder.EncodeRecord(record);
  Slice encoded_record = encoder.GetRecord();
  Slice encoded_header = encoder.GetHeader();

  decoder.DecodeHeader(&encoded_header);

  BlobRecord decoded_record;
  OwnedSlice blob;
  decoder.DecodeRecord(&encoded_record, &decoded_record, &blob);

  ASSERT_EQ(record, decoded_record);
}

#if defined(ZSTD)

std::string CreateDict() {
  const int sample_count = 1000;
  std::string samples = "";
  std::vector<size_t> sample_lens;

  BlobRecord record;
  BlobEncoder encoder(kZSTD);

  for (int i = 0; i < sample_count; ++i) {
    std::string key = "key" + std::to_string(i);
    std::string value = "value" + std::to_string(i);
    record.key = Slice(key);
    record.value = Slice(value);
    encoder.EncodeRecord(record);

    std::string encoded_record = encoder.GetRecord().ToString();
    sample_lens.push_back(encoded_record.size());
    samples += encoded_record;
  }

  return ZSTD_TrainDictionary(samples, sample_lens, 4000);
}

TEST(BlobFormatTest, BlobCompressionZSTD) {
  auto dict = CreateDict();
  CompressionDict compression_dict(dict, kZSTD, 10);
  UncompressionDict uncompression_dict(dict, true);

  BlobEncoder encoder(kZSTD, &compression_dict);
  BlobDecoder decoder(&uncompression_dict, kZSTD);

  BlobRecord record;
  record.key = "key1";
  record.value = "value1";

  encoder.EncodeRecord(record);
  Slice encoded_record = encoder.GetRecord();
  Slice encoded_header = encoder.GetHeader();

  decoder.DecodeHeader(&encoded_header);

  BlobRecord decoded_record;
  OwnedSlice blob;
  decoder.DecodeRecord(&encoded_record, &decoded_record, &blob);

  ASSERT_EQ(record, decoded_record);
}

#endif  // ZSTD

}  // namespace titandb
}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#pragma once

#include "rocksdb/table_properties.h"
#include "table/internal_iterator.h"

namespace rocksdb {
namespace titandb {

// Own a heap-allocated compaction input and reuse its property buffers.
// Ordinary/legacy SSTs keep the original input.
InternalIterator* NewRawSizeIterator(
    InternalIterator* input, const InternalKeyComparator& comparator,
    std::shared_ptr<const TableProperties> properties);

}  // namespace titandb
}  // namespace rocksdb

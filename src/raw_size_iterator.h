#pragma once

#include "rocksdb/table_properties.h"
#include "table/internal_iterator.h"

namespace rocksdb {
namespace titandb {

// Reuse the table reader's property buffers. Ordinary/legacy SSTs keep input.
InternalIterator* NewRawSizeIterator(
    InternalIterator* input, const InternalKeyComparator& comparator,
    std::shared_ptr<const TableProperties> properties, Arena* arena);

}  // namespace titandb
}  // namespace rocksdb

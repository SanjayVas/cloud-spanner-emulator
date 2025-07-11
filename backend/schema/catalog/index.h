//
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_INDEX_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_INDEX_H_

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "backend/schema/catalog/column.h"
#include "backend/schema/catalog/locality_group.h"
#include "backend/schema/catalog/table.h"
#include "backend/schema/graph/schema_node.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

// Index represents a secondary index on a table.
//
// The index stores pointers to the indexed table as well as the backing data
// table which stores the index information (index columns + indexed table
// primary key columns + storing columns). The primary key of the backing data
// table will be the primary key of the indexed table prefixed by the index
// columns (with duplicate columns removed) to guarantee uniqueness of the data
// table key.
//
// Example:
//
// CREATE TABLE Albums (
//   SingerId     INT64 NOT NULL,
//   AlbumId      INT64 NOT NULL,
//   AlbumTitle   STRING(MAX),
//   ReleaseDate  DATE
// ) PRIMARY KEY (SingerId, AlbumId),
//   INTERLEAVE IN PARENT Singers ON DELETE CASCADE;
//
// CREATE INDEX AlbumsByAlbumTitle ON Albums(AlbumTitle);
//
// This will create an index 'AlbumsByAlbumTitle' on the table 'Albums'. The
// index data table will store the column 'AlbumTitle' which is the index
// column, followed by 'SingerId', 'AlbumId' which are the primary key columns
// of indexed table. If any storing columns were present they would be appended
// to the end.
//
class Index : public SchemaNode {
 public:
  // Returns the name of this index.
  std::string Name() const { return name_; }

  // Returns the table that is indexed.
  const Table* indexed_table() const { return indexed_table_; }

  // Returns the backing table which stores the index data.
  const Table* index_data_table() const { return index_data_table_; }

  // Returns the parent table that the index data table is interleaved in, if
  // one exists.
  const Table* parent() const;

  // Returns the key columns of the index as declared in the CREATE INDEX
  // statement.
  absl::Span<const KeyColumn* const> key_columns() const {
    return key_columns_;
  }

  // Returns the list of all the storing columns.
  absl::Span<const Column* const> stored_columns() const {
    return stored_columns_;
  }

  // Returns the list of all the null filtered columns.
  absl::Span<const Column* const> null_filtered_columns() const {
    return null_filtered_columns_;
  }

  bool is_null_filtered_column(const Column* column) const {
    auto it = std::find(null_filtered_columns_.begin(),
                        null_filtered_columns_.end(), column);
    return it != null_filtered_columns_.end();
  }
  // Returns true if this is a unique index.
  bool is_unique() const { return is_unique_; }

  // Returns true if this index has NULL_FILTERED enabled.
  bool is_null_filtered() const { return is_null_filtered_; }

  // Returns true if this index is managed by other schema nodes. Managed
  // indexes are regular indexes except for their lifecycles. Users cannot
  // create, alter or drop managed indexes.
  bool is_managed() const { return !managing_nodes_.empty(); }

  // Returns the nodes that are managing this index.
  absl::Span<const SchemaNode* const> managing_nodes() const {
    return managing_nodes_;
  }

  // Returns the type of the index.
  bool is_search_index() const {
    return index_type_ == IndexType::kSearchIndex;
  }

  bool is_vector_index() const {
    return index_type_ == IndexType::kVectorIndex;
  }

  // Returns the list of partition by column defined in the search index.
  absl::Span<const Column* const> partition_by() const { return partition_by_; }

  // Returns the list of order by column defined in the search index.
  absl::Span<const KeyColumn* const> order_by() const { return order_by_; }

  // Returns a detailed string which lists information about this index.
  std::string FullDebugString() const;

  // Returns the locality group this index belongs to.
  const LocalityGroup* locality_group() const { return locality_group_; }

  // SchemaNode interface implementation.
  // ------------------------------------

  ddl::VectorIndexOptionsProto vector_index_options() const {
    return vector_index_options_;
  }

  std::optional<SchemaNameInfo> GetSchemaNameInfo() const override {
    return SchemaNameInfo{.name = name_, .kind = "Index", .global = true};
  }

  absl::Status Validate(SchemaValidationContext* context) const override;

  absl::Status ValidateUpdate(const SchemaNode* old,
                              SchemaValidationContext* context) const override;

  std::string DebugString() const override;

  class Builder;
  class Editor;

 private:
  friend class IndexValidator;

  using ValidationFn =
      std::function<absl::Status(const Index*, SchemaValidationContext*)>;
  using UpdateValidationFn = std::function<absl::Status(
      const Index*, const Index*, SchemaValidationContext*)>;

  // Constructors are private and only friend classes are able to build /
  // modify.
  Index(const ValidationFn& validate, const UpdateValidationFn& validate_update)
      : validate_(validate), validate_update_(validate_update) {}
  Index(const Index&) = default;

  std::unique_ptr<SchemaNode> ShallowClone() const override {
    return absl::WrapUnique(new Index(*this));
  }

  absl::Status DeepClone(SchemaGraphEditor* editor,
                         const SchemaNode* orig) override;

  // Validation delegates.
  const ValidationFn validate_;

  const UpdateValidationFn validate_update_;

  // The name of this index.
  std::string name_;

  // The table that this index references.
  const Table* indexed_table_;

  // The backing table that stores the index data.
  const Table* index_data_table_;

  // The columns declared as the index's key, in the same order
  // as they appear in the CREATE INDEX statement. References are
  // to the corresponding KeyColumn(s) in 'index_data_table_'.
  std::vector<const KeyColumn*> key_columns_;

  // Additional columns specified in the 'STORING' clause in the same
  // order as they appear in the CREATE INDEX statement. References are
  // to the corresponding columns in 'index_data_table_'.
  std::vector<const Column*> stored_columns_;

  // Nodes that are managing this index. The first node creates the index and
  // adds itself as a managing node. Subsequent nodes that can share this index
  // add themselves as a managing node rather than creating a new index. Dropped
  // nodes remove themselves. The last node dropped also drops this index.
  std::vector<const SchemaNode*> managing_nodes_;

  // Whether the indexed columns form a unique key. If true, additional
  // constraints will be checked to enforce uniqueness for the Index.
  bool is_unique_ = false;

  // Whether this index has NULL_FILTERED enabled which applies to all index key
  // columns.
  bool is_null_filtered_ = false;
  // Columns specified in the WHERE IS NOT NULL clause. References are
  // to the corresponding columns in 'index_data_table_'.
  std::vector<const Column*> null_filtered_columns_;

  // The type of the index.
  enum class IndexType { kIndex, kSearchIndex, kVectorIndex };
  IndexType index_type_ = IndexType::kIndex;

  // Currently applies only to search index. A list of key parts that the index
  // is partitioned by. If this is empty, then the index is not partitioned.
  std::vector<const Column*> partition_by_;

  // Currently applies only to search index. A list of key parts that the index
  // is ordered by. If this is empty, then the index is unordered.
  std::vector<const KeyColumn*> order_by_;

  // Applies only to vector index. The options for the vector index.
  ddl::VectorIndexOptionsProto vector_index_options_;

  // The locality group this index belongs to.
  const LocalityGroup* locality_group_ = nullptr;
};

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_INDEX_H_

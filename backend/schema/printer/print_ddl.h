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

#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_PRINT_DDL_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_PRINT_DDL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "zetasql/public/types/type.h"
#include "absl/status/statusor.h"
#include "backend/schema/catalog/change_stream.h"
#include "backend/schema/catalog/check_constraint.h"
#include "backend/schema/catalog/column.h"
#include "backend/schema/catalog/foreign_key.h"
#include "backend/schema/catalog/index.h"
#include "backend/schema/catalog/locality_group.h"
#include "backend/schema/catalog/named_schema.h"
#include "backend/schema/catalog/placement.h"
#include "backend/schema/catalog/proto_bundle.h"
#include "backend/schema/catalog/schema.h"
#include "backend/schema/catalog/sequence.h"
#include "backend/schema/catalog/table.h"
#include "backend/schema/catalog/udf.h"
#include "google/protobuf/repeated_ptr_field.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

// Prints the DDL statement for a column.
std::string PrintColumn(const Column* column);

// Prints the DDL statement for this KeyColumn.
std::string PrintKeyColumn(const KeyColumn* column);

// Prints the DDL statements for an index.
std::string PrintIndex(const Index* index);

// Prints the DDL statements for a table.
std::string PrintTable(const Table* table);

// Prints the DDL statements for options.
std::string PrintOptions(::google::protobuf::RepeatedPtrField<ddl::SetOption> options);

// Prints the DDL statements for a change stream.
std::string PrintChangeStream(const ChangeStream* change_stream);

// Prints the DDL statements for a check constraint.
std::string PrintCheckConstraint(const CheckConstraint* check_constraint);

// Prints the DDL statements for a foreign key.
std::string PrintForeignKey(const ForeignKey* foreign_key);

// Prints the DDL statements for a sequence.
std::string PrintSequence(const Sequence* sequence);

// Prints the DDL statements for a named schema.
std::string PrintNamedSchema(const NamedSchema* named_schema);

// Prints the DDL statements for a udf.
std::string PrintUdf(const Udf* udf);

// Converts an OnDeleteAction to its string representation.
std::string OnDeleteActionToString(Table::OnDeleteAction action);

// Converts a RowDeletionPolicy to its string representation.
std::string RowDeletionPolicyToString(const ddl::RowDeletionPolicy& policy);

// Converts a Cloud Spanner column type to its string representation.
std::string ColumnTypeToString(const zetasql::Type* type,
                               std::optional<int64_t> max_length);

// Converts proto_bundle types to its string representation.
std::string PrintProtoBundle(std::shared_ptr<const ProtoBundle> proto_bundle);

// Prints the DDL statements for a locality group.
std::string PrintLocalityGroup(const LocalityGroup* locality_group);

// Prints the DDL statements for a locality group options.
std::string PrintLocalityGroupOptions(
    ::google::protobuf::RepeatedPtrField<ddl::SetOption> options);

// Prints the DDL statements for a placement.
std::string PrintPlacement(const Placement* placement);

// Prints the DDL statements for all tables and indexes within the given schema.
absl::StatusOr<std::vector<std::string>> PrintDDLStatements(
    const Schema* schema);

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_PRINT_DDL_H_

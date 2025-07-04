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

#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_QUERY_TABLES_FROM_METADATA_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_QUERY_TABLES_FROM_METADATA_H_

#include <memory>
#include <string>
#include <vector>

#include "zetasql/public/simple_catalog.h"
#include "zetasql/public/types/type.h"
#include "zetasql/public/types/type_factory.h"
#include "zetasql/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "backend/query/info_schema_columns_metadata_values.h"
#include "third_party/spanner_pg/datatypes/extended/pg_oid_type.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

// Maps the type specified in the information catalog metadata for a Spanner
// ZetaSQL database to a ZetaSQL type.
static const zetasql_base::NoDestructor<
    absl::flat_hash_map<std::string, const zetasql::Type*>>
    kSpannerTypeToGSQLType{{
        {"BOOL", zetasql::types::BoolType()},
        {"DATE", zetasql::types::DateType()},
        {"INT64", zetasql::types::Int64Type()},
        {"FLOAT64", zetasql::types::FloatType()},
        {"STRING(32)", zetasql::types::StringType()},
        {"STRING(100)", zetasql::types::StringType()},
        {"STRING(MAX)", zetasql::types::StringType()},
        {"TIMESTAMP", zetasql::types::TimestampType()},
        {"JSON", zetasql::types::JsonType()},
    }};

// Maps the type specified in the information catalog metadata for a Spanner
// PostgreSQL database to a ZetaSQL type.
static const zetasql_base::NoDestructor<
    absl::flat_hash_map<std::string, const zetasql::Type*>>
    kSpannerPGTypeToGSQLType{{
        {"bigint", zetasql::types::Int64Type()},
        {"bigint[]", zetasql::types::Int64ArrayType()},
        {"boolean", zetasql::types::BoolType()},
        {"character varying", zetasql::types::StringType()},
        {"character varying[]", zetasql::types::StringArrayType()},
        {"double precision", zetasql::types::DoubleType()},
        {"oid", postgres_translator::spangres::datatypes::GetPgOidType()},
        {"oid[]",
         postgres_translator::spangres::datatypes::GetPgOidArrayType()},
        {"timestamp with time zone", zetasql::types::TimestampType()},
    }};

// Given a list of ColumnsMetaEntry items, returns SimpleTables that can be
// added to a SimpleCatalog mapped by the table name. The tables are created by
// mapping the spanner type in the ColumnsMetaEntry to the ZetaSQL type given
// by the provided mapping. Only tables for the given supported list of tables
// is returned. The metadata entries must be ordered by table name.
absl::flat_hash_map<std::string, std::unique_ptr<zetasql::SimpleTable>>
AddTablesFromMetadata(
    const std::vector<ColumnsMetaEntry>& metadata_entries,
    const absl::flat_hash_map<std::string, const zetasql::Type*>&
        spanner_to_gsql_type,
    const absl::flat_hash_set<std::string>& supported_tables);

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_QUERY_TABLES_FROM_METADATA_H_

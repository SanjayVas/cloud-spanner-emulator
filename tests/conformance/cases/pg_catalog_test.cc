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

#include <cstdint>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "zetasql/base/testing/status_matchers.h"
#include "tests/common/proto_matchers.h"
#include "zetasql/base/no_destructor.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "google/cloud/spanner/oid.h"
#include "google/cloud/spanner/value.h"
#include "tests/common/scoped_feature_flags_setter.h"
#include "tests/conformance/common/database_test_base.h"

namespace google {
namespace spanner {
namespace emulator {
namespace test {

namespace {

using ::google::cloud::spanner::PgOid;

static const zetasql_base::NoDestructor<absl::flat_hash_set<std::string>>
    kSupportedButEmptyTables{{
        "pg_available_extension_versions",
        "pg_available_extensions",
        "pg_backend_memory_contexts",
        "pg_config",
        "pg_cursors",
        "pg_file_settings",
        "pg_hba_file_rules",
        "pg_matviews",
        "pg_policies",
        "pg_prepared_xacts",
        "pg_publication_tables",
        "pg_rules",
        "pg_shmem_allocations",
    }};

class PGCatalogTest : public DatabaseTest {
 public:
  PGCatalogTest() : feature_flags_({.enable_postgresql_interface = true}) {}

  void SetUp() override {
    dialect_ = database_api::DatabaseDialect::POSTGRESQL;
    DatabaseTest::SetUp();
  }

  absl::Status SetUpDatabase() override {
    return SetSchemaFromFile("information_schema.test");
  }

  cloud::spanner::Value Ns() { return Null<std::string>(); }
  cloud::spanner::Value Nb() { return Null<bool>(); }
  cloud::spanner::Value Ni64() { return Null<int64_t>(); }
  cloud::spanner::Value Nd() { return Null<double>(); }
  cloud::spanner::Value NOid() { return Null<PgOid>(); }
  cloud::spanner::Value Ni64Array() { return Null<std::vector<int64_t>>(); }
  cloud::spanner::Value NOidArray() { return Null<std::vector<PgOid>>(); }

  // Returns the given rows, replacing matching string patterns with their
  // actual values from the given results.
  static std::vector<ValueRow> ExpectedRows(
      const absl::StatusOr<std::vector<ValueRow>>& results,
      const std::vector<ValueRow> rows) {
    if (!results.ok()) {
      return rows;
    }
    std::vector<ValueRow> expected;
    for (const ValueRow& row : rows) {
      ValueRow next;
      for (int i = 0; i < row.values().size(); ++i) {
        Value value = row.values()[i];
        if (value.get<std::string>().ok()) {
          std::string pattern = value.get<std::string>().value();
          value = Value(FindString(results, i, pattern));
        }
        next.add(value);
      }
      expected.push_back(next);
    }
    return expected;
  }

  // Returns the first result string that matches a pattern. Returns the pattern
  // if none match. One use case is to match generated names that have
  // different signatures between production and emulator.
  static std::string FindString(
      const absl::StatusOr<std::vector<ValueRow>>& results, int field_index,
      const std::string& pattern) {
    for (const auto& row : results.value()) {
      auto value = row.values()[field_index].get<std::string>().value();
      if (RE2::FullMatch(value, pattern)) {
        return value;
      }
    }
    return pattern;
  }

 protected:
  absl::Status PopulateDatabase() { return absl::OkStatus(); }

 private:
  test::ScopedEmulatorFeatureFlagsSetter feature_flags_;
};

TEST_F(PGCatalogTest, PGAm) {
  auto expected =
      std::vector<ValueRow>({{PgOid(75001), "spanner_default", "t"},
                             {PgOid(75002), "spanner_default", "i"}});
  EXPECT_THAT(Query(R"sql(
      SELECT
        oid,
        amname,
        amtype
      FROM
        pg_catalog.pg_am
      ORDER BY
        amtype DESC)sql"),
              IsOkAndHoldsRows({expected}));
}

TEST_F(PGCatalogTest, PGAttrdef) {
  // Oid assignment differs from production so we cannot assert those.
  auto expected = std::vector<ValueRow>({
      {19, "(key1 + '1'::bigint)"},
      {20, "length(key2)"},
      {21, "'100'::bigint"},
      {22, "CURRENT_TIMESTAMP"},
  });
  EXPECT_THAT(Query(R"sql(
      SELECT
        adnum,
        adbin
      FROM
        pg_catalog.pg_attrdef
      ORDER BY
        oid)sql"),
              IsOkAndHoldsRows(expected));

  // Instead, assert that the distinct OID counts.
  EXPECT_THAT(Query(R"sql(
      SELECT
        COUNT(DISTINCT oid),
        COUNT(DISTINCT adrelid)
      FROM
        pg_catalog.pg_attrdef)sql"),
              IsOkAndHoldsRows({{4, 1}}));
}

TEST_F(PGCatalogTest, PGAttribute) {
  constexpr absl::string_view query_template = R"sql(
      SELECT
        c.relname,
        attname,
        t.typname,
        attnum,
        attndims,
        attcacheoff,
        attcompression,
        attnotnull,
        atthasdef,
        atthasmissing,
        attidentity,
        attgenerated,
        attisdropped,
        attislocal,
        attinhcount
      FROM
        pg_catalog.pg_attribute AS a
      JOIN
        pg_catalog.pg_class AS c
        ON a.attrelid = c.oid
      JOIN
        pg_catalog.pg_type AS t
        ON a.atttypid = t.oid
      WHERE
        c.relnamespace != 11 AND c.relnamespace != 75003 AND
        c.relnamespace != 75004 AND c.relkind = '%s'
      ORDER BY
        c.relnamespace, c.relname, attnum)sql";

  // Table attributes.
  auto expected = std::vector<ValueRow>({
      {"base", "key1", "int8", 1, 0, -1, std::string{'\0'}, true, false, false,
       std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "key2", "varchar", 2, 0, -1, std::string{'\0'}, true, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "bool_value", "bool", 3, 0, -1, std::string{'\0'}, false, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "int_value", "int8", 4, 0, -1, std::string{'\0'}, true, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "float_value", "float4", 5, 0, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "double_value", "float8", 6, 0, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "str_value", "varchar", 7, 0, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "byte_value", "bytea", 8, 0, -1, std::string{'\0'}, false, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "timestamp_value", "timestamptz", 9, 0, -1, std::string{'\0'},
       false, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},
      {"base", "date_value", "date", 10, 0, -1, std::string{'\0'}, false, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "bool_array", "_bool", 11, 1, -1, std::string{'\0'}, true, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "int_array", "_int8", 12, 1, -1, std::string{'\0'}, false, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "float_array", "_float4", 13, 1, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "double_array", "_float8", 14, 1, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "str_array", "_varchar", 15, 1, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "byte_array", "_bytea", 16, 1, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "timestamp_array", "_timestamptz", 17, 1, -1, std::string{'\0'},
       false, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},
      {"base", "date_array", "_date", 18, 1, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "gen_value", "int8", 19, 0, -1, std::string{'\0'}, false, false,
       false, std::string{'\0'}, "s", false, true, 0},
      {"base", "gen_function_value", "int8", 20, 0, -1, std::string{'\0'},
       false, false, false, std::string{'\0'}, "s", false, true, 0},
      {"base", "default_col_value", "int8", 21, 0, -1, std::string{'\0'}, false,
       true, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"base", "default_timestamp_col_value", "timestamptz", 22, 0, -1,
       std::string{'\0'}, false, true, false, std::string{'\0'},
       std::string{'\0'}, false, true, 0},
      {"base", "identity_no_params_col", "int8", 23, 0, -1, std::string{'\0'},
       false, true, false, std::string{'d'}, std::string{'\0'}, false, true, 0},
      {"base", "identity_col", "int8", 24, 0, -1, std::string{'\0'}, false,
       true, false, std::string{'d'}, std::string{'\0'}, false, true, 0},

      {"cascade_child", "key1", "int8", 1, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"cascade_child", "key2", "varchar", 2, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"cascade_child", "child_key", "bool", 3, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"cascade_child", "value1", "varchar", 4, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"cascade_child", "value2", "bool", 5, 0, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"cascade_child", "created_at", "timestamptz", 6, 0, -1,
       std::string{'\0'}, false, false, false, std::string{'\0'},
       std::string{'\0'}, false, true, 0},

      {"no_action_child", "key1", "int8", 1, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"no_action_child", "key2", "varchar", 2, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"no_action_child", "child_key", "bool", 3, 0, -1, std::string{'\0'},
       true, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},
      {"no_action_child", "value", "varchar", 4, 0, -1, std::string{'\0'},
       false, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},

      {"row_deletion_policy", "key", "int8", 1, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"row_deletion_policy", "created_at", "timestamptz", 2, 0, -1,
       std::string{'\0'}, false, false, false, std::string{'\0'},
       std::string{'\0'}, false, true, 0},

      // named_schema.ns_table_1
      {"ns_table_1", "key1", "int8", 1, 0, -1, std::string{'\0'}, true, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"ns_table_1", "key2", "varchar", 2, 0, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"ns_table_1", "bool_value", "bool", 3, 0, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},

      {"ns_table_2", "key1", "int8", 1, 0, -1, std::string{'\0'}, true, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"ns_table_2", "key2", "int8", 2, 0, -1, std::string{'\0'}, false, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},

      // named_schema2.ns_table_1
      {"ns_table_1", "key1", "int8", 1, 0, -1, std::string{'\0'}, true, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"ns_table_1", "key2", "varchar", 2, 0, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"ns_table_1", "bool_value", "bool", 3, 0, -1, std::string{'\0'}, false,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
  });
  EXPECT_THAT(Query(absl::StrFormat(query_template, "r")),
              IsOkAndHoldsRows(expected));

  // Index attributes.
  expected = std::vector<ValueRow>({
      {"IDX_base_bool_value_key2_N_\\w{16}", "bool_value", "bool", 1, 0, -1,
       std::string{'\0'}, true, false, false, std::string{'\0'},
       std::string{'\0'}, false, true, 0},
      {"IDX_base_bool_value_key2_N_\\w{16}", "key2", "varchar", 2, 0, -1,
       std::string{'\0'}, true, false, false, std::string{'\0'},
       std::string{'\0'}, false, true, 0},

      {"IDX_cascade_child_child_key_value1_U_\\w{16}", "child_key", "bool", 1,
       0, -1, std::string{'\0'}, true, false, false, std::string{'\0'},
       std::string{'\0'}, false, true, 0},
      {"IDX_cascade_child_child_key_value1_U_\\w{16}", "value1", "varchar", 2,
       0, -1, std::string{'\0'}, true, false, false, std::string{'\0'},
       std::string{'\0'}, false, true, 0},

      {"PK_base", "key1", "int8", 1, 0, -1, std::string{'\0'}, true, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"PK_base", "key2", "varchar", 2, 0, -1, std::string{'\0'}, true, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},

      {"PK_cascade_child", "key1", "int8", 1, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"PK_cascade_child", "key2", "varchar", 2, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"PK_cascade_child", "child_key", "bool", 3, 0, -1, std::string{'\0'},
       true, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},

      {"PK_no_action_child", "key1", "int8", 1, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
      {"PK_no_action_child", "key2", "varchar", 2, 0, -1, std::string{'\0'},
       true, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},
      {"PK_no_action_child", "child_key", "bool", 3, 0, -1, std::string{'\0'},
       true, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},
      {"PK_row_deletion_policy", "key", "int8", 1, 0, -1, std::string{'\0'},
       true, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},

      {"cascade_child_by_value", "key1", "int8", 1, 0, -1, std::string{'\0'},
       true, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},
      {"cascade_child_by_value", "key2", "varchar", 2, 0, -1, std::string{'\0'},
       true, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},
      {"cascade_child_by_value", "value2", "bool", 3, 0, -1, std::string{'\0'},
       true, false, false, std::string{'\0'}, std::string{'\0'}, false, true,
       0},
      {"cascade_child_by_value", "value1", "varchar", 5, 0, -1,
       std::string{'\0'}, true, false, false, std::string{'\0'},
       std::string{'\0'}, false, true, 0},

      {"no_action_child_by_value", "value", "varchar", 1, 0, -1,
       std::string{'\0'}, false, false, false, std::string{'\0'},
       std::string{'\0'}, false, true, 0},

      // named_schema.ns_table_1
      {"PK_ns_table_1", "key1", "int8", 1, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},

      {"PK_ns_table_2", "key1", "int8", 1, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},

      {"ns_index", "key1", "int8", 1, 0, -1, std::string{'\0'}, true, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},

      // named_schema2.ns_table_1
      {"PK_ns_table_1", "key1", "int8", 1, 0, -1, std::string{'\0'}, true,
       false, false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
  });
  auto results = Query(absl::StrFormat(query_template, "i"));
  EXPECT_THAT(*results, ExpectedRows(*results, expected));

  // View attributes.
  expected = std::vector<ValueRow>({
      {"base_view", "key1", "int8", 1, 0, -1, std::string{'\0'}, false, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},

      {"ns_view", "key1", "int8", 1, 0, -1, std::string{'\0'}, false, false,
       false, std::string{'\0'}, std::string{'\0'}, false, true, 0},
  });
  EXPECT_THAT(Query(absl::StrFormat(query_template, "v")),
              IsOkAndHoldsRows(expected));
}

TEST_F(PGCatalogTest, PGClass) {
  constexpr absl::string_view query_template = R"sql(
      SELECT
        relname,
        nspname,
        relam,
        relhasindex,
        relpersistence,
        relkind,
        relnatts,
        relchecks,
        relispopulated
      FROM
        pg_catalog.pg_class AS c
      JOIN
        pg_catalog.pg_namespace AS n
      ON
        c.relnamespace = n.oid
      WHERE
        relkind = '%s' AND relnamespace != 11 AND relnamespace != 75003
        AND relnamespace != 75004
      ORDER BY
        relnamespace, relname)sql";

  auto sequence_results = Query(absl::StrFormat(query_template, "S"));
  auto expected_sequence_rows = std::vector<ValueRow>({
      {"test_sequence", "public", PgOid(0), false, "p", "S", Ni64(), 0, true},
  });
  ZETASQL_EXPECT_OK(sequence_results);
  EXPECT_THAT(*sequence_results,
              ExpectedRows(*sequence_results, expected_sequence_rows));

  auto index_results = Query(absl::StrFormat(query_template, "i"));
  auto expected_index_rows = std::vector<ValueRow>({
      {"IDX_base_bool_value_key2_N_\\w{16}", "public", PgOid(75002), false, "p",
       "i", 2, 0, true},
      {"IDX_cascade_child_child_key_value1_U_\\w{16}", "public", PgOid(75002),
       false, "p", "i", 2, 0, true},
      {"PK_base", "public", PgOid(75002), false, "p", "i", 2, 0, true},
      {"PK_cascade_child", "public", PgOid(75002), false, "p", "i", 3, 0, true},
      {"PK_no_action_child", "public", PgOid(75002), false, "p", "i", 3, 0,
       true},
      {"PK_row_deletion_policy", "public", PgOid(75002), false, "p", "i", 1, 0,
       true},
      {"cascade_child_by_value", "public", PgOid(75002), false, "p", "i", 4, 0,
       true},
      {"no_action_child_by_value", "public", PgOid(75002), false, "p", "i", 1,
       0, true},

      {"PK_ns_table_1", "named_schema", PgOid(75002), false, "p", "i", 1, 0,
       true},
      {"PK_ns_table_2", "named_schema", PgOid(75002), false, "p", "i", 1, 0,
       true},
      {"ns_index", "named_schema", PgOid(75002), false, "p", "i", 1, 0, true},

      {"PK_ns_table_1", "named_schema2", PgOid(75002), false, "p", "i", 1, 0,
       true},
  });
  ZETASQL_EXPECT_OK(index_results);
  EXPECT_THAT(*index_results,
              ExpectedRows(*index_results, expected_index_rows));

  auto table_results = Query(absl::StrFormat(query_template, "r"));
  auto expected_table_rows = std::vector<ValueRow>({
      {"base", "public", PgOid(75001), true, "p", "r", 24, 2, true},
      {"cascade_child", "public", PgOid(75001), true, "p", "r", 6, 0, true},
      {"no_action_child", "public", PgOid(75001), true, "p", "r", 4, 0, true},
      {"row_deletion_policy", "public", PgOid(75001), false, "p", "r", 2, 0,
       true},

      {"ns_table_1", "named_schema", PgOid(75001), true, "p", "r", 3, 0, true},
      {"ns_table_2", "named_schema", PgOid(75001), false, "p", "r", 2, 0, true},

      {"ns_table_1", "named_schema2", PgOid(75001), false, "p", "r", 3, 0,
       true},  // NOLINT
  });
  ZETASQL_EXPECT_OK(table_results);
  EXPECT_THAT(*table_results,
              ExpectedRows(*table_results, expected_table_rows));

  auto view_results = Query(absl::StrFormat(query_template, "v"));
  auto expected_view_rows = std::vector<ValueRow>({
      {"base_view", "public", PgOid(0), false, "p", "v", 1, 0, true},
      {"ns_view", "named_schema", PgOid(0), false, "p", "v", 1, 0, true},
  });
  ZETASQL_EXPECT_OK(view_results);
  EXPECT_THAT(*view_results, ExpectedRows(*view_results, expected_view_rows));

  // Assert oids are distinct.
  EXPECT_THAT(Query(R"sql(
      SELECT
        COUNT(DISTINCT oid) = COUNT(1)
      FROM
        pg_catalog.pg_class)sql"),
              IsOkAndHoldsRows({{true}}));

  // Check empty rows are empty.
  EXPECT_THAT(Query(R"sql(
      SELECT
        reltype,
        reloftype,
        relowner,
        relfilenode,
        reltablespace,
        relpages,
        reltuples,
        relallvisible,
        reltoastrelid,
        relisshared,
        relhasrules,
        relhastriggers,
        relhassubclass,
        relrowsecurity,
        relforcerowsecurity,
        relreplident,
        relispartition,
        relrewrite,
        relfrozenxid,
        relminmxid,
        reloptions,
        relpartbound
      FROM
        pg_catalog.pg_class
      GROUP BY
        reltype,
        reloftype,
        relowner,
        relfilenode,
        reltablespace,
        relpages,
        reltuples,
        relallvisible,
        reltoastrelid,
        relisshared,
        relhasrules,
        relhastriggers,
        relhassubclass,
        relrowsecurity,
        relforcerowsecurity,
        relreplident,
        relispartition,
        relrewrite,
        relfrozenxid,
        relminmxid,
        reloptions,
        relpartbound)sql"),
              IsOkAndHoldsRows({{NOid(), NOid(), NOid(), NOid(), NOid(), Ni64(),
                                 Nd(),   Ni64(), NOid(), Nb(),   Nb(),   Nb(),
                                 Nb(),   Nb(),   Nb(),   Ns(),   Nb(),   NOid(),
                                 Ni64(), Ni64(), Ns(),   Ns()}}));
};

TEST_F(PGCatalogTest, PGCollation) {
  auto expected = std::vector<ValueRow>({
      {PgOid(100), "default", "pg_catalog", NOid(), "d", true, -1, Ns(), Ns(),
       Ns(), Ns()},
      {PgOid(950), "C", "pg_catalog", NOid(), "c", true, -1, Ns(), Ns(), Ns(),
       Ns()},
  });

  EXPECT_THAT(Query(R"sql(
      SELECT
        c.oid,
        collname,
        n.nspname,
        collowner,
        collprovider,
        collisdeterministic,
        collencoding,
        collcollate,
        collctype,
        colliculocale,
        collversion
      FROM
        pg_catalog.pg_collation AS c
      JOIN
        pg_catalog.pg_namespace AS n
      ON
        c.collnamespace = n.oid
      ORDER BY
        c.oid)sql"),
              IsOkAndHoldsRows(expected));
}

TEST_F(PGCatalogTest, PGConstraint) {
  auto results = Query(R"sql(
      SELECT
        conname,
        n.nspname,
        contype,
        convalidated,
        cl.relname,
        confupdtype,
        confdeltype,
        conkey
      FROM
        pg_catalog.pg_constraint as c
      JOIN pg_catalog.pg_namespace as n ON c.connamespace = n.oid
      JOIN pg_catalog.pg_class as cl ON c.conrelid = cl.oid
      WHERE contype != 'f'
      ORDER BY
        contype, c.oid)sql");
  EXPECT_THAT(
      *results,
      ExpectedRows(*results,
                   {
                       {"check_constraint_name", "public", "c", true, "base",
                        " ", " ", std::vector<int>{4}},
                       {"CK_base_\\w{16}_1", "public", "c", true, "base", " ",
                        " ", std::vector<int>{4}},
                       {"PK_base", "public", "p", true, "base", " ", " ",
                        std::vector<int>{1, 2}},
                       {"PK_cascade_child", "public", "p", true,
                        "cascade_child", " ", " ", std::vector<int>{1, 2, 3}},
                       {"PK_no_action_child", "public", "p", true,
                        "no_action_child", " ", " ", std::vector<int>{1, 2, 3}},
                       {"PK_row_deletion_policy", "public", "p", true,
                        "row_deletion_policy", " ", " ", std::vector<int>{1}},
                       {"PK_ns_table_1", "named_schema", "p", true,
                        "ns_table_1", " ", " ", std::vector<int>{1}},
                       {"PK_ns_table_2", "named_schema", "p", true,
                        "ns_table_2", " ", " ", std::vector<int>{1}},
                       {"PK_ns_table_1", "named_schema2", "p", true,
                        "ns_table_1", " ", " ", std::vector<int>{1}},
                   }));

  results = Query(R"sql(
      SELECT
        conname,
        n.nspname,
        contype,
        convalidated,
        cl.relname,
        fcl.relname,
        confdeltype,
        conkey,
        confkey
      FROM
        pg_catalog.pg_constraint as c
      JOIN pg_catalog.pg_namespace as n ON c.connamespace = n.oid
      JOIN pg_catalog.pg_class as cl ON c.conrelid = cl.oid
      JOIN pg_catalog.pg_class as fcl ON c.confrelid = fcl.oid
      WHERE contype = 'f'
      ORDER BY
        contype, c.oid)sql");
  EXPECT_THAT(*results,
              ExpectedRows(*results,
                           {
                               {"fk_base_cascade_child", "public", "f", true,
                                "base", "cascade_child", "a",
                                std::vector<int>{3, 2}, std::vector<int>{3, 4}},
                               {"fk_ns_table_2", "named_schema", "f", true,
                                "ns_table_2", "ns_table_1", "a",
                                std::vector<int>{1}, std::vector<int>{1}},
                           }));

  // Check empty columns.
  EXPECT_THAT(
      Query(R"sql(
      SELECT DISTINCT
        condeferrable,
        condeferred,
        contypid,
        conindid,
        conparentid,
        confmatchtype,
        conislocal,
        coninhcount,
        connoinherit,
        conbin
      FROM
        pg_catalog.pg_constraint as c)sql"),
      IsOkAndHoldsRows({
          {Nb(), Nb(), NOid(), NOid(), NOid(), Ns(), Nb(), Ni64(), Nb(), Ns()},
      }));
}

TEST_F(PGCatalogTest, PGIndex) {
  auto expected = std::vector<ValueRow>({
      {"IDX_base_bool_value_key2_N_\\w{16}", "base", 2, 2, false, false,
       std::vector<int>{3, 2}},
      {"PK_base", "base", 2, 2, true, true, std::vector<int>{1, 2}},
      {"IDX_cascade_child_child_key_value1_U_\\w{16}", "cascade_child", 2, 2,
       true, false, std::vector<int>{3, 4}},
      {"PK_cascade_child", "cascade_child", 3, 3, true, true,
       std::vector<int>{1, 2, 3}},
      {"cascade_child_by_value", "cascade_child", 4, 3, true, false,
       std::vector<int>{1, 2, 5, 4}},
      {"PK_no_action_child", "no_action_child", 3, 3, true, true,
       std::vector<int>{1, 2, 3}},
      {"no_action_child_by_value", "no_action_child", 1, 1, false, false,
       std::vector<int>{4}},
      {"PK_ns_table_1", "ns_table_1", 1, 1, true, true, std::vector<int>{1}},
      {"PK_ns_table_1", "ns_table_1", 1, 1, true, true, std::vector<int>{1}},
      {"ns_index", "ns_table_1", 1, 1, true, false, std::vector<int>{1}},
      {"PK_ns_table_2", "ns_table_2", 1, 1, true, true, std::vector<int>{1}},
      {"PK_row_deletion_policy", "row_deletion_policy", 1, 1, true, true,
       std::vector<int>{1}},
  });
  auto results = Query(R"sql(
      SELECT
        c.relname,
        t.relname,
        indnatts,
        indnkeyatts,
        indisunique,
        indisprimary,
        indkey
      FROM
        pg_catalog.pg_index as i
      JOIN pg_catalog.pg_class as c ON i.indexrelid = c.oid
      JOIN pg_catalog.pg_class as t ON i.indrelid = t.oid
      ORDER BY
        t.relname, c.relname)sql");
  EXPECT_THAT(*results, ExpectedRows(*results, expected));

  // Check constant columns.
  EXPECT_THAT(
      Query(R"sql(
      SELECT DISTINCT
        indisexclusion,
        indimmediate,
        indisclustered,
        indisvalid,
        indcheckxmin,
        indisready,
        indislive,
        indisreplident,
        indexprs,
        indpred
      FROM
        pg_catalog.pg_index as c)sql"),
      IsOkAndHoldsRows({
          {false, Nb(), false, true, false, true, true, false, Ns(), Ns()},
      }));
}

TEST_F(PGCatalogTest, PGIndexes) {
  auto results = Query(R"sql(
      SELECT
        schemaname,
        tablename,
        indexname,
        tablespace,
        indexdef
      FROM
        pg_catalog.pg_indexes
      ORDER BY
        schemaname,
        tablename,
        indexname)sql");
  // NOLINTBEGIN
  EXPECT_THAT(
      *results,
      ExpectedRows(
          *results,
          {{"named_schema", "ns_table_1", "PK_ns_table_1", Ns(), Ns()},
           {"named_schema", "ns_table_1", "ns_index", Ns(), Ns()},
           {"named_schema", "ns_table_2", "PK_ns_table_2", Ns(), Ns()},

           {"named_schema2", "ns_table_1", "PK_ns_table_1", Ns(), Ns()},

           {"public", "base", "IDX_base_bool_value_key2_N_\\w{16}", Ns(), Ns()},
           {"public", "base", "PK_base", Ns(), Ns()},
           {"public", "cascade_child",
            "IDX_cascade_child_child_key_value1_U_\\w{16}", Ns(), Ns()},
           {"public", "cascade_child", "PK_cascade_child", Ns(), Ns()},
           {"public", "cascade_child", "cascade_child_by_value", Ns(), Ns()},
           {"public", "no_action_child", "PK_no_action_child", Ns(), Ns()},
           {"public", "no_action_child", "no_action_child_by_value", Ns(),
            Ns()},
           {"public", "row_deletion_policy", "PK_row_deletion_policy", Ns(),
            Ns()}}));
  // NOLINTEND
}

TEST_F(PGCatalogTest, PGNamespace) {
  // Check that the system namespaces have the correct OIDs.
  auto expected =
      std::vector<ValueRow>({{PgOid(11), "pg_catalog", NOid()},
                             {PgOid(2200), "public", NOid()},
                             {PgOid(75003), "information_schema", NOid()},
                             {PgOid(75004), "spanner_sys", NOid()}});
  EXPECT_THAT(Query(R"sql(
      SELECT
        oid,
        nspname,
        nspowner
      FROM
        pg_catalog.pg_namespace
      WHERE oid < 100000
      ORDER BY
        oid)sql"),
              IsOkAndContainsRows(expected));

  expected = std::vector<ValueRow>({
      {"named_schema", NOid()},
      {"named_schema2", NOid()},
  });

  // Check that user namespaces are surfaced.
  EXPECT_THAT(Query(R"sql(
      SELECT
        nspname,
        nspowner
      FROM
        pg_catalog.pg_namespace
      WHERE oid >= 100000
      ORDER BY
        oid)sql"),
              IsOkAndHoldsRows(expected));

  // Check that the user namespaces have unique OIDs.
  ZETASQL_ASSERT_OK_AND_ASSIGN(auto results, Query(R"sql(
      SELECT
        COUNT(DISTINCT oid)
      FROM
        pg_catalog.pg_namespace
      WHERE oid >= 100000
      GROUP BY
        oid)sql"));

  ASSERT_EQ(results.size(), expected.size());
}

TEST_F(PGCatalogTest, PGProc) {
  if (in_prod_env()) {
    GTEST_SKIP();
  }
  auto expected = std::vector<ValueRow>({
      {"read_json_test_stream", "public", PgOid(0), "f", true, 5, 0,
       PgOid(3802),
       std::vector<PgOid>{PgOid(1184), PgOid(1184), PgOid(1043), PgOid(20),
                          PgOid(1015)},
       Ns()},
      {"read_json_test_stream2", "public", PgOid(0), "f", true, 5, 0,
       PgOid(3802),
       std::vector<PgOid>{PgOid(1184), PgOid(1184), PgOid(1043), PgOid(20),
                          PgOid(1015)},
       Ns()},
      {"read_json_test_stream3", "public", PgOid(0), "f", true, 5, 0,
       PgOid(3802),
       std::vector<PgOid>{PgOid(1184), PgOid(1184), PgOid(1043), PgOid(20),
                          PgOid(1015)},
       Ns()},
      {"read_json_test_stream4", "public", PgOid(0), "f", true, 5, 0,
       PgOid(3802),
       std::vector<PgOid>{PgOid(1184), PgOid(1184), PgOid(1043), PgOid(20),
                          PgOid(1015)},
       Ns()},
  });
  EXPECT_THAT(Query(R"sql(
      SELECT
        p.proname,
        n.nspname,
        provariadic,
        prokind,
        proretset,
        pronargs,
        pronargdefaults,
        prorettype,
        proargtypes,
        prosqlbody
      FROM
        pg_catalog.pg_proc as p
      LEFT JOIN pg_catalog.pg_namespace as n on n.oid = p.pronamespace
      WHERE n.nspname != 'pg_catalog' AND n.nspname != 'spanner'
      ORDER BY
        p.oid)sql"),
              IsOkAndHoldsRows(expected));
}

TEST_F(PGCatalogTest, PGSequence) {
  auto expected = std::vector<ValueRow>({
      {"test_sequence", 20, 1234, Ni64(), Ni64(), Ni64(), 1000, false},
  });
  EXPECT_THAT(Query(R"sql(
      SELECT
        c.relname,
        seqtypid,
        seqstart,
        seqincrement,
        seqmax,
        seqmin,
        seqcache,
        seqcycle
      FROM
        pg_catalog.pg_sequence as s
      JOIN pg_catalog.pg_class as c on s.seqrelid = c.oid)sql"),
              IsOkAndHoldsRows(expected));
}

TEST_F(PGCatalogTest, PGSequences) {
  auto expected = std::vector<ValueRow>({
      {"public", "test_sequence", Ns(), 1234, Ni64(), Ni64(), Ni64(), false,
       1000, Ni64()},
  });
  EXPECT_THAT(Query(R"sql(
      SELECT
        schemaname,
        sequencename,
        sequenceowner,
        start_value,
        min_value,
        max_value,
        increment_by,
        cycle,
        cache_size,
        last_value
      FROM
        pg_catalog.pg_sequences
      ORDER BY
        schemaname)sql"),
              IsOkAndHoldsRows({expected}));
}

TEST_F(PGCatalogTest, PGSettings) {
  auto expected = std::vector<ValueRow>({
      {"max_index_keys", "16", "Preset Options",
       "Shows the maximum number of index keys.", "internal", "integer",
       "default", "16", "16", "16", "16", false},  // NOLINT
  });
  EXPECT_THAT(Query(R"sql(
      SELECT
        name,
        setting,
        category,
        short_desc,
        context,
        vartype,
        source,
        min_val,
        max_val,
        boot_val,
        reset_val,
        pending_restart
      FROM
        pg_catalog.pg_settings)sql"),
              IsOkAndHoldsRows(expected));
}

TEST_F(PGCatalogTest, PGTables) {
  auto expected = std::vector<ValueRow>({
      {"named_schema", "ns_table_1", Ns(), Ns(), true, Nb(), Nb(), Nb()},
      {"named_schema", "ns_table_2", Ns(), Ns(), false, Nb(), Nb(), Nb()},

      {"named_schema2", "ns_table_1", Ns(), Ns(), false, Nb(), Nb(), Nb()},

      {"public", "base", Ns(), Ns(), true, Nb(), Nb(), Nb()},
      {"public", "cascade_child", Ns(), Ns(), true, Nb(), Nb(), Nb()},
      {"public", "no_action_child", Ns(), Ns(), true, Nb(), Nb(), Nb()},
      {"public", "row_deletion_policy", Ns(), Ns(), false, Nb(), Nb(), Nb()},
  });
  EXPECT_THAT(Query(R"sql(
      SELECT
        schemaname,
        tablename,
        tableowner,
        tablespace,
        hasindexes,
        hasrules,
        hastriggers,
        rowsecurity
      FROM
        pg_catalog.pg_tables
      WHERE
        schemaname != 'pg_catalog' AND schemaname != 'information_schema' AND
        schemaname != 'spanner_sys'
      ORDER BY
        schemaname, tablename)sql"),
              IsOkAndHoldsRows({expected}));
}

TEST_F(PGCatalogTest, PGType) {
  auto expected = std::vector<ValueRow>({
      // Array Types.
      {PgOid(1000), "_bool",  PgOid(11), NOid(), -1,   false,
       "b",         "A",      false,     true,   ",",  PgOid(0),
       PgOid(16),   PgOid(0), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),   NOid(),    Ns(),   Ns()},
      {PgOid(1001), "_bytea", PgOid(11), NOid(), -1,   false,
       "b",         "A",      false,     true,   ",",  PgOid(0),
       PgOid(17),   PgOid(0), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),   NOid(),    Ns(),   Ns()},
      {PgOid(1182), "_date",  PgOid(11), NOid(), -1,   false,
       "b",         "A",      false,     true,   ",",  PgOid(0),
       PgOid(1082), PgOid(0), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),   NOid(),    Ns(),   Ns()},
      {PgOid(1021), "_float4", PgOid(11), NOid(), -1,   false,
       "b",         "A",       false,     true,   ",",  PgOid(0),
       PgOid(700),  PgOid(0),  Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),    NOid(),    Ns(),   Ns()},
      {PgOid(1022), "_float8", PgOid(11), NOid(), -1,   false,
       "b",         "A",       false,     true,   ",",  PgOid(0),
       PgOid(701),  PgOid(0),  Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),    NOid(),    Ns(),   Ns()},
      {PgOid(1016), "_int8",  PgOid(11), NOid(), -1,   false,
       "b",         "A",      false,     true,   ",",  PgOid(0),
       PgOid(20),   PgOid(0), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),   NOid(),    Ns(),   Ns()},
      {PgOid(3807), "_jsonb", PgOid(11), NOid(), -1,   false,
       "b",         "A",      false,     true,   ",",  PgOid(0),
       PgOid(3802), PgOid(0), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),   NOid(),    Ns(),   Ns()},
      {PgOid(1231), "_numeric", PgOid(11), NOid(), -1,   false,
       "b",         "A",        false,     true,   ",",  PgOid(0),
       PgOid(1700), PgOid(0),   Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),     NOid(),    Ns(),   Ns()},
      {PgOid(1028), "_oid",   PgOid(11), NOid(), -1,   false,
       "b",         "A",      false,     true,   ",",  PgOid(0),
       PgOid(26),   PgOid(0), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),   NOid(),    Ns(),   Ns()},
      {PgOid(1009), "_text",  PgOid(11), NOid(), -1,   false,
       "b",         "A",      false,     true,   ",",  PgOid(0),
       PgOid(25),   PgOid(0), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),   NOid(),    Ns(),   Ns()},
      {PgOid(1185), "_timestamptz", PgOid(11), NOid(),
       -1,          false,          "b",       "A",
       false,       true,           ",",       PgOid(0),
       PgOid(1184), PgOid(0),       Ns(),      Ns(),
       Nb(),        NOid(),         Ni64(),    Ni64(),
       NOid(),      Ns(),           Ns()},
      {PgOid(1015), "_varchar", PgOid(11), NOid(), -1,   false,
       "b",         "A",        false,     true,   ",",  PgOid(0),
       PgOid(1043), PgOid(0),   Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),     NOid(),    Ns(),   Ns()},

      // Pseudotypes.
      {PgOid(2276), "any",  PgOid(11), NOid(),   4,        true,     "p",  "P",
       false,       true,   ",",       PgOid(0), PgOid(0), PgOid(0), Ns(), Ns(),
       Nb(),        NOid(), Ni64(),    Ni64(),   NOid(),   Ns(),     Ns()},
      {PgOid(2277), "anyarray", PgOid(11), NOid(), -1,   false,
       "p",         "P",        false,     true,   ",",  PgOid(0),
       PgOid(0),    PgOid(0),   Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),     NOid(),    Ns(),   Ns()},
      {PgOid(5078), "anycompatiblearray",
       PgOid(11),   NOid(),
       -1,          false,
       "p",         "P",
       false,       true,
       ",",         PgOid(0),
       PgOid(0),    PgOid(0),
       Ns(),        Ns(),
       Nb(),        NOid(),
       Ni64(),      Ni64(),
       NOid(),      Ns(),
       Ns()},
      {PgOid(2283), "anyelement", PgOid(11), NOid(), 4,    true,
       "p",         "P",          false,     true,   ",",  PgOid(0),
       PgOid(0),    PgOid(0),     Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),       NOid(),    Ns(),   Ns()},
      {PgOid(2776), "anynonarray", PgOid(11), NOid(), 4,    true,
       "p",         "P",           false,     true,   ",",  PgOid(0),
       PgOid(0),    PgOid(0),      Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),        NOid(),    Ns(),   Ns()},

      // Base Types.
      {PgOid(16), "bool",      PgOid(11), NOid(), 1,    true,
       "b",       "B",         true,      true,   ",",  PgOid(0),
       PgOid(0),  PgOid(1000), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),    Ni64(),      NOid(),    Ns(),   Ns()},
      {PgOid(17), "bytea",     PgOid(11), NOid(), -1,   false,
       "b",       "U",         false,     true,   ",",  PgOid(0),
       PgOid(0),  PgOid(1001), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),    Ni64(),      NOid(),    Ns(),   Ns()},
      {PgOid(1082), "date",      PgOid(11), NOid(), 4,    true,
       "b",         "D",         false,     true,   ",",  PgOid(0),
       PgOid(0),    PgOid(1182), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),      NOid(),    Ns(),   Ns()},
      {PgOid(700), "float4",    PgOid(11), NOid(), 4,    true,
       "b",        "N",         false,     true,   ",",  PgOid(0),
       PgOid(0),   PgOid(1021), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),     Ni64(),      NOid(),    Ns(),   Ns()},
      {PgOid(701), "float8",    PgOid(11), NOid(), 8,    true,
       "b",        "N",         true,      true,   ",",  PgOid(0),
       PgOid(0),   PgOid(1022), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),     Ni64(),      NOid(),    Ns(),   Ns()},
      {PgOid(20), "int8",      PgOid(11), NOid(), 8,    true,
       "b",       "N",         false,     true,   ",",  PgOid(0),
       PgOid(0),  PgOid(1016), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),    Ni64(),      NOid(),    Ns(),   Ns()},
      {PgOid(3802), "jsonb",     PgOid(11), NOid(), -1,   false,
       "b",         "U",         false,     true,   ",",  PgOid(0),
       PgOid(0),    PgOid(3807), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),      NOid(),    Ns(),   Ns()},
      {PgOid(1700), "numeric",   PgOid(11), NOid(), -1,   false,
       "b",         "N",         false,     true,   ",",  PgOid(0),
       PgOid(0),    PgOid(1231), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),      NOid(),    Ns(),   Ns()},
      {PgOid(26), "oid",       PgOid(11), NOid(), 4,    true,
       "b",       "N",         true,      true,   ",",  PgOid(0),
       PgOid(0),  PgOid(1028), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),    Ni64(),      NOid(),    Ns(),   Ns()},
      {PgOid(25), "text",      PgOid(11), NOid(), -1,   false,
       "b",       "S",         true,      true,   ",",  PgOid(0),
       PgOid(0),  PgOid(1009), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),    Ni64(),      NOid(),    Ns(),   Ns()},
      {PgOid(1184), "timestamptz", PgOid(11), NOid(), 8,    true,
       "b",         "D",           true,      true,   ",",  PgOid(0),
       PgOid(0),    PgOid(1185),   Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),        NOid(),    Ns(),   Ns()},
      {PgOid(1043), "varchar",   PgOid(11), NOid(), -1,   false,
       "b",         "S",         false,     true,   ",",  PgOid(0),
       PgOid(0),    PgOid(1015), Ns(),      Ns(),   Nb(), NOid(),
       Ni64(),      Ni64(),      NOid(),    Ns(),   Ns()},
  });
  EXPECT_THAT(Query(R"sql(
      SELECT
        oid,
        typname,
        typnamespace,
        typowner,
        typlen,
        typbyval,
        typtype,
        typcategory,
        typispreferred,
        typisdefined,
        typdelim,
        typrelid,
        typelem,
        typarray,
        typalign,
        typstorage,
        typnotnull,
        typbasetype,
        typtypmod,
        typndims,
        typcollation,
        typdefaultbin,
        typdefault
      FROM
        pg_catalog.pg_type
      ORDER BY
        typname)sql"),
              IsOkAndContainsRows({expected}));
}

TEST_F(PGCatalogTest, PGViews) {
  auto expected = std::vector<ValueRow>({
      {"public", "base_view", Ns(), "SELECT key1 FROM base"},
      {"named_schema", "ns_view", Ns(),
       "SELECT key1 FROM named_schema.ns_table_1 t"},
  });
  EXPECT_THAT(Query(
                  R"sql(
      SELECT
        schemaname, viewname, viewowner, definition
      FROM
        pg_catalog.pg_views
      WHERE
        schemaname != 'pg_catalog' AND schemaname != 'information_schema' AND
        schemaname != 'spanner_sys'
      ORDER BY
        definition, schemaname, viewname)sql"),
              IsOkAndHoldsRows(expected));
}

TEST_F(PGCatalogTest, SupportedButEmptyTables) {
  for (const auto& table_name : *kSupportedButEmptyTables) {
    EXPECT_THAT(Query(absl::Substitute(R"sql(SELECT * FROM pg_catalog.$0)sql",
                                       table_name)),
                IsOkAndHoldsRows({}));
  }
}

}  // namespace

}  // namespace test
}  // namespace emulator
}  // namespace spanner
}  // namespace google

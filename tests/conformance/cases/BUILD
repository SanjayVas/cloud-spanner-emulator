#
# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

package(
    default_visibility = ["//:__subpackages__"],
)

licenses(["notice"])

cc_library(
    name = "cases",
    testonly = 1,
    srcs = [
        "ann_test.cc",
        "arrays.cc",
        "batch_dml.cc",
        "batch_write.cc",
        "case_sensitivity.cc",
        "change_streams_exclusion.cc",
        "change_streams_read_write.cc",
        "check_constraint_read_write.cc",
        "column_constraints.cc",
        "column_default_value_read_write.cc",
        "commit_timestamps.cc",
        "default_time_zone.cc",
        "delete_range.cc",
        "dml.cc",
        "dml_replay.cc",
        "foreign_key_actions.cc",
        "foreign_key_transactions.cc",
        "foreign_key_verifiers.cc",
        "generated_column_read_write.cc",
        "index_backfill.cc",
        "index_read_write.cc",
        "information_schema.cc",
        "interleaving.cc",
        "large_reads.cc",
        "limits.cc",
        "malformed_writes.cc",
        "multi_row_writes.cc",
        "named_schema.cc",
        "operations.cc",
        "parameters.cc",
        "partition_query.cc",
        "partition_reads.cc",
        "partitioned_dml.cc",
        "pg_catalog_test.cc",
        "pg_change_streams_read_write.cc",
        "pg_functions_test.cc",
        "pg_synonyms.cc",
        "pg_views_test.cc",
        "primary_keys.cc",
        "query.cc",
        "query_hints.cc",
        "query_modes.cc",
        "range_delete.cc",
        "range_reads.cc",
        "read_your_writes.cc",
        "schema_changes.cc",
        "search_test.cc",
        "select_for_update.cc",
        "sequence_read_write.cc",
        "sessions.cc",
        "single_row_writes.cc",
        "singleton_tables.cc",
        "snapshot_reads.cc",
        "spanner_sys.cc",
        "tablesample.cc",
        "thread_safety.cc",
        "transaction_errors.cc",
        "transactions.cc",
        "udfs.cc",
        "values.cc",
        "views.cc",
    ],
    data = [
        "//tests/conformance/data/schema_changes:test_files",
        "//tests/conformance/data/schemas:schema_files",
    ],
    deps = [
        "//backend/schema/catalog:property_graph_cc_proto",
        "//common:clock",
        "//common:config",
        "//common:errors",
        "//common:feature_flags",
        "//frontend/converters:pg_change_streams",
        "//frontend/handlers:change_streams",
        "//tests/common:change_streams",
        "//tests/common:chunking",
        "//tests/common:file_based_test_runner",
        "//tests/common:proto_matchers",
        "//tests/common:scoped_feature_flags_setter",
        "//tests/conformance/common:database_test_base",
        "//tests/conformance/common:environment",
        "//tests/conformance/common:query_translator",
        "//third_party/spanner_pg/datatypes/common:numeric_core",
        "@com_github_googleapis_google_cloud_cpp//:common",
        "@com_github_googleapis_google_cloud_cpp//:spanner",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_set",  # buildcleaner: keep
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_googleapis//google/spanner/admin/database/v1:database_cc_proto",
        "@com_google_googleapis//google/spanner/v1:spanner_cc_grpc",
        "@com_google_googleapis//google/spanner/v1:spanner_cc_proto",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:cc_wkt_protos",
        "@com_google_protobuf//:protobuf",
        "@com_google_zetasql//zetasql/base:no_destructor",
        "@com_google_zetasql//zetasql/base:ret_check",
        "@com_googlesource_code_re2//:re2",
        "@nlohmann_json//:json",
    ],
    alwayslink = 1,
)

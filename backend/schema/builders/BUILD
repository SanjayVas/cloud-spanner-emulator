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
    name = "schema_builders",
    hdrs = [
        "change_stream_builder.h",
        "check_constraint_builder.h",
        "column_builder.h",
        "database_options_builder.h",
        "foreign_key_builder.h",
        "index_builder.h",
        "locality_group_builder.h",
        "model_builder.h",
        "named_schema_builder.h",
        "placement_builder.h",
        "property_graph_builder.h",
        "sequence_builder.h",
        "table_builder.h",
        "udf_builder.h",
        "view_builder.h",
    ],
    deps = [
        "//backend/common:ids",
        "//backend/schema/catalog:schema",
        "//backend/schema/ddl:operations_cc_proto",
        "//backend/schema/graph:schema_node",
        "//backend/schema/parser:ddl_parser",
        "//backend/schema/updater:schema_validation_context",
        "//backend/schema/validators:schema_validators",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_zetasql//zetasql/public:function",
        "@com_google_zetasql//zetasql/public:type",
    ],
)

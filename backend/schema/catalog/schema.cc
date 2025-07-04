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

#include "backend/schema/catalog/schema.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "zetasql/public/types/type.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "backend/schema/catalog/change_stream.h"
#include "backend/schema/catalog/check_constraint.h"
#include "backend/schema/catalog/column.h"
#include "backend/schema/catalog/database_options.h"
#include "backend/schema/catalog/foreign_key.h"
#include "backend/schema/catalog/index.h"
#include "backend/schema/catalog/locality_group.h"
#include "backend/schema/catalog/model.h"
#include "backend/schema/catalog/named_schema.h"
#include "backend/schema/catalog/placement.h"
#include "backend/schema/catalog/property_graph.h"
#include "backend/schema/catalog/proto_bundle.h"
#include "backend/schema/catalog/sequence.h"
#include "backend/schema/catalog/table.h"
#include "backend/schema/catalog/udf.h"
#include "backend/schema/catalog/view.h"
#include "backend/schema/ddl/operations.pb.h"
#include "backend/schema/graph/schema_graph.h"
#include "backend/schema/graph/schema_node.h"
#include "backend/schema/parser/ddl_parser.h"
#include "backend/schema/updater/ddl_type_conversion.h"
#include "common/constants.h"
#include "re2/re2.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

const char kManagedIndexNonFingerprintRegex[] = "(IDX_\\w+_)[0-9A-F]{16}";
const int kFingerprintLength = 16;

const View* Schema::FindView(const std::string& view_name) const {
  auto itr = views_map_.find(view_name);
  if (itr == views_map_.end()) {
    return nullptr;
  }
  return itr->second;
}

const View* Schema::FindViewCaseSensitive(const std::string& view_name) const {
  auto view = FindView(view_name);
  if (!view || view->Name() != view_name) {
    return nullptr;
  }
  return view;
}

const Udf* Schema::FindUdf(const std::string& udf_name) const {
  auto itr = udfs_map_.find(udf_name);
  if (itr == udfs_map_.end()) {
    return nullptr;
  }
  return itr->second;
}

const Udf* Schema::FindUdfCaseSensitive(const std::string& udf_name) const {
  auto udf = FindUdf(udf_name);
  if (!udf || udf->Name() != udf_name) {
    return nullptr;
  }
  return udf;
}

const Table* Schema::FindTable(const std::string& table_name) const {
  auto itr = tables_map_.find(table_name);
  if (itr == tables_map_.end()) {
    // Fall back to synonyms.
    return FindTableUsingSynonym(table_name);
  }
  return itr->second;
}

const Table* Schema::FindTableCaseSensitive(
    const std::string& table_name) const {
  auto table = FindTable(table_name);
  if (!table || table->Name() != table_name) {
    // Fall back to synonyms.
    return FindTableUsingSynonymCaseSensitive(table_name);
  }
  return table;
}

const Table* Schema::FindTableUsingSynonym(
    const std::string& table_synonym) const {
  auto itr = synonyms_map_.find(table_synonym);
  if (itr == synonyms_map_.end()) {
    return nullptr;
  }
  return itr->second;
}

const Table* Schema::FindTableUsingSynonymCaseSensitive(
    const std::string& table_synonym) const {
  auto table = FindTableUsingSynonym(table_synonym);
  if (!table || table->synonym() != table_synonym) {
    return nullptr;
  }
  return table;
}

const Index* Schema::FindIndex(const std::string& index_name) const {
  auto itr = index_map_.find(index_name);
  if (itr == index_map_.end()) {
    return this->FindManagedIndex(index_name);
  }
  return itr->second;
}

const Index* Schema::FindIndexCaseSensitive(
    const std::string& index_name) const {
  auto index = FindIndex(index_name);
  if (!index || index->Name() != index_name) {
    return nullptr;
  }
  return index;
}

std::vector<const Index*> Schema::FindIndexesUnderName(
    const std::string& index_name) const {
  std::vector<const Index*> indexes;
  auto index = FindIndex(index_name);
  if (index != nullptr) {
    indexes.push_back(index);
  }
  for (const auto& named_schema : named_schemas()) {
    index = FindIndex(named_schema->Name() + "." + index_name);
    if (index != nullptr) {
      indexes.push_back(index);
    }
  }
  return indexes;
}

const ChangeStream* Schema::FindChangeStream(
    const std::string& change_stream_name) const {
  auto itr = change_streams_map_.find(change_stream_name);
  if (itr == change_streams_map_.end()) {
    return nullptr;
  }
  return itr->second;
}

const Placement* Schema::FindPlacement(
    const std::string& placement_name) const {
  auto itr = placements_map_.find(placement_name);
  if (itr == placements_map_.end()) {
    return nullptr;
  }
  return itr->second;
}

const Sequence* Schema::FindSequence(const std::string& sequence_name,
                                     bool exclude_internal) const {
  auto itr = sequences_map_.find(sequence_name);
  if (itr == sequences_map_.end()) {
    return nullptr;
  }
  if (exclude_internal && itr->second->is_internal_use()) {
    return nullptr;
  }
  return itr->second;
}

const Model* Schema::FindModel(const std::string& model_name) const {
  auto itr = models_map_.find(model_name);
  if (itr == models_map_.end()) {
    return nullptr;
  }
  return itr->second;
}

const PropertyGraph* Schema::FindPropertyGraph(
    const std::string& graph_name) const {
  auto itr = property_graphs_map_.find(graph_name);
  if (itr == property_graphs_map_.end()) {
    return nullptr;
  }
  return itr->second;
}

const NamedSchema* Schema::FindNamedSchema(
    const std::string& named_schema_name) const {
  auto itr = named_schemas_map_.find(named_schema_name);

  if (itr == named_schemas_map_.end()) {
    return nullptr;
  }
  return itr->second;
}

const Index* Schema::FindManagedIndex(const std::string& index_name) const {
  // Check that the index_name matches the format of managed index names, and
  // extract the non-fingerprint part of the index.
  std::string non_fingerprint_index_name;
  if (!RE2::FullMatch(index_name, kManagedIndexNonFingerprintRegex,
                      &non_fingerprint_index_name)) {
    return nullptr;
  }

  for (const auto& itr : index_map_) {
    if (itr.second->is_managed() &&
        itr.first.length() ==
            non_fingerprint_index_name.length() + kFingerprintLength &&
        itr.first.find(non_fingerprint_index_name) != std::string::npos) {
      return itr.second;
    }
  }
  return nullptr;
}

const LocalityGroup* Schema::FindLocalityGroup(
    const std::string& locality_group_name) const {
  auto itr = locality_groups_map_.find(locality_group_name);
  if (itr == locality_groups_map_.end()) {
    return nullptr;
  }
  return itr->second;
}

ddl::ForeignKey::Action FindForeignKeyOnDeleteAction(const ForeignKey* fk) {
  return fk->on_delete_action() == ForeignKey::Action::kCascade
             ? ddl::ForeignKey::CASCADE
             : ddl::ForeignKey::NO_ACTION;
}

void DumpIndex(const Index* index, ddl::CreateIndex& create_index) {
  ABSL_CHECK_NE(index, nullptr);  // Crash OK
  create_index.set_index_name(index->Name());
  create_index.set_index_base_name(index->indexed_table()->Name());
  create_index.set_unique(index->is_unique());
  if (index->parent() != nullptr) {
    create_index.set_interleave_in_table(index->parent()->Name());
  }
  for (const KeyColumn* key_column : index->key_columns()) {
    ddl::KeyPartClause* key_part_clause = create_index.add_key();
    key_part_clause->set_key_name(key_column->column()->Name());
    if (!key_column->is_descending() && key_column->is_nulls_last()) {
      key_part_clause->set_order(ddl::KeyPartClause::ASC_NULLS_LAST);
    } else if (key_column->is_descending() && !key_column->is_nulls_last()) {
      key_part_clause->set_order(ddl::KeyPartClause::DESC_NULLS_FIRST);
    } else {
      key_part_clause->set_order(key_column->is_descending()
                                     ? ddl::KeyPartClause::DESC
                                     : ddl::KeyPartClause::ASC);
    }
  }
  for (const Column* stored_column : index->stored_columns()) {
    ddl::StoredColumnDefinition* stored_column_def =
        create_index.add_stored_column_definition();
    stored_column_def->set_name(stored_column->Name());
  }
}

template <typename ColumnDef>
void SetColumnExpression(const Column* column, ColumnDef& column_def) {
  if (column->expression().has_value()) {
    column_def.set_expression(*column->expression());
    if (column->original_expression().has_value()) {
      column_def.mutable_expression_origin()->set_original_expression(
          *column->original_expression());
    }
  }
}

void DumpColumn(const Column* column, ddl::ColumnDefinition& column_def) {
  ABSL_CHECK_NE(column, nullptr);  // Crash OK
  column_def.set_column_name(column->Name());
  const zetasql::Type* column_type = column->GetType();
  if (column_type != nullptr) {
    ddl::ColumnDefinition type_column_def =
        GoogleSqlTypeToDDLColumnType(column_type);
    column_def.set_type(type_column_def.type());
    if (column_type->IsArray()) {
      *column_def.mutable_array_subtype() = type_column_def.array_subtype();
    }
  }
  if (column->declared_max_length().has_value()) {
    if (column_type->IsArray()) {
      column_def.mutable_array_subtype()->set_length(
          *column->declared_max_length());
    } else {
      column_def.set_length(*column->declared_max_length());
    }
  }
  column_def.set_not_null(!column->is_nullable());
  if (column->is_placement_key()) {
    column_def.set_placement_key(true);
  }
  if (column->allows_commit_timestamp()) {
    ddl::SetOption* set_option = column_def.add_set_options();
    set_option->set_option_name(ddl::kPGCommitTimestampOptionName);
    set_option->set_bool_value(true);
  }
  if (column->is_identity_column()) {
    ddl::ColumnDefinition::IdentityColumnDefinition* identity_column =
        column_def.mutable_identity_column();
    ABSL_CHECK_EQ(column->sequences_used().size(), 1);  // Crash OK
    const Sequence* sequence =
        static_cast<const Sequence*>(column->sequences_used().at(0));
    if (!sequence->use_default_sequence_kind_option()) {
      if (sequence->sequence_kind() == Sequence::BIT_REVERSED_POSITIVE) {
        identity_column->set_type(
            ddl::ColumnDefinition::IdentityColumnDefinition::
                BIT_REVERSED_POSITIVE);
      }
    }
    if (sequence->start_with_counter().has_value()) {
      identity_column->set_start_with_counter(
          sequence->start_with_counter().value());
    }
    if (sequence->skip_range_min().has_value()) {
      identity_column->set_skip_range_min(sequence->skip_range_min().value());
      identity_column->set_skip_range_max(sequence->skip_range_max().value());
    }
  } else if (column->has_default_value()) {
    SetColumnExpression(column, *column_def.mutable_column_default());
  }
  if (column->is_generated()) {
    ddl::ColumnDefinition_GeneratedColumnDefinition* generated_column =
        column_def.mutable_generated_column();
    // Non-stored generated columns are not supported.
    generated_column->set_stored(true);
    SetColumnExpression(column, *generated_column);
  }
}

void DumpForeignKey(const ForeignKey* foreign_key,
                    ddl::ForeignKey& foreign_key_def) {
  ABSL_CHECK_NE(foreign_key, nullptr);  // Crash OK
  foreign_key_def.set_enforced(foreign_key->enforced());
  if (!foreign_key->constraint_name().empty()) {
    // Do not set constraint name when it is a generated name.
    foreign_key_def.set_constraint_name(foreign_key->Name());
  }
  foreign_key_def.set_referenced_table_name(
      foreign_key->referenced_table()->Name());
  for (const Column* column : foreign_key->referencing_columns()) {
    foreign_key_def.add_constrained_column_name(column->Name());
  }
  for (const Column* column : foreign_key->referenced_columns()) {
    foreign_key_def.add_referenced_column_name(column->Name());
  }
  if (foreign_key->on_delete_action() !=
      ForeignKey::Action::kActionUnspecified) {
    foreign_key_def.set_on_delete(FindForeignKeyOnDeleteAction(foreign_key));
  }
}

void DumpInterleaveClause(const Table* table,
                          ddl::InterleaveClause& interleave_clause) {
  ABSL_CHECK_NE(table, nullptr);  // Crash OK
  interleave_clause.set_table_name(table->parent()->Name());
  interleave_clause.set_on_delete(table->on_delete_action() ==
                                          Table::OnDeleteAction::kCascade
                                      ? ddl::InterleaveClause::CASCADE
                                      : ddl::InterleaveClause::NO_ACTION);
}

void DumpCheckConstraint(const CheckConstraint* check_constraint,
                         ddl::CheckConstraint& check_constraint_def) {
  check_constraint_def.set_enforced(true);
  if (!check_constraint->has_generated_name()) {
    check_constraint_def.set_name(check_constraint->Name());
  }
  check_constraint_def.set_expression(check_constraint->expression());
  if (check_constraint->original_expression().has_value()) {
    check_constraint_def.mutable_expression_origin()->set_original_expression(
        *check_constraint->original_expression());
  }
}

void DumpChangeStream(const ChangeStream* change_stream,
                      ddl::CreateChangeStream& create_change_stream) {
  create_change_stream.set_change_stream_name(change_stream->Name());
  if (change_stream->for_clause() != nullptr) {
    ddl::ChangeStreamForClause* for_clause =
        create_change_stream.mutable_for_clause();
    *for_clause = *change_stream->for_clause();
  }
  if (change_stream->value_capture_type().has_value()) {
    ddl::SetOption* set_option = create_change_stream.add_set_options();
    set_option->set_option_name(ddl::kChangeStreamValueCaptureTypeOptionName);
    set_option->set_string_value(*change_stream->value_capture_type());
  }
  if (change_stream->retention_period().has_value()) {
    ddl::SetOption* set_option = create_change_stream.add_set_options();
    set_option->set_option_name(ddl::kChangeStreamRetentionPeriodOptionName);
    set_option->set_string_value(*change_stream->retention_period());
  }
  if (change_stream->exclude_insert().has_value()) {
    ddl::SetOption* set_option = create_change_stream.add_set_options();
    set_option->set_option_name(ddl::kChangeStreamExcludeInsertOptionName);
    set_option->set_bool_value(*change_stream->exclude_insert());
  }
  if (change_stream->exclude_update().has_value()) {
    ddl::SetOption* set_option = create_change_stream.add_set_options();
    set_option->set_option_name(ddl::kChangeStreamExcludeUpdateOptionName);
    set_option->set_bool_value(*change_stream->exclude_update());
  }
  if (change_stream->exclude_delete().has_value()) {
    ddl::SetOption* set_option = create_change_stream.add_set_options();
    set_option->set_option_name(ddl::kChangeStreamExcludeDeleteOptionName);
    set_option->set_bool_value(*change_stream->exclude_delete());
  }
  if (change_stream->exclude_ttl_deletes().has_value()) {
    ddl::SetOption* set_option = create_change_stream.add_set_options();
    set_option->set_option_name(ddl::kChangeStreamExcludeTtlDeletesOptionName);
    set_option->set_bool_value(*change_stream->exclude_ttl_deletes());
  }
}

void DumpSequence(const Sequence* sequence,
                  ddl::CreateSequence& create_sequence) {
  create_sequence.set_sequence_name(sequence->Name());
  if (sequence->sequence_kind() != Sequence::BIT_REVERSED_POSITIVE) {
    return;
  }
  ddl::SetOption* set_option = create_sequence.add_set_options();
  set_option->set_option_name(kSequenceKindOptionName);
  set_option->set_string_value(kSequenceKindBitReversedPositive);

  if (sequence->start_with_counter().has_value()) {
    set_option = create_sequence.add_set_options();
    set_option->set_option_name(kSequenceStartWithCounterOptionName);
    set_option->set_int64_value(sequence->start_with_counter().value());
  }
  if (sequence->skip_range_min().has_value()) {
    set_option = create_sequence.add_set_options();
    set_option->set_option_name(kSequenceSkipRangeMinOptionName);
    set_option->set_int64_value(sequence->skip_range_min().value());
  }
  if (sequence->skip_range_max().has_value()) {
    set_option = create_sequence.add_set_options();
    set_option->set_option_name(kSequenceSkipRangeMaxOptionName);
    set_option->set_int64_value(sequence->skip_range_max().value());
  }
}

void DumpNamedSchema(const NamedSchema* named_schema,
                     ddl::CreateSchema& create_schema) {
  create_schema.set_schema_name(named_schema->Name());
}

void DumpModelColumn(const Model::ModelColumn& model_column,
                     ddl::ColumnDefinition& column_definition) {
  column_definition = GoogleSqlTypeToDDLColumnType(model_column.type);
  column_definition.set_column_name(model_column.name);

  if (model_column.is_required.has_value()) {
    ddl::SetOption* required = column_definition.add_set_options();
    required->set_option_name(ddl::kModelColumnRequiredOptionName);
    required->set_bool_value(*model_column.is_required);
  }
}

void DumpModel(const Model* model, ddl::CreateModel& create_model) {
  create_model.set_model_name(model->Name());
  create_model.set_remote(model->is_remote());

  for (const Model::ModelColumn& input : model->input()) {
    DumpModelColumn(input, *create_model.add_input());
  }
  for (const Model::ModelColumn& output : model->output()) {
    DumpModelColumn(output, *create_model.add_output());
  }

  if (model->endpoint().has_value()) {
    ddl::SetOption* endpoint = create_model.add_set_options();
    endpoint->set_option_name(ddl::kModelEndpointOptionName);
    endpoint->set_string_value(*model->endpoint());
  }

  if (!model->endpoints().empty()) {
    ddl::SetOption* endpoints = create_model.add_set_options();
    endpoints->set_option_name(ddl::kModelEndpointsOptionName);

    for (const std::string& endpoint : model->endpoints()) {
      endpoints->add_string_list_value(endpoint);
    }
  }

  if (model->default_batch_size().has_value()) {
    ddl::SetOption* default_batch = create_model.add_set_options();
    default_batch->set_option_name(ddl::kModelDefaultBatchSizeOptionName);
    default_batch->set_int64_value(*model->default_batch_size());
  }
}

void DumpPropertyGraph(const PropertyGraph* graph,
                       ddl::CreatePropertyGraph& create_property_graph) {
  create_property_graph.set_name(graph->Name());
  create_property_graph.set_ddl_body(graph->DdlBody());
}

void DumpDatabaseOptions(const DatabaseOptions* database_option,
                         ddl::AlterDatabase& alter_database) {
  alter_database.set_db_name(database_option->Name());
  for (const auto& option : database_option->options()) {
    ddl::SetOption* set_option =
        alter_database.mutable_set_options()->add_options();
    set_option->set_option_name(option.option_name());
    set_option->set_string_value(option.string_value());
  }
}

void DumpLocalityGroup(const LocalityGroup* locality_group,
                       ddl::CreateLocalityGroup& create_locality_group) {
  create_locality_group.set_locality_group_name(locality_group->Name());
  for (const auto& option : locality_group->options()) {
    if (option.option_name() == ddl::kInternalLocalityGroupStorageOptionName) {
      ddl::SetOption* set_option = create_locality_group.add_set_options();
      set_option->set_option_name(ddl::kLocalityGroupStorageOptionName);
      if (option.has_bool_value()) {
        set_option->set_string_value(
            option.bool_value() ? ddl::kLocalityGroupStorageOptionSSDVal
                                : ddl::kLocalityGroupStorageOptionHDDVal);
      }
    } else if (option.option_name() ==
               ddl::kInternalLocalityGroupSpillTimeSpanOptionName) {
      for (const auto& time_span : option.string_list_value()) {
        ddl::SetOption* set_option = create_locality_group.add_set_options();
        set_option->set_option_name(ddl::kLocalityGroupSpillTimeSpanOptionName);
        absl::string_view raw_time_span = time_span;
        if (absl::ConsumePrefix(&raw_time_span, "disk:")) {
          set_option->set_string_value(raw_time_span);
        }
      }
    }
  }
}

ddl::DDLStatementList Schema::Dump() const {
  ddl::DDLStatementList ddl_statements;
  // Do named schemas first since tables, views, sequences, and indexes rely on
  // them.
  for (const NamedSchema* named_schema : named_schemas_) {
    DumpNamedSchema(named_schema,
                    *ddl_statements.add_statement()->mutable_create_schema());
  }

  // Print sequences next, since other schema objects may use them.
  for (const Sequence* sequence : sequences_) {
    if (sequence->is_internal_use()) {
      // Do not print internal sequences.
      continue;
    }
    DumpSequence(sequence,
                 *ddl_statements.add_statement()->mutable_create_sequence());
  }

  for (const Table* table : tables_) {
    ddl::CreateTable* create_table =
        ddl_statements.add_statement()->mutable_create_table();
    create_table->set_table_name(table->Name());
    for (const Column* column : table->columns()) {
      DumpColumn(column, *create_table->add_column());
    }

    for (const ForeignKey* foreign_key : table->foreign_keys()) {
      DumpForeignKey(foreign_key, *create_table->add_foreign_key());
    }

    for (const KeyColumn* key_column : table->primary_key()) {
      ddl::KeyPartClause* key_part_clause = create_table->add_primary_key();
      key_part_clause->set_key_name(key_column->column()->Name());
    }

    if (table->parent() != nullptr) {
      DumpInterleaveClause(table, *create_table->mutable_interleave_clause());
    }

    // Unnamed check constraints are printed before named ones.
    for (const CheckConstraint* check_constraint : table->check_constraints()) {
      if (check_constraint->has_generated_name()) {
        DumpCheckConstraint(check_constraint,
                            *create_table->add_check_constraint());
      }
    }
    // Named check constraints.
    for (const CheckConstraint* check_constraint : table->check_constraints()) {
      if (!check_constraint->has_generated_name()) {
        DumpCheckConstraint(check_constraint,
                            *create_table->add_check_constraint());
      }
    }

    if (table->row_deletion_policy().has_value()) {
      *create_table->mutable_row_deletion_policy() =
          *table->row_deletion_policy();
    }
  }

  for (const auto& [unused_name, index] : index_map_) {
    if (!index->is_managed()) {
      DumpIndex(index, *ddl_statements.add_statement()->mutable_create_index());
    }
  }

  for (const Model* model : models_) {
    DumpModel(model, *ddl_statements.add_statement()->mutable_create_model());
  }

  for (const PropertyGraph* graph : property_graphs_) {
    DumpPropertyGraph(
        graph,
        *ddl_statements.add_statement()->mutable_create_property_graph());
  }

  for (const View* view : views_) {
    ddl::CreateFunction* create_function =
        ddl_statements.add_statement()->mutable_create_function();
    create_function->set_function_kind(ddl::Function::VIEW);
    create_function->set_function_name(view->Name());
    if (view->security() == View::INVOKER) {
      create_function->set_sql_security(ddl::Function::INVOKER);
    }
    create_function->set_sql_body(view->body());
    if (view->body_origin().has_value()) {
      create_function->mutable_sql_body_origin()->set_original_expression(
          *view->body_origin());
    }
  }

  for (const Udf* udf : udfs_) {
    ddl::CreateFunction* create_function =
        ddl_statements.add_statement()->mutable_create_function();
    create_function->set_function_kind(ddl::Function::FUNCTION);
    create_function->set_function_name(udf->Name());
    create_function->set_return_typename(
        udf->signature()->result_type().argument_name());
    create_function->set_sql_body(udf->body());
    if (udf->body_origin().has_value()) {
      create_function->mutable_sql_body_origin()->set_original_expression(
          *udf->body_origin());
    }
  }

  for (const ChangeStream* change_stream : change_streams_) {
    DumpChangeStream(
        change_stream,
        *ddl_statements.add_statement()->mutable_create_change_stream());
  }

  if (database_options_ != nullptr) {
    DumpDatabaseOptions(
        database_options_,
        *ddl_statements.add_statement()->mutable_alter_database());
  }

  for (const LocalityGroup* locality_group : locality_groups_) {
    DumpLocalityGroup(
        locality_group,
        *ddl_statements.add_statement()->mutable_create_locality_group());
  }

  return ddl_statements;
}

Schema::Schema(const SchemaGraph* graph,
               std::shared_ptr<const ProtoBundle> proto_bundle,
               const database_api::DatabaseDialect& dialect,
               std::string_view database_id)
    : graph_(graph),
      proto_bundle_(proto_bundle),
      dialect_(dialect),
      database_id_(database_id) {
  views_.clear();
  views_map_.clear();
  tables_.clear();
  tables_map_.clear();
  index_map_.clear();
  change_streams_.clear();
  change_streams_map_.clear();
  placements_.clear();
  placements_map_.clear();
  models_.clear();
  models_map_.clear();
  sequences_.clear();
  sequences_map_.clear();
  named_schemas_.clear();
  named_schemas_map_.clear();
  udfs_.clear();
  udfs_map_.clear();
  synonyms_.clear();
  synonyms_map_.clear();
  locality_groups_.clear();
  locality_groups_map_.clear();
  for (const SchemaNode* node : graph_->GetSchemaNodes()) {
    const View* view = node->As<const View>();
    if (view != nullptr) {
      views_.push_back(view);
      views_map_[view->Name()] = view;
      continue;
    }

    const Table* table = node->As<const Table>();
    if (table != nullptr && table->is_public()) {
      tables_.push_back(table);
      tables_map_[table->Name()] = table;
      if (!table->synonym().empty()) {
        synonyms_.push_back(table->synonym());
        synonyms_map_[table->synonym()] = table;
      }
      continue;
    }

    const Index* index = node->As<const Index>();
    if (index != nullptr) {
      index_map_[index->Name()] = index;
      continue;
    }

    const ChangeStream* change_stream = node->As<ChangeStream>();
    if (change_stream != nullptr) {
      change_streams_.push_back(change_stream);
      change_streams_map_[change_stream->Name()] = change_stream;
      continue;
    }

    const Placement* placement = node->As<Placement>();
    if (placement != nullptr) {
      placements_.push_back(placement);
      placements_map_[placement->PlacementName()] = placement;
      continue;
    }

    const Sequence* sequence = node->As<Sequence>();
    if (sequence != nullptr) {
      sequences_.push_back(sequence);
      sequences_map_[sequence->Name()] = sequence;
      continue;
    }

    const Model* model = node->As<Model>();
    if (model != nullptr) {
      models_.push_back(model);
      models_map_[model->Name()] = model;
      continue;
    }

    const PropertyGraph* property_graph = node->As<PropertyGraph>();
    if (property_graph != nullptr) {
      property_graphs_.push_back(property_graph);
      property_graphs_map_[property_graph->Name()] = property_graph;
      continue;
    }

    const NamedSchema* named_schema = node->As<NamedSchema>();
    if (named_schema != nullptr) {
      named_schemas_.push_back(named_schema);
      named_schemas_map_[named_schema->Name()] = named_schema;
      continue;
    }

    const Udf* udf = node->As<Udf>();
    if (udf != nullptr) {
      udfs_.push_back(udf);
      udfs_map_[udf->Name()] = udf;
      continue;
    }

    const LocalityGroup* locality_group = node->As<LocalityGroup>();
    if (locality_group != nullptr) {
      locality_groups_.push_back(locality_group);
      locality_groups_map_[locality_group->Name()] = locality_group;
      continue;
    }

    const DatabaseOptions* database_options = node->As<DatabaseOptions>();
    if (database_options != nullptr) {
      database_options_ = database_options;
      continue;
    }
    // Columns need not be stored in the schema, they are just owned by the
    // graph.
  }
}

std::pair<absl::string_view, absl::string_view> SDLObjectName::SplitSchemaName(
    absl::string_view name) {
  size_t last_dot = name.find_last_of('.');
  if (last_dot == absl::string_view::npos) {
    return {absl::string_view(), name};
  } else {
    return {name.substr(0, last_dot), name.substr(last_dot + 1)};
  }
}

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google

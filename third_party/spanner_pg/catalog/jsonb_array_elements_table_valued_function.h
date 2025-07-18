//
// PostgreSQL is released under the PostgreSQL License, a liberal Open Source
// license, similar to the BSD or MIT licenses.
//
// PostgreSQL Database Management System
// (formerly known as Postgres, then as Postgres95)
//
// Portions Copyright © 1996-2020, The PostgreSQL Global Development Group
//
// Portions Copyright © 1994, The Regents of the University of California
//
// Portions Copyright 2023 Google LLC
//
// Permission to use, copy, modify, and distribute this software and its
// documentation for any purpose, without fee, and without a written agreement
// is hereby granted, provided that the above copyright notice and this
// paragraph and the following two paragraphs appear in all copies.
//
// IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
// DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
// LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
// EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.
//
// THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
// FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN
// "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE
// MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
//------------------------------------------------------------------------------

#ifndef CATALOG_JSONB_ARRAY_ELEMENTS_TABLE_VALUED_FUNCTION_H_
#define CATALOG_JSONB_ARRAY_ELEMENTS_TABLE_VALUED_FUNCTION_H_

#include "zetasql/public/evaluator_table_iterator.h"
#include "zetasql/public/function_signature.h"
#include "zetasql/public/table_valued_function.h"
#include "absl/status/statusor.h"
#include "third_party/spanner_pg/datatypes/extended/pg_jsonb_type.h"

namespace postgres_translator {

using ::postgres_translator::spangres::datatypes::GetPgJsonbType;

// The emulator implementation of jsonb_array_elements.
class JsonbArrayElementsTableValuedFunction
    : public zetasql::FixedOutputSchemaTVF {
 public:
  explicit JsonbArrayElementsTableValuedFunction()
      : zetasql::FixedOutputSchemaTVF(
            /*function_name_path=*/{"pg.jsonb_array_elements"},
            zetasql::FunctionSignature(
                zetasql::FunctionArgumentType::RelationWithSchema(
                    zetasql::TVFRelation(
                        {{"jsonb_array_elements", GetPgJsonbType()}}),
                    /*extra_relation_input_columns_allowed=*/false),
                {GetPgJsonbType()}, nullptr),
            /*result_schema=*/
            zetasql::TVFRelation(
                {{"jsonb_array_elements", GetPgJsonbType()}})) {}

  absl::StatusOr<std::unique_ptr<zetasql::EvaluatorTableIterator>>
  CreateEvaluator(std::vector<TvfEvaluatorArg> input_arguments,
                  const std::vector<zetasql::TVFSchemaColumn>& output_columns,
                  const zetasql::FunctionSignature* function_call_signature)
      const override;
};

}  // namespace postgres_translator

#endif  // CATALOG_JSONB_ARRAY_ELEMENTS_TABLE_VALUED_FUNCTION_H_

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

#include "backend/schema/validators/placement_validator.h"

#include <string>

#include "absl/status/status.h"
#include "backend/schema/catalog/placement.h"
#include "backend/schema/ddl/operations.pb.h"
#include "backend/schema/updater/global_schema_names.h"
#include "backend/schema/updater/schema_validation_context.h"
#include "zetasql/base/ret_check.h"
#include "zetasql/base/status_macros.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

namespace {}  // namespace

absl::Status PlacementValidator::Validate(const Placement* placement,
                                          SchemaValidationContext* context) {
  ZETASQL_RET_CHECK(!placement->name_.empty());
  ZETASQL_RETURN_IF_ERROR(
      GlobalSchemaNames::ValidateSchemaName("Placement", placement->name_));
  return absl::OkStatus();
}

absl::Status PlacementValidator::ValidateUpdate(
    const Placement* placement, const Placement* old_placement,
    SchemaValidationContext* context) {
  return absl::OkStatus();
}

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google

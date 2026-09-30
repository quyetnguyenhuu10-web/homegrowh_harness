#pragma once

#include "../src/error/error.h"

provider::Result<void> validate_schema(const nlohmann::ordered_json& schema);

#pragma once

#include <nlohmann/json.hpp>

using json = nlohmann::json;

json build_transcript(json message);

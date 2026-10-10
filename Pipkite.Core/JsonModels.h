#pragma once
// JSON mapping for models that cross process boundaries (UI pipe, bridge pipe, storage).
// int64 identifiers are emitted as strings in UI-facing JSON.

#include <nlohmann/json.hpp>

#include "Models.h"
#include "Planner.h"
#include "Result.h"

namespace sr {

nlohmann::json toJson(const Signal& s);
nlohmann::json toJson(const ExecutionPlan& p);
nlohmann::json toJson(const Decision& d);
nlohmann::json toJson(const AccountSnapshot& a);
nlohmann::json toJson(const SymbolSpec& s);
nlohmann::json toJson(const Quote& q);
nlohmann::json toJson(const Deal& d);
nlohmann::json toJson(const AccountKey& k);

Result<ExecutionPlan> planFromJson(const nlohmann::json& j);
Result<AccountSnapshot> accountFromJson(const nlohmann::json& j);
Result<SymbolSpec> symbolFromJson(const nlohmann::json& j);
Result<Quote> quoteFromJson(const nlohmann::json& j);
Result<Deal> dealFromJson(const nlohmann::json& j);
AccountKey accountKeyFromJson(const nlohmann::json& j);

std::int64_t jsonInt64(const nlohmann::json& v, std::int64_t fallback = 0);  // accepts string or number

}  // namespace sr

#pragma once

#include <string>

namespace sr {

// Random (version 4) UUID string. Treated as an opaque identifier everywhere.
std::string newUuid();
bool looksLikeUuid(const std::string& s);

}  // namespace sr

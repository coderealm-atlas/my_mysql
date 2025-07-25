// Generated from error_codes.ini
#pragma once

#include <string>
#include <ostream>

namespace db_error {

enum class DbError : int {
  NotFound = 1000,
  MultipleResult = 1001,
  NullId = 1002,
};

inline int to_int(DbError e) { return static_cast<int>(e); }
}  // namespace error

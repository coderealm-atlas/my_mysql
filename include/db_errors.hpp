// Generated from error_codes.ini
#pragma once

#include <string>
#include <ostream>

namespace db_errors {

enum class DbError : int {
  NO_ROWS = 1000,
  MULTIPLE_RESULTS = 1001,
  NULL_ID = 1002,
  INDEX_OUT_OF_BOUNDS = 1003,
};

inline int to_int(DbError e) { return static_cast<int>(e); }
}  // namespace error

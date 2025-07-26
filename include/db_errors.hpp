// Generated from error_codes.ini
#pragma once

#include <string>
#include <ostream>

namespace db_errors {

enum class DbError : int {
  SQL_FAILED = 1000,
  NO_ROWS = 1001,
  MULTIPLE_RESULTS = 1002,
  NULL_ID = 1003,
  INDEX_OUT_OF_BOUNDS = 1004,
};

inline int to_int(DbError e) { return static_cast<int>(e); }
}  // namespace error

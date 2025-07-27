// Auto-generated from error_codes.ini
#pragma once

namespace db_errors {

namespace SQL_EXEC {  // SQL_EXEC errors

constexpr int SQL_FAILED = 1000;  // SQL execution failed.
constexpr int NO_ROWS = 1001;  // not found
constexpr int MULTIPLE_RESULTS = 1002;  // multiple result.
constexpr int NULL_ID = 1003;  // return object has null id.
constexpr int INDEX_OUT_OF_BOUNDS = 1004;  // row index out of bounds.
}  // namespace SQL_EXEC

namespace PARSE {  // PARSE errors

constexpr int BAD_VALUE_ACCESS = 2000;  // bad value access.
}  // namespace PARSE

}  // namespace db_errors
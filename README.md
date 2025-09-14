# MySQL Monadic Wrapper

A simple monadic wrapper around [Boost.MySQL](https://www.boost.org/doc/libs/develop/libs/mysql/doc/html/index.html) for asynchronous database operations with functional programming patterns.

This is **not a library** - it's a demonstration project showing how to use monadic patterns for MySQL database operations.

## Features

- Monadic async MySQL operations using `.then()` chains
- Dependency injection with boost::di
- Comprehensive testing with Google Test
- Performance benchmarking with Google Benchmark
- Uses the official MySQL Sakila sample database for realistic testing

## Usage Examples

### Basic Query

```cpp
#include "mysql_monad.hpp"


TEST_F(MonadMysqlTest, expect_count) {
  using namespace monad;
  std::optional<MyResult<std::tuple<int64_t, int64_t>>> result_opt;
  return session_
      ->run_query([](mysql::pooled_connection& conn) {
        mysql::format_context ctx(conn->format_opts().value());
        mysql::format_sql_to(ctx, "SELECT COUNT(*) FROM film;");
        mysql::format_sql_to(ctx, "SELECT COUNT(*) FROM country;");
        return StringResult::Ok(std::move(ctx).get().value());
      })
      .then([](auto state) {
        // First result set: film count
        return IO<std::tuple<int64_t, int64_t>>::from_result(
            zip_results(state.expect_count("film count", 0),
                        state.expect_count("country count", 1)));
      })
      .run([&](auto r) {
        result_opt = std::move(r);
        notifyCompletion();
      });
  waitForCompletion();
  EXPECT_FALSE(result_opt->is_err()) << result_opt->error();
}
```

### Chained Operations

```cpp
// Insert, verify, and cleanup in a chain
TEST_F(MonadMysqlTest, insert_verify_clean) {
  using namespace monad;
  using RetTuple = std::tuple<uint64_t, int64_t, int64_t>;
  std::string cname = "Test Country";
  std::optional<MyResult<RetTuple>> result_opt;
  return session_
      ->run_query([cname](mysql::pooled_connection& conn) {
        mysql::format_context ctx(conn->format_opts().value());
        mysql::format_sql_to(
            ctx,
            "INSERT INTO country (country, last_update) VALUES ({}, NOW());",
            cname);
        mysql::format_sql_to(ctx, "SELECT LAST_INSERT_ID();");
        mysql::format_sql_to(
            ctx, "SELECT COUNT(*) FROM country WHERE country = {};", cname);
        mysql::format_sql_to(ctx, "DELETE FROM country WHERE country = {};",
                             cname);
        return StringResult::Ok(std::move(ctx).get().value());
      })
      .then([](auto state) {
        // First result set: insert
        auto insert_res = state.expect_affected_rows("query affected rows failed.", 0);
        // Second result set: id
        auto id_res = state.template expect_one_value<int64_t>(
            "Expect id of insert failed.", 1, 0);
        // Third result set: count
        auto count_res = state.expect_count("Expect one row with count failed.", 2);
        // Fourth result set: delete
        auto del_res =
            state.expect_affected_one_row("Expect one row deleted failed.", 3);
        return monad::IO<RetTuple>::from_result(
            zip_results_skip_void(insert_res, id_res, count_res, del_res));
      })
      .run([&](auto result) {
        result_opt = std::move(result);
        notifyCompletion();
      });
  waitForCompletion();
  EXPECT_FALSE(result_opt->is_err()) << result_opt->error();
  auto [insert_row, id, count] = result_opt->value();
  ASSERT_EQ(insert_row, 1);
  ASSERT_EQ(count, 1);
  ASSERT_GT(id, 0);
}
```

## Building

Requires:
- C++20 compiler 
- CMake
- vcpkg for dependencies
- MySQL server (for testing)

```bash
# Configure
cmake --preset=default

# Build
cmake --build build

# Run tests
./build/tests/sakila_integration_test

# Run benchmarks  
./build/bm/sakila_benchmark
```

## Database Setup

The project uses database migrations via [dbmate](https://github.com/amacneil/dbmate):

```bash
# Setup test database with Sakila schema
dbmate --env-file db/.env_test --migrations-dir db/test_migrations up
```

## Project Structure

```
├── src/                    # Source files
├── include/                # Headers  
├── tests/                  # Integration tests
├── bm/                     # Benchmarks
├── db/                     # Database migrations and schema
├── config_dir/             # Configuration files
└── http_client/            # HTTP client submodule
```

## Key Components

- **MonadicMysqlSession**: Main wrapper providing monadic interface
- **MysqlSessionState**: State container for query results and errors  
- **IO Monad**: Functional composition with `.then()` and `.run()`
- **Dependency Injection**: Uses boost::di for clean architecture
- **Async Operations**: All operations are asynchronous using Boost.Asio

This project demonstrates functional programming patterns applied to database operations, making complex async workflows more readable and composable.
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

// Create a session (injected via dependency injection)
auto session = session_factory_();

// Simple SELECT query
session->run_query("SELECT COUNT(*) FROM film")
    .then([](auto state) {
        if (state.has_error()) {
            std::cerr << "Query failed: " << state.error_message() << std::endl;
            return IO<MysqlSessionState>::pure(std::move(state));
        }
        
        auto result = state.expect_one_row("Expected film count", 0, 0);
        if (result.is_ok()) {
            auto count = result.value().at(0).as_int64();
            std::cout << "Film count: " << count << std::endl;
        }
        
        return IO<MysqlSessionState>::pure(std::move(state));
    })
    .run([](auto result) {
        std::cout << "Query completed" << std::endl;
    });
```

### Chained Operations

```cpp
// Insert, verify, and cleanup in a chain
session->run_query("INSERT INTO country (country, last_update) VALUES ('Test Country', NOW())")
    .then([&](auto state) {
        if (state.has_error()) {
            return IO<MysqlSessionState>::error(std::move(state));
        }
        // Verify insertion
        return session->run_query("SELECT COUNT(*) FROM country WHERE country = 'Test Country'");
    })
    .then([&](auto state) {
        auto result = state.expect_one_row("Expected count", 0, 0);
        auto count = result.value().at(0).as_int64();
        std::cout << "Inserted records: " << count << std::endl;
        
        // Cleanup
        return session->run_query("DELETE FROM country WHERE country = 'Test Country'");
    })
    .run([](auto result) {
        std::cout << "Operation chain completed" << std::endl;
    });
```

### Complex JOIN Query

```cpp
session->run_query(R"(
    SELECT f.title, c.name as category, l.name as language 
    FROM film f 
    JOIN film_category fc ON f.film_id = fc.film_id 
    JOIN category c ON fc.category_id = c.category_id 
    JOIN language l ON f.language_id = l.language_id 
    LIMIT 10
)")
    .then([](auto state) {
        if (state.has_error()) {
            std::cerr << "Join query failed" << std::endl;
            return IO<MysqlSessionState>::pure(std::move(state));
        }
        
        for (const auto& row : state.results.rows()) {
            std::cout << "Film: " << row.at(0).as_string() 
                      << " | Category: " << row.at(1).as_string()
                      << " | Language: " << row.at(2).as_string() << std::endl;
        }
        
        return IO<MysqlSessionState>::pure(std::move(state));
    })
    .run([](auto result) {
        std::cout << "Join query completed" << std::endl;
    });
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
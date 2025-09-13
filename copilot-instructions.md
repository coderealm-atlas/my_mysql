# Copilot Instructions for my_mysql Project

## Project Overview
This is a C++ MySQL wrapper library with monadic error handling, built using modern C++20 features. The project provides type-safe database operations with comprehensive error handling.

## Architecture
- **Core Library**: MySQL connection pooling and query execution with monadic error handling
- **Dependencies**: Boost (MySQL, ASIO, JSON), OpenSSL, GTest for testing
- **Build System**: CMake with vcpkg for dependency management
- **Submodules**: `http_client` - HTTP client utilities and common headers

## Key Components

### Database Layer
- `mysql_base.hpp` - Core MySQL connection and pool management
- `mysql_monad.hpp` - Monadic error handling for database operations
- `mysql_config_provider.hpp` - Database configuration management

### Testing
- Simplified test schema in `db/test_migrations/` (production schema excluded for security)
- Test database: `cjj365_test_simple` 
- Unit tests in `tests/my_mysql_test.cpp`

### Configuration
- Config files in `config_dir/` (gitignored except .tpl templates)
- Environment-specific configs: `.env_test`, `.env_local`, etc.

## Development Guidelines

### CMake Configuration
- Default build configuration is **Debug** 
- Main CMakeLists.txt handles vcpkg integration and core library
- Tests include http_client submodule sources directly via `file(GLOB HTTP_CLIENT_SOURCES)`
- Use `target_include_directories` for proper header inclusion
- Debug build includes optimizations (`-O1`) with debug symbols for faster development

### Database Security
- **NEVER commit** actual migration files or production schema
- `db/migrations/` and `db/schema.sql` are gitignored
- Use `db/test_migrations/` for simplified test-only schemas
- Always use parameterized queries and environment variables for credentials

### Submodule Management
- `http_client` provides shared utilities and headers
- After git operations that rewrite history, may need to re-add submodule:
  ```bash
  git submodule add https://github.com/coderealm-atlas/http_client.git
  ```

### Code Style
- Modern C++20 features (concepts, ranges, coroutines where applicable)
- Monadic error handling patterns using custom Result types
- RAII for resource management
- Dependency injection using boost::di

### Testing Approach
- **Unit Tests**: Isolated test database with minimal schema (`tests/my_mysql_test.cpp`)
- **Integration Tests**: Sakila sample database for comprehensive testing (`tests/sakila_integration_test.cpp`)
- **Performance Benchmarks**: Google Benchmark with realistic data volumes (`bm/sakila_benchmark.cpp`)
- Database reset before each test using dbmate
- Tests use **develop** configuration profile (not separate test configs)
- Async test patterns with ThreadNotifier for coordination
- Focus on error conditions and edge cases

#### Sakila Integration Testing
- **Schema**: Full DVD rental store schema with 15+ tables and foreign key relationships
- **Data Volume**: 1000 films, 200 actors, 100 countries, 500 cities for realistic testing
- **Test Coverage**: Complex JOINs, transactions, aggregations, text search, pagination
- **Performance**: Benchmarks for query performance, pagination efficiency, large result sets

## Common Tasks

### Adding New Tests

#### Unit Tests (Simple)
1. Add to `tests/my_mysql_test.cpp` using `MonadMysqlTest` fixture
2. Uses simple schema from `db/test_migrations/20250912000001_simple_test_schema.sql`
3. Reset: `dbmate --env-file db/.env_test --migrations-dir db/test_migrations drop && up`

#### Integration Tests (Comprehensive)
1. Add to `tests/sakila_integration_test.cpp` using `SakilaIntegrationTest` fixture  
2. Uses Sakila schema from `db/test_migrations/20250912000002_sakila_schema.sql`
3. Automatic test data generation for realistic scenarios
4. Ideal for testing complex queries, transactions, performance edge cases

#### Performance Benchmarks
1. Add to `bm/sakila_benchmark.cpp` using Google Benchmark framework
2. Includes setup for substantial test data (1000s of records)
3. Measures query performance, memory usage, concurrency

#### Common Notes
- Tests automatically use **develop** configuration profile
- No separate test configs needed - uses existing `mysql_config.develop.json`
- Both test suites can coexist and test different aspects

### Building
```bash
# Build all tests and benchmarks
cmake --build build --parallel 6

# Build specific targets
cmake --build build --target my_mysql_test                 # Unit tests
cmake --build build --target sakila_integration_test       # Integration tests  
cmake --build build --target sakila_benchmark              # Performance benchmarks
```

### Running Tests
```bash
# Unit tests (fast, basic functionality)
./build/tests/my_mysql_test --gtest_filter=TestName

# Integration tests (comprehensive, realistic data)
./build/tests/sakila_integration_test --gtest_filter=SakilaIntegrationTest.test_name

# Performance benchmarks
./build/bm/sakila_benchmark --benchmark_filter=BM_SimpleSelect
```

## Security Considerations
- Database credentials in environment variables only
- Production schemas and migrations are excluded from git history
- Config templates (.tpl) show structure without sensitive data
- Regular security review of exposed database operations

## Dependencies
- Boost (asio, mysql, json, iostreams, log, uuid)
- OpenSSL for TLS/SSL
- GTest/GMock for testing
- date library for time handling
- vcpkg for package management

## File Structure
```
├── include/           # Public headers
├── src/              # Implementation files  
├── tests/            # Unit tests (simplified dependencies)
├── config_dir/       # Configuration (gitignored except .tpl)
├── db/
│   ├── test_migrations/  # Simplified test schema
│   └── .env_*            # Database configurations
├── http_client/      # Submodule for shared utilities
└── build/           # CMake build directory
```

## Notes for AI Assistants
- Always check current file contents before editing (user may have made manual changes)
- Respect security boundaries around database files
- When modifying CMake, ensure http_client submodule integration remains intact
- Test changes incrementally to catch configuration issues early
- Use descriptive commit messages explaining security and architectural decisions
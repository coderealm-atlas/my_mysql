// Simple RAII helper to reset the test database (drop + migrate up)
// Usage: create an instance at the start of a test SetUp and assert rc()==0.
// Optionally configurable via env vars:
//   TEST_DB_ENV_FILE (default: db/.env_test)
//   TEST_DB_MIGRATIONS_DIR (default: db/test_migrations)
#pragma once
#include <cstdlib>
#include <sstream>
#include <string>

class DbResetter {
  int rc_ = -1;
  std::string command_;

  static std::string get_env_or(const char* key, const char* def) {
    if (const char* v = std::getenv(key); v && *v) return std::string(v);
    return std::string(def);
  }

 public:
  DbResetter() {
    auto env_file = get_env_or("TEST_DB_ENV_FILE", "db/.env_test");
    auto migrations_dir = get_env_or("TEST_DB_MIGRATIONS_DIR", "db/test_migrations");
    std::ostringstream oss;
    oss << "dbmate --env-file " << env_file << " --migrations-dir " << migrations_dir
        << " drop && dbmate --env-file " << env_file << " --migrations-dir " << migrations_dir << " up";
    command_ = oss.str();
    rc_ = std::system(command_.c_str());
  }
  int rc() const { return rc_; }
  const std::string& command() const { return command_; }
};

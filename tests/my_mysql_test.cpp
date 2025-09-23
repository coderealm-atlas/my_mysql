#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <filesystem>
#include <tuple>
#include <thread>
#include <chrono>

#include "boost/di.hpp"
#include "common_macros.hpp"
#include "io_context_manager.hpp"
#include "misc_util.hpp"
#include "mysql_monad.hpp"
#include "result_monad.hpp"
#include "tutil.hpp"  // IWYU pragma: keep
#include "test_injectors.hpp"
#include "db_resetter.hpp"

// (Removed unused namespace alias and header to satisfy linter)

// Using shared helpers from test_injectors namespace now


static cjj365::IIoContextManager* io_context_manager_holder;
// Global test environment to run once after all tests in this file
class MysqlTestGlobalTestEnv : public ::testing::Environment {
 public:
  void TearDown() override {
    // Final cleanup after the whole test suite in this binary
    std::cerr << "[MysqlTestGlobalTestEnv] Global TearDown completed."
              << std::endl;
    if (io_context_manager_holder) {
      io_context_manager_holder->stop();
    }
  }
};

// Register the global environment
static const auto* const global_env =
    ::testing::AddGlobalTestEnvironment(new MysqlTestGlobalTestEnv());

// Test fixture class to reduce duplication
class MonadMysqlTest : public ::testing::Test {
  using Injector = decltype(test_injectors::build_unit_test_injector());
  std::unique_ptr<Injector> injector_;

 protected:
  void SetUp() override {
    // Reset database for each test via RAII helper
    DbResetter resetter;
    ASSERT_EQ(resetter.rc(), 0) << "Failed to reset test database. Command: " << resetter.command();

  injector_ = std::make_unique<Injector>(test_injectors::build_unit_test_injector());
    session_factory_ = injector_->create<monad::MonadicMysqlSession::Factory>();
    session_ = session_factory_();
    io_context_manager_holder = &injector_->create<cjj365::IIoContextManager&>();
  }

  void TearDown() override {
    // Release session before leak assertion
    session_.reset();
    // Idle wait loop: give async callbacks a short window to release final shared_ptr refs
    // (observed occasional race under coverage/instrumentation builds)
    for (int i = 0; i < 50 && monad::MonadicMysqlSession::instance_count.load() != 0; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto remaining = monad::MonadicMysqlSession::instance_count.load();
    const char* relax_env = std::getenv("MYSQL_TEST_RELAX_ASSERT");
    if (remaining != 0) {
      if (relax_env) {
        std::cerr << "[warn] MonadicMysqlSession leak count=" << remaining
                  << " (relaxed by MYSQL_TEST_RELAX_ASSERT)" << std::endl;
      } else {
        ASSERT_EQ(remaining, 0) << "Leaked MonadicMysqlSession instances at test end";
      }
    }
  }

  // Helper method to wait for async operations
  void waitForCompletion() { notifier_.waitForNotification(); }

  // Helper method to notify completion
  void notifyCompletion() { notifier_.notify(); }

  misc::ThreadNotifier notifier_;
  monad::MonadicMysqlSession::Factory session_factory_;
  std::shared_ptr<monad::MonadicMysqlSession> session_;
};

TEST_F(MonadMysqlTest, test_running_dir) {
  auto current_dir = std::filesystem::current_path();
  std::cerr << "Current directory: " << std::filesystem::absolute(current_dir)
            << std::endl;
}

//clang-format off
// gdb --args ./build/tests/my_mysql_test
// --gtest_filter=MonadMysqlTest.only_one_row
// clang-format on
TEST_F(MonadMysqlTest, only_one_row) {
  using namespace monad;

  session_
      ->run_query(
          "INSERT INTO cjj365_users (name, email, password, roles, state) "
          "VALUES ('jianglibo', 'jianglibo@hotmail.com', 'password123', "
          "JSON_ARRAY('user', 'admin', 'notallowed'), 'active')")
      .then([&](auto state) {
        EXPECT_FALSE(state.has_error());
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .then([&](auto) {
        return session_->run_query("SELECT COUNT(*) FROM cjj365_users")
            .then([&](auto state) {
              EXPECT_FALSE(state.has_error());
              auto result =
                  state.expect_one_row_borrowed("Expected one row with count", 0, 0);
              EXPECT_TRUE(result.is_ok());
              EXPECT_EQ(result.value().at(0).as_int64(), 1);
              return IO<MysqlSessionState>::pure(std::move(state));
            });
      })
      .then([&](auto) {
        return session_->run_query("DELETE FROM cjj365_users;");
      })
      .map([&](auto state) {
        auto rr = state.expect_affected_one_row("Expected one row deleted", 0);
        EXPECT_TRUE(rr.is_ok());
        return state;
      })
      .run([&](auto r) {
        EXPECT_TRUE(r.is_ok());
        this->notifyCompletion();
      });

  this->waitForCompletion();
}

TEST_F(MonadMysqlTest, list_row_ok) {
  using namespace monad;

  session_
      ->run_query(
          "SELECT * FROM cjj365_users;SELECT COUNT(*) FROM cjj365_users;")
      .then([&](auto state) {
        EXPECT_FALSE(state.has_error());
        auto result =
            state.expect_list_of_rows("Expected empty list and count", 0, 1);
        EXPECT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().first.rows().size(), 0);
        EXPECT_EQ(result.value().second, 0);
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .run([&](auto r) {
        EXPECT_TRUE(r.is_ok());
        this->notifyCompletion();
      });

  this->waitForCompletion();
}

TEST_F(MonadMysqlTest, list_row_out_of_bounds) {
  using namespace monad;

  session_
      ->run_query(
          "SELECT * FROM cjj365_users;SELECT COUNT(*) FROM cjj365_users;")
      .then([&](auto state) {
        auto result = state.expect_list_of_rows("Index OOB test", 0, 2);
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code,
                  db_errors::SQL_EXEC::INDEX_OUT_OF_BOUNDS);
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .run([&](auto r) {
        EXPECT_FALSE(r.is_err());
        this->notifyCompletion();
      });

  this->waitForCompletion();
}

TEST_F(MonadMysqlTest, sql_failed) {
  using namespace monad;

  session_->run_query("SELECT x* FROM cjj365_users;").run([&](auto r) {
    auto rr = r.value().expect_one_row_borrowed("Expect fail", 0, 0);
    EXPECT_TRUE(rr.is_err());
    EXPECT_EQ(rr.error().code, db_errors::SQL_EXEC::SQL_FAILED);
    this->notifyCompletion();
  });

  this->waitForCompletion();
}

TEST_F(MonadMysqlTest, maybe_one_row_borrowed) {
  using namespace monad;

  session_->run_query("SELECT * FROM cjj365_users WHERE id = 1")
      .then([&](auto state) {
        // Test case: No rows
        DEBUG_PRINT("[debug] 1");
        auto result = state.maybe_one_row_borrowed(0, 0);
        EXPECT_TRUE(result.is_ok());
        EXPECT_FALSE(result.value().has_value());
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .then([&](auto) {
        DEBUG_PRINT("[debug] 2");
        return session_->run_query(
            "INSERT INTO cjj365_users (name, email, password, roles, state) "
            "VALUES ('jianglibo', 'jianglibo@hotmail.com', 'password123', "
            "JSON_ARRAY('user', 'admin', 'notallowed'), 'active');");
      })
      .then([&](auto state) {
        DEBUG_PRINT("[debug] 3");
        EXPECT_FALSE(state.has_error());
        return session_->run_query("SELECT id FROM cjj365_users WHERE id = 1");
      })
      .then([&](auto state) {
        // Test case: One row
        DEBUG_PRINT("[debug] 4");
        auto result = state.maybe_one_row_borrowed(0, 0);
        DEBUG_PRINT("[debug] 4.1");
        EXPECT_TRUE(result.is_ok());
        DEBUG_PRINT("[debug] 4.2");
        EXPECT_TRUE(result.value().has_value());
        DEBUG_PRINT("[debug] 4.3, kind: " << result.value()->at(0).kind());
        EXPECT_EQ(result.value()->at(0).as_int64(), 1);
        DEBUG_PRINT("[debug] 4.4");
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .then([&](auto) {
        DEBUG_PRINT("[debug] 5");
        return session_->run_query(
            "INSERT INTO cjj365_users (name, email, password, roles) "
            "VALUES ('testuser2', 'test2@test.com', 'password', "
            "JSON_ARRAY('user'));");
      })
      .then([&](auto state) {
        DEBUG_PRINT("[debug] 6");
        EXPECT_FALSE(state.has_error()) << state.diagnostics();
        return session_->run_query("SELECT * FROM cjj365_users");
      })
      .then([&](auto state) {
        // Test case: Multiple rows
        DEBUG_PRINT("[debug] 7");
        auto result = state.maybe_one_row_borrowed(0, 0);
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code, db_errors::SQL_EXEC::MULTIPLE_RESULTS);
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .then([&](auto) {
        DEBUG_PRINT("[debug] 8");
        return session_->run_query(
            "SELECT name, NULL as email FROM cjj365_users WHERE id = 1");
      })
      .then([&](auto state) {
        DEBUG_PRINT("[debug] 9");
        // Test case: NULL value in specified column
        auto result =
            state.maybe_one_row_borrowed(0, 1);  // column 1 is email, which is NULL
        EXPECT_TRUE(result.is_ok());
        EXPECT_FALSE(result.value().has_value());
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .run([&](auto r) {
        if (r.is_err()) {
          std::cerr << "Final error: " << r.error() << std::endl;
        }
        EXPECT_TRUE(r.is_ok());
        this->notifyCompletion();
      });

  this->waitForCompletion();
}

TEST_F(MonadMysqlTest, expect_count) {
  using namespace monad;
  std::optional<MyResult<std::tuple<int64_t, int64_t>>> result_opt;
  session_
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
        this->notifyCompletion();
      });
  this->waitForCompletion();
  EXPECT_FALSE(result_opt->is_err()) << result_opt->error();
}

TEST_F(MonadMysqlTest, expect_one_row_cols_gt_semantics) {
  using namespace monad;
  // Build a query returning exactly one row with 5 columns
  session_
      ->run_query(
          "SELECT 1 AS a, 2 AS b, 3 AS c, 4 AS d, 5 AS e;")
      .then([&](auto state) {
        EXPECT_FALSE(state.has_error()) << state.diagnostics();
        // Should succeed when threshold < column count (5)
        auto ok_res = state.expect_one_row_cols_gt("expect columns > 3", 3);
        EXPECT_TRUE(ok_res.is_ok());
        // Should fail when threshold >= column count (strict >)
        auto fail_res = state.expect_one_row_cols_gt("expect columns > 5", 5);
        EXPECT_TRUE(fail_res.is_err());
        if (fail_res.is_err()) {
          EXPECT_EQ(fail_res.error().code, db_errors::SQL_EXEC::NO_ROWS);
        }
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .run([&](auto r) {
        EXPECT_TRUE(r.is_ok());
        this->notifyCompletion();
      });
  this->waitForCompletion();
}

TEST_F(MonadMysqlTest, expect_one_value_unsupported_type) {
  using namespace monad;
  session_
      ->run_query("SELECT 42 AS answer;")
      .then([&](auto state) {
        EXPECT_FALSE(state.has_error()) << state.diagnostics();
        // float is not a supported target type; should yield BAD_VALUE_ACCESS
  auto unsupported = state.template expect_one_value<float>("unsupported float", 0);
        EXPECT_TRUE(unsupported.is_err());
        if (unsupported.is_err()) {
          EXPECT_EQ(unsupported.error().code, db_errors::PARSE::BAD_VALUE_ACCESS);
        }
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .run([&](auto r) {
        EXPECT_TRUE(r.is_ok());
        this->notifyCompletion();
      });
  this->waitForCompletion();
}

TEST_F(MonadMysqlTest, insert_verify_clean) {
  using namespace monad;
  using RetTuple = std::tuple<uint64_t, int64_t, int64_t>;
  std::string cname = "Test Country";
  std::optional<MyResult<RetTuple>> result_opt;
  session_
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
        auto insert_res = state.expect_affected_rows("Expect affected rows", 0);
        // Second result set: id
        auto id_res = state.template expect_one_value<int64_t>(
            "Expect id of insert", 1, 0);
        // Third result set: count
        auto count_res = state.expect_count("Expect one row with count", 2);
        // Fourth result set: delete
        auto del_res =
            state.expect_affected_one_row("Expect one row deleted", 3);
        return monad::IO<RetTuple>::from_result(
            zip_results_skip_void(insert_res, id_res, count_res, del_res));
      })
      .run([&](auto result) {
        result_opt = std::move(result);
        this->notifyCompletion();
      });
  this->waitForCompletion();
  EXPECT_FALSE(result_opt->is_err()) << result_opt->error();
  auto [insert_row, id, count] = result_opt->value();
  ASSERT_EQ(insert_row, 1);
  ASSERT_EQ(count, 1);
  ASSERT_GT(id, 0);
}
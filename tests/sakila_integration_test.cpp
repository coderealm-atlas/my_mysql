#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <filesystem>
#include <variant>

#include "boost/di.hpp"
#include "common_macros.hpp"
#include "log_stream.hpp"
#include "misc_util.hpp"
#include "mysql_base.hpp"
#include "mysql_monad.hpp"
#include "simple_data.hpp"
#include "tutil.hpp"
#include "test_injectors.hpp"
#include "db_resetter.hpp"
#include "test_openssl_env.hpp"  // IWYU pragma: keep

namespace fs = std::filesystem; // di alias removed (not directly used here)

// Using shared injector builders from test_injectors namespace

// Test fixture for Sakila integration testing
class SakilaIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Reset database and apply schema
    DbResetter resetter; // uses env overrides if provided
    ASSERT_EQ(resetter.rc(), 0) << "Failed to reset test database with Sakila schema. Command: " << resetter.command();

  using Injector = decltype(test_injectors::build_integration_test_injector());
  injector_ = std::make_unique<Injector>(test_injectors::build_integration_test_injector());
  session_factory_ = injector_->create<monad::MonadicMysqlSession::Factory>();
    // Test that we can create a session (but don't store it - create fresh ones per test)
    auto test_session = session_factory_();
    ASSERT_TRUE(test_session) << "Failed to create MySQL session factory";
  }

  void TearDown() override {
    // Assert no leaked sessions across integration tests
    ASSERT_EQ(monad::MonadicMysqlSession::instance_count.load(), 0)
        << "Leaked MonadicMysqlSession instances after integration test";
  }

  // Notification helpers for async tests
  void notifyCompletion() {
    notifier_.notify();
  }

  void waitForCompletion() {
    notifier_.waitForNotification();
  }

  // Static helper functions
  static cjj365::ConfigSources& config_sources() {
    static cjj365::ConfigSources instance({fs::path{"config_dir"}},
                                          {"test", "develop"});
    return instance;
  }

  static customio::ConsoleOutputWithColor& output() {
    static customio::ConsoleOutputWithColor instance(4);
    return instance;
  }

 protected:
  misc::ThreadNotifier notifier_;
  std::unique_ptr<decltype(test_injectors::build_integration_test_injector())> injector_;
  monad::MonadicMysqlSession::Factory session_factory_;
  
  // Helper method to create a fresh session for each test
  std::shared_ptr<monad::MonadicMysqlSession> createSession() {
    return session_factory_();
  }
};

// ============================================================================
// BASIC FUNCTIONALITY TESTS
// ============================================================================

TEST_F(SakilaIntegrationTest, test_schema_exists) {
  using namespace monad;
  
  // Create a fresh session for this test
  auto session = createSession();
  ASSERT_TRUE(session) << "Failed to create session for test";
  
  // Test that main Sakila tables exist - just check a few key ones
  session->run_query(
      "SELECT COUNT(*) as count FROM information_schema.tables "
      "WHERE table_schema = DATABASE() AND table_name IN "
      "('actor', 'film', 'country', 'language')")
      .then([&](auto state) {
        EXPECT_FALSE(state.has_error());
        auto result = state.expect_one_row_borrowed("Expected one row with table count", 0, 0);
        EXPECT_TRUE(result.is_ok());
        auto count = result.value().at(0).as_int64();
        EXPECT_EQ(count, 4) << "Should have 4 main Sakila tables";
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .run([&](auto r) {
        EXPECT_TRUE(r.is_ok());
        notifyCompletion();
      });

  waitForCompletion();
}

TEST_F(SakilaIntegrationTest, test_basic_data_insertion) {
  using namespace monad;
  
  // Create a fresh session for this test
  auto session = createSession();
  ASSERT_TRUE(session) << "Failed to create session for test";
  
  // Test basic data insertion and retrieval
  session->run_query(
      "INSERT INTO country (country, last_update) VALUES ('Test Country', NOW())")
      .then([&, session](auto state) {
        EXPECT_FALSE(state.has_error());
        return session->run_query("SELECT COUNT(*) FROM country WHERE country = 'Test Country'");
      })
      .then([&, session](auto state) {
        EXPECT_FALSE(state.has_error());
        auto result = state.expect_one_row_borrowed("Expected one row with count", 0, 0);
        EXPECT_TRUE(result.is_ok());
        auto count = result.value().at(0).as_int64();
        EXPECT_EQ(count, 1) << "Should find the inserted country";
        return session->run_query("DELETE FROM country WHERE country = 'Test Country'");
      })
      .run([&](auto r) {
        EXPECT_TRUE(r.is_ok());
        notifyCompletion();
      });

  waitForCompletion();
}

// More comprehensive tests would go here...
// For now, let's focus on getting the basic infrastructure working
// Future enhancements can add performance tests, complex queries, etc.
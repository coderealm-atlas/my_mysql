#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <filesystem>
#include <variant>

#include "boost/di.hpp"
#include "common_macros.hpp"
#include "io_context_manager.hpp"
#include "log_stream.hpp"
#include "misc_util.hpp"
#include "my_di_extension.hpp"
#include "mysql_base.hpp"
#include "mysql_config_provider.hpp"
#include "mysql_monad.hpp"
#include "simple_data.hpp"
#include "tutil.hpp"

namespace di = boost::di;
namespace fs = std::filesystem;

// Test fixture for Sakila integration testing
class SakilaIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Reset database and apply both simple schema and Sakila schema
    int rc = std::system(
        "dbmate --env-file db/.env_test --migrations-dir db/test_migrations "
        "drop && dbmate --env-file "
        "db/.env_test --migrations-dir db/test_migrations up");
    ASSERT_EQ(rc, 0) << "Failed to reset test database with Sakila schema";

    // Create injector and factory
    auto injector = di::make_injector(
        di::bind<sql::IMysqlConfigProvider>().to<sql::MysqlConfigProviderFile>(),
        di::bind<cjj365::ConfigSources>().to(config_sources()),
        di::bind<customio::IOutput>().to(output()),
    // Provide IoContextManager implementation for interface to satisfy DI constraints
    di::bind<cjj365::IIoContextManager>().to<cjj365::IoContextManager>(),
        bind_shared_factory<monad::MonadicMysqlSession>(),
        di::bind<cjj365::IIocConfigProvider>()
            .to<cjj365::IocConfigProviderFile>());

    // Get the factory before storing in void*
    session_factory_ = injector.create<monad::MonadicMysqlSession::Factory>();
    
    // Store the injector in void* with a proper deleter
    injector_ = std::unique_ptr<void, std::function<void(void*)>>(
        new auto(std::move(injector)),
        [](void* ptr) { delete static_cast<std::remove_reference_t<decltype(injector)>*>(ptr); }
    );
    
    // Test that we can create a session (but don't store it - create fresh ones per test)
    auto test_session = session_factory_();
    ASSERT_TRUE(test_session) << "Failed to create MySQL session factory";
  }

  void TearDown() override {
    // No need to clean up individual sessions - they're created per test
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
  std::unique_ptr<void, std::function<void(void*)>> injector_;
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
        auto result = state.expect_one_row("Expected one row with table count", 0, 0);
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
        auto result = state.expect_one_row("Expected one row with count", 0, 0);
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
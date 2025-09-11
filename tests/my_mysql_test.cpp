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
#include "tutil.hpp"  // IWYU pragma: keep

namespace di = boost::di;

static cjj365::ConfigSources& config_sources() {
  static cjj365::ConfigSources instance({fs::path{"config_dir"}}, {});
  return instance;
}
static customio::ConsoleOutputWithColor& output() {
  static customio::ConsoleOutputWithColor instance(4);
  return instance;
}

TEST(MonadMysqlTest, test_running_dir) {
  auto current_dir = std::filesystem::current_path();
  std::cerr << "Current directory: " << std::filesystem::absolute(current_dir)
            << std::endl;
}

//clang-format off
// gdb --args ./build/tests/my_mysql_test
// --gtest_filter=MonadMysqlTest.only_one_row
// clang-format on
TEST(MonadMysqlTest, only_one_row) {
  misc::ThreadNotifier notifier;
  using namespace monad;
  int rc = std::system(
      "dbmate --env-file db/.env_local drop && dbmate --env-file "
      "db/.env_local up");
  ASSERT_EQ(rc, 0) << "Failed to reset test database";

  // std::vector<fs::path> config_paths = {std::filesystem::current_path() /
  //                                       "config_dir"};

  // must static or else io_context_manager which holder output will got
  // SIGSEGV.

  auto injector = di::make_injector(
      di::bind<sql::IMysqlConfigProvider>().to<sql::MysqlConfigProviderFile>(),
      di::bind<cjj365::ConfigSources>().to(config_sources()),
      di::bind<customio::IOutput>().to(output()),
      bind_shared_factory<monad::MonadicMysqlSession>(),
      di::bind<cjj365::IIocConfigProvider>()
          .to<cjj365::IocConfigProviderFile>());

  auto session_factory = injector.create<monad::MonadicMysqlSession::Factory>();
  auto session = session_factory();

  session
      ->run_query(
          "INSERT INTO cjj365_users (name, email, password, roles, state) "
          "VALUES ('jianglibo', 'jianglibo@hotmail.com', 'password123', "
          "JSON_ARRAY('user', 'admin', 'notallowed'), 'active')")
      .then([&](auto state) {
        EXPECT_FALSE(state.has_error());
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .then([&](auto) {
        return session->run_query("SELECT COUNT(*) FROM cjj365_users")
            .then([&](auto state) {
              EXPECT_FALSE(state.has_error());
              auto result =
                  state.expect_one_row("Expected one row with count", 0, 0);
              EXPECT_TRUE(result.is_ok());
              EXPECT_EQ(result.value().at(0).as_int64(), 1);
              return IO<MysqlSessionState>::pure(std::move(state));
            });
      })
      .then(
          [&](auto) { return session->run_query("DELETE FROM cjj365_users;"); })
      .map([&](auto state) {
        auto rr = state.expect_affected_one_row("Expected one row deleted", 0);
        EXPECT_TRUE(rr.is_ok());
        return state;
      })
      .run([&](auto r) {
        EXPECT_TRUE(r.is_ok());
        notifier.notify();
      });

  notifier.waitForNotification();
  auto& ioc_manager = injector.create<cjj365::IoContextManager&>();
}

TEST(MonadMysqlTest, list_row_ok) {
  misc::ThreadNotifier notifier;
  using namespace monad;
  int rc = std::system(
      "dbmate --env-file db/.env_local drop && dbmate --env-file "
      "db/.env_local up");
  ASSERT_EQ(rc, 0) << "Failed to reset test database";
  // std::vector<fs::path> config_paths = {std::filesystem::current_path() /
  //                                       "config_dir"};
  // static cjj365::ConfigSources sources{config_paths, {"develop"}};
  // static customio::ConsoleOutputWithColor output{4};

  auto injector = di::make_injector(
      di::bind<sql::IMysqlConfigProvider>().to<sql::MysqlConfigProviderFile>(),
      di::bind<cjj365::ConfigSources>().to(config_sources()),
      di::bind<customio::IOutput>().to(output()),
      bind_shared_factory<monad::MonadicMysqlSession>(),
      di::bind<cjj365::IIocConfigProvider>()
          .to<cjj365::IocConfigProviderFile>());

  auto session_factory = injector.create<monad::MonadicMysqlSession::Factory>();
  auto session = session_factory();

  session
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
        notifier.notify();
      });

  notifier.waitForNotification();
  auto& ioc_manager = injector.create<cjj365::IoContextManager&>();
}

TEST(MonadMysqlTest, list_row_out_of_bounds) {
  misc::ThreadNotifier notifier;
  using namespace monad;
  int rc = std::system(
      "dbmate --env-file db/.env_local drop && dbmate --env-file "
      "db/.env_local up");
  ASSERT_EQ(rc, 0) << "Failed to reset test database";

  // std::vector<fs::path> config_paths = {std::filesystem::current_path() /
  //                                       "config_dir"};
  // static cjj365::ConfigSources sources{config_paths, {"develop"}};
  // static customio::ConsoleOutputWithColor output{4};

  auto injector = di::make_injector(
      di::bind<sql::IMysqlConfigProvider>().to<sql::MysqlConfigProviderFile>(),
      di::bind<customio::IOutput>().to(output()),
      di::bind<cjj365::ConfigSources>().to(config_sources()),
      bind_shared_factory<monad::MonadicMysqlSession>(),
      di::bind<cjj365::IIocConfigProvider>()
          .to<cjj365::IocConfigProviderFile>());

  auto session_factory = injector.create<monad::MonadicMysqlSession::Factory>();
  auto session = session_factory();

  session
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
        notifier.notify();
      });

  notifier.waitForNotification();
  auto& ioc_manager = injector.create<cjj365::IoContextManager&>();
}

TEST(MonadMysqlTest, sql_failed) {
  misc::ThreadNotifier notifier;
  using namespace monad;
  int rc = std::system(
      "dbmate --env-file db/.env_local drop && dbmate --env-file "
      "db/.env_local up");
  ASSERT_EQ(rc, 0) << "Failed to reset test database";

  // std::vector<fs::path> config_paths = {std::filesystem::current_path() /
  //                                       "config_dir"};
  // static cjj365::ConfigSources sources{config_paths, {"develop"}};
  // static customio::ConsoleOutputWithColor output{4};

  auto injector = di::make_injector(
      di::bind<sql::IMysqlConfigProvider>().to<sql::MysqlConfigProviderFile>(),
      di::bind<customio::IOutput>().to(output()),
      di::bind<cjj365::ConfigSources>().to(config_sources()),
      bind_shared_factory<monad::MonadicMysqlSession>(),
      di::bind<cjj365::IIocConfigProvider>()
          .to<cjj365::IocConfigProviderFile>());

  auto session_factory = injector.create<monad::MonadicMysqlSession::Factory>();
  auto session = session_factory();

  session->run_query("SELECT x* FROM cjj365_users;").run([&](auto r) {
    auto rr = r.value().expect_one_row("Expect fail", 0, 0);
    EXPECT_TRUE(rr.is_err());
    EXPECT_EQ(rr.error().code, db_errors::SQL_EXEC::SQL_FAILED);
    notifier.notify();
  });

  notifier.waitForNotification();
  auto& ioc_manager = injector.create<cjj365::IoContextManager&>();
}

TEST(MonadMysqlTest, maybe_one_row) {
  misc::ThreadNotifier notifier;
  using namespace monad;
  int rc = std::system(
      "dbmate --env-file db/.env_local drop && dbmate --env-file "
      "db/.env_local up");
  ASSERT_EQ(rc, 0) << "Failed to reset test database";

  auto injector = di::make_injector(
      di::bind<sql::IMysqlConfigProvider>().to<sql::MysqlConfigProviderFile>(),
      di::bind<cjj365::ConfigSources>().to(config_sources()),
      di::bind<customio::IOutput>().to(output()),
      bind_shared_factory<monad::MonadicMysqlSession>(),
      di::bind<cjj365::IIocConfigProvider>()
          .to<cjj365::IocConfigProviderFile>());

  auto session_factory = injector.create<monad::MonadicMysqlSession::Factory>();
  auto session = session_factory();

  session->run_query("SELECT * FROM cjj365_users WHERE id = 1")
      .then([&](auto state) {
        // Test case: No rows
        DEBUG_PRINT("[debug] 1");
        auto result = state.maybe_one_row(0, 0);
        EXPECT_TRUE(result.is_ok());
        EXPECT_FALSE(result.value().has_value());
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .then([&](auto) {
        DEBUG_PRINT("[debug] 2");
        return session->run_query(
            "INSERT INTO cjj365_users (name, email, password, roles, state) "
            "VALUES ('jianglibo', 'jianglibo@hotmail.com', 'password123', "
            "JSON_ARRAY('user', 'admin', 'notallowed'), 'active');");
      })
      .then([&](auto state) {
        DEBUG_PRINT("[debug] 3");
        EXPECT_FALSE(state.has_error());
        return session->run_query("SELECT id FROM cjj365_users WHERE id = 1");
      })
      .then([&](auto state) {
        // Test case: One row
        DEBUG_PRINT("[debug] 4");
        auto result = state.maybe_one_row(0, 0);
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
        return session->run_query(
            "INSERT INTO cjj365_users (name, email, password, roles) "
            "VALUES ('testuser2', 'test2@test.com', 'password', "
            "JSON_ARRAY('user'));");
      })
      .then([&](auto state) {
        DEBUG_PRINT("[debug] 6");
        EXPECT_FALSE(state.has_error()) << state.diagnostics();
        return session->run_query("SELECT * FROM cjj365_users");
      })
      .then([&](auto state) {
        // Test case: Multiple rows
        DEBUG_PRINT("[debug] 7");
        auto result = state.maybe_one_row(0, 0);
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code, db_errors::SQL_EXEC::MULTIPLE_RESULTS);
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .then([&](auto) {
        DEBUG_PRINT("[debug] 8");
        return session->run_query(
            "SELECT name, NULL as email FROM cjj365_users WHERE id = 1");
      })
      .then([&](auto state) {
        DEBUG_PRINT("[debug] 9");
        // Test case: NULL value in specified column
        auto result =
            state.maybe_one_row(0, 1);  // column 1 is email, which is NULL
        EXPECT_TRUE(result.is_ok());
        EXPECT_FALSE(result.value().has_value());
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .run([&](auto r) {
        if (r.is_err()) {
          std::cerr << "Final error: " << r.error() << std::endl;
        }
        EXPECT_TRUE(r.is_ok());
        notifier.notify();
      });

  notifier.waitForNotification();
  auto& ioc_manager = injector.create<cjj365::IoContextManager&>();
}

#include <gtest/gtest.h>  // Add this line

#include <boost/asio/io_context.hpp>
#include <variant>

#include "ioc_provider.hpp"
#include "misc_util.hpp"
#include "mysql_base.hpp"
#include "mysql_monad.hpp"
#include "tutil.hpp"  // IWYU pragma: keep

struct MockMysqlConfigProvider : public sql::IMysqlConfigProvider {
  MockMysqlConfigProvider(const sql::MysqlConfig& config) : config_(config) {}

  const sql::MysqlConfig& get() const override { return config_; }

 private:
  sql::MysqlConfig config_;
};

static std::string mysql_config_str = R"""(
   {
    "host": "home-t630.cjj365.cc",
    "ca_str": "",
    "cert_str": "",
    "cert_key_str": "",
    "port": 3306,
    "username": "user_run_tests",
    "password": "Iek5roe0ohv1eifohrahVi0woomoh9oWiev0phei3Te8soo6ohgie7Chauneeng7",
    "database": "cjj365_test",
    "ssl": 0,
    "multi_queries": true,
    "thread_safe": true,
    "unix_socket": "",
    "username_socket": "",
    "password_socket": ""
  }
)""";

TEST(MonadMysqlTest, only_one_row) {
  misc::ThreadNotifier notifier;
  using namespace monad;
  int rc = std::system(
      "dbmate --env-file db/.env_local drop && dbmate --env-file "
      "db/.env_local up");
  ASSERT_EQ(rc, 0) << "Failed to reset test database";
  asio::io_context ioc{};
  std::string after_replace_env = t::replace_all_env_vars(mysql_config_str, {});
  std::cerr << "after_replace_env: " << after_replace_env << std::endl;
  sql::MysqlConfig mc =
      json::value_to<sql::MysqlConfig>(json::parse(after_replace_env));
  MockMysqlConfigProvider config_provider(mc);
  ioc::DummyIocProvider ioc_provider;
  sql::MysqlPoolWrapper mysql_pool(ioc_provider, config_provider);

  auto session = std::make_shared<monad::MonadicMysqlSession>(mysql_pool);

  session
      ->run_query(
          "INSERT INTO cjj365_users (name, email, password, roles, state) "
          "VALUES ('jianglibo', 'jianglibo@hotmail.com', 'password123', "
          "JSON_ARRAY('user', 'admin', 'notallowed'), 'active')")
      .then([&](auto state) {
        if (state.has_error()) {
          std::cerr << "Error inserting user: " << state.diagnostics()
                    << std::endl;
        }
        EXPECT_FALSE(state.has_error());
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .then([&](auto state) {
        return session->run_query("SELECT COUNT(*) FROM cjj365_users")
            .then([&](auto state) {
              EXPECT_FALSE(state.has_error());
              auto result = state.expect_one_row("Expected one row with count",
                                                 0, 0);  // row_view
              EXPECT_TRUE(result.is_ok());
              EXPECT_EQ(result.value().at(0).as_int64(), 1);
              return IO<MysqlSessionState>::pure(std::move(state));
            });
      })
      .then([&](auto state) {
        return session->run_query("DELETE FROM cjj365_users;");
      })
      .map([&](auto state) {
        auto rr = state.expect_affected_one_row("Expected one row with count",
                                                0);  // row_view
        EXPECT_TRUE(rr.is_ok());
        return state;
      })
      .run([&, session](auto r) {
        EXPECT_TRUE(r.is_ok());
        std::cerr << "Query result: " << r.value().results.size() << std::endl;
        ioc.stop();
      });
  ioc.run();
}

TEST(MonadMysqlTest, list_row_ok) {
  misc::ThreadNotifier notifier;
  using namespace monad;
  int rc = std::system(
      "dbmate --env-file db/.env_local drop && dbmate --env-file "
      "db/.env_local up");
  ASSERT_EQ(rc, 0) << "Failed to reset test database";
  asio::io_context ioc{};
  std::string after_replace_env = t::replace_all_env_vars(mysql_config_str, {});
  std::cerr << "after_replace_env: " << after_replace_env << std::endl;
  sql::MysqlConfig mc =
      json::value_to<sql::MysqlConfig>(json::parse(after_replace_env));
  MockMysqlConfigProvider config_provider(mc);

  ioc::DummyIocProvider ioc_provider;
  sql::MysqlPoolWrapper mysql_pool(ioc_provider, config_provider);

  auto session = std::make_shared<monad::MonadicMysqlSession>(mysql_pool);
  session
      ->run_query(
          "SELECT * FROM cjj365_users;SELECT COUNT(*) FROM cjj365_users;")
      .then([&](auto state) {
        EXPECT_FALSE(state.has_error());
        auto result = state.expect_list_of_rows("Expected one row with count",
                                                0, 1);  // row_view
        EXPECT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().first.rows().size(), 0);
        EXPECT_EQ(result.value().second, 0);
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .run([&, session](auto r) {
        EXPECT_TRUE(r.is_ok());
        std::cerr << "Query result: " << r.value().results.size() << std::endl;
        ioc.stop();
      });
  ioc.run();
}

TEST(MonadMysqlTest, list_row_out_of_bounds) {
  misc::ThreadNotifier notifier;
  using namespace monad;
  int rc = std::system(
      "dbmate --env-file db/.env_local drop && dbmate --env-file "
      "db/.env_local up");
  ASSERT_EQ(rc, 0) << "Failed to reset test database";
  asio::io_context ioc{};
  std::string after_replace_env = t::replace_all_env_vars(mysql_config_str, {});
  std::cerr << "after_replace_env: " << after_replace_env << std::endl;
  sql::MysqlConfig mc =
      json::value_to<sql::MysqlConfig>(json::parse(after_replace_env));
  MockMysqlConfigProvider config_provider(mc);
  ioc::DummyIocProvider ioc_provider;
  sql::MysqlPoolWrapper mysql_pool(ioc_provider, config_provider);

  auto session = std::make_shared<monad::MonadicMysqlSession>(mysql_pool);
  session
      ->run_query(
          "SELECT * FROM cjj365_users;SELECT COUNT(*) FROM cjj365_users;")
      .then([&](auto state) {
        auto result =
            state.expect_list_of_rows("Expected one row with count", 0,
                                      2);  // 2 is out of index. row_view
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(result.error().code,
                  db_errors::SQL_EXEC::INDEX_OUT_OF_BOUNDS);
        return IO<MysqlSessionState>::pure(std::move(state));
      })
      .run([&, session](auto r) {
        EXPECT_FALSE(r.is_err());
        ioc.stop();
      });
  ioc.run();
}

TEST(MonadMysqlTest, sql_failed) {
  misc::ThreadNotifier notifier;
  using namespace monad;
  int rc = std::system(
      "dbmate --env-file db/.env_local drop && dbmate --env-file "
      "db/.env_local up");
  ASSERT_EQ(rc, 0) << "Failed to reset test database";
  asio::io_context ioc{};
  std::string after_replace_env = t::replace_all_env_vars(mysql_config_str, {});
  std::cerr << "after_replace_env: " << after_replace_env << std::endl;
  sql::MysqlConfig mc =
      json::value_to<sql::MysqlConfig>(json::parse(after_replace_env));
  MockMysqlConfigProvider config_provider(mc);
  ioc::DummyIocProvider ioc_provider;
  sql::MysqlPoolWrapper mysql_pool(ioc_provider, config_provider);

  auto session = std::make_shared<monad::MonadicMysqlSession>(mysql_pool);
  session->run_query("SELECT x* FROM cjj365_users;").run([&, session](auto r) {
    auto rr = r.value().expect_one_row("Expected one row with count", 0, 0);
    EXPECT_TRUE((rr.is_err()));
    EXPECT_EQ(rr.error().code, db_errors::SQL_EXEC::SQL_FAILED);
    std::cerr << "Query error: " << rr.error().what << std::endl;
    ioc.stop();
  });
  ioc.run();
}

TEST(MonadMysqlTest, configProvider) {
  sql::MysqlConfig config;
  config.host = "localhost";
  config.port = 3306;
  config.username = "user";
  config.password = "password";
  config.database = "test_db";
  config.thread_safe = true;

  MockMysqlConfigProvider config_provider(config);
  const sql::MysqlConfig& retrieved_config = config_provider.get();

  EXPECT_EQ(retrieved_config.host, config.host);
  EXPECT_EQ(retrieved_config.port, config.port);
  EXPECT_EQ(retrieved_config.username, config.username);
  EXPECT_EQ(retrieved_config.password, config.password);
  EXPECT_EQ(retrieved_config.database, config.database);
  EXPECT_EQ(retrieved_config.thread_safe, config.thread_safe);
}
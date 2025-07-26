#include <gtest/gtest.h>  // Add this line

#include <boost/asio/io_context.hpp>
#include <variant>

#include "misc_util.hpp"
#include "mysql_base.hpp"
#include "mysql_monad.hpp"
#include "tutil.hpp"  // IWYU pragma: keep

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
  sql::MysqlPoolWrapper mysql_pool(ioc, mc);

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
              auto result = state.only_one_row(
                  "Expected one row with count");  // row_view
              EXPECT_TRUE(result.is_ok());
              EXPECT_EQ(result.value().at(0).as_int64(), 1);
              return IO<MysqlSessionState>::pure(std::move(state));
            });
      })
      .then([&](auto state) {
        return session->run_query("DELETE FROM cjj365_users;");
      })
      .map([&](auto state) {
        auto rr = state.affected_only_one_row("Expected one row with count",
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
  sql::MysqlPoolWrapper mysql_pool(ioc, mc);

  auto session = std::make_shared<monad::MonadicMysqlSession>(mysql_pool);
  session
      ->run_query(
          "SELECT * FROM cjj365_users;SELECT COUNT(*) FROM cjj365_users;")
      .then([&](auto state) {
        EXPECT_FALSE(state.has_error());
        auto result =
            state.list_rows("Expected one row with count", 0, 1);  // row_view
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
  sql::MysqlPoolWrapper mysql_pool(ioc, mc);

  auto session = std::make_shared<monad::MonadicMysqlSession>(mysql_pool);
  session
      ->run_query(
          "SELECT * FROM cjj365_users;SELECT COUNT(*) FROM cjj365_users;")
      .then([&](auto state) {
        auto result =
            state.list_rows("Expected one row with count", 0, 2);  // row_view
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
  sql::MysqlPoolWrapper mysql_pool(ioc, mc);

  auto session = std::make_shared<monad::MonadicMysqlSession>(mysql_pool);
  session->run_query("SELECT x* FROM cjj365_users;").run([&, session](auto r) {
    auto rr = r.value().only_one_row("Expected one row with count");
    EXPECT_TRUE((rr.is_err()));
    EXPECT_EQ(rr.error().code, db_errors::SQL_EXEC::SQL_FAILED);
    std::cerr << "Query error: " << rr.error().what << std::endl;
    ioc.stop();
  });
  ioc.run();
}
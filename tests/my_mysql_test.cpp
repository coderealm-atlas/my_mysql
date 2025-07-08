#include <gtest/gtest.h>  // Add this line

#include <boost/asio/io_context.hpp>
#include <variant>

#include "misc_util.hpp"
#include "mysql_base.hpp"
#include "mysql_monad.hpp"
#include "tutil.hpp"  // IWYU pragma: keep

static std::string mysql_config_str = R"""(
   {
    "host": "mysql_tail.cjj365.cc",
    "ca_str": "${MYSQL_CA}",
    "cert_str": "${MYSQL_CERT}",
    "cert_key_str": "${MYSQL_CERT_KEY}",
    "port": 3306,
    "username": "${MYSQL_USER}",
    "password": "${MYSQL_SECRET}",
    "database": "cjj365_test",
    "ssl": 2,
    "multi_queries": true,
    "thread_safe": true,
    "unix_socket": "",
    "username_socket": "",
    "password_socket": ""
  }
)""";

TEST(UserServiceTest, create_user) {
  misc::ThreadNotifier notifier;
  using namespace monad;
  // int rc = std::system(
  //     "dbmate --env-file db/.env_local drop && dbmate --env-file "
  //     "db/.env_local up");
  // ASSERT_EQ(rc, 0) << "Failed to reset test database";
  asio::io_context ioc{};
  std::string after_replace_env = t::replace_all_env_vars(mysql_config_str, {});
  std::cerr << "after_replace_env: " << after_replace_env << std::endl;
  sql::MysqlConfig mc =
      json::value_to<sql::MysqlConfig>(json::parse(after_replace_env));
  sql::MysqlPoolWrapper mysql_pool(ioc, mc);
  json::value user_json =
      json::object{{"name", "jianglibo"},
                   {"email", "jianglibo@hotmail.com"},
                   {"password", "password123"},
                   {"roles", json::array{"user", "admin", "notallowed"}}};
  ioc.run();
  notifier.waitForNotification();
}

// TEST(UserServiceTest, list_users) {
//   cjj365::misc::ThreadNotifier notifier;
//   using namespace monad;
//   using cjj365::meta::User;

//   // Reset test database
//   int rc = std::system(
//       "dbmate --env-file db/.env_local drop && dbmate --env-file "
//       "db/.env_local up");
//   ASSERT_EQ(rc, 0) << "Failed to reset test database";

//   cjj365::AppConfigCommon ac = load_app_config_instance();
//   bbdb::sql::MysqlPoolWrapper mysql_pool(ac);
//   const auto injector = di::make_injector(
//       base_injector(ac, mysql_pool),
//       di::bind<service::IUserService>().to<dbservice::UserServiceMysql>().in(
//           di::unique));
//   auto user_service =
//   injector.create<std::shared_ptr<service::IUserService>>();
//   ASSERT_TRUE(user_service != nullptr);

//   // First create some test users
//   std::vector<json::value> test_users = {
//       json::object{{"name", "user1"},
//                    {"email", "user1@test.com"},
//                    {"password", "pass1"}},
//       json::object{{"name", "user2"},
//                    {"email", "user2@test.com"},
//                    {"password", "pass2"}},
//       json::object{{"name", "user3"},
//                    {"email", "user3@test.com"},
//                    {"password", "pass3"}}};

//   IO<void> start = IO<void>::pure();
//   for (size_t i = 0; i < test_users.size(); ++i) {
//     start = start.then([&, i]() -> IO<void> {
//       DEBUG_PRINT("Creating user " << i + 1 << " " <<
//       test_users[i].at("name")); return
//       user_service->create_user(std::move(test_users[i]))
//           .then([&](User user) {
//             // DEBUG_PRINT("Created user " << i + 1 << " "
//             //                             << test_users[i].at("name"));
//             return IO<void>::pure();
//           });
//     });
//   }

//   start
//       .then([&]() {
//         // Now test list_users
//         return user_service->list_users(0, 10, json::object{});
//       })
//       .map([](cjj365::ListResult<User> result) {
//         EXPECT_GE(result.meta.total, 3);
//         EXPECT_EQ(result.size(), 3);
//         // Could add more specific checks here
//         return result;
//       })
//       .catch_then([](const Error& err) {
//         ADD_FAILURE() << "Failed to list users: " << err.what;
//         return IO<cjj365::ListResult<User>>::pure({});
//       })
//       .run([&notifier](auto result) { notifier.notify(); });

//   notifier.waitForNotification();
//   ac.stop();
// }

// TEST(UserServiceTest, get_user) {
//   cjj365::misc::ThreadNotifier notifier;
//   using namespace monad;
//   using cjj365::meta::User;

//   // Reset test database (same as before)
//   int rc = std::system(
//       "dbmate --env-file db/.env_local drop && dbmate --env-file
//       db/.env_local " "up");
//   ASSERT_EQ(rc, 0);

//   cjj365::AppConfigCommon ac = load_app_config_instance();
//   bbdb::sql::MysqlPoolWrapper mysql_pool(ac);
//   const auto injector = di::make_injector(
//       base_injector(ac, mysql_pool),
//       di::bind<service::IUserService>().to<dbservice::UserServiceMysql>().in(
//           di::unique));
//   auto user_service =
//   injector.create<std::shared_ptr<service::IUserService>>();
//   ASSERT_TRUE(user_service != nullptr);

//   using OptionalUserIO = monad::IO<std::optional<User>>;
//   using OpUser = std::optional<User>;
//   // Create a test user first
//   json::value user_json = json::object{{"name", "testuser"},
//                                        {"email", "test@example.com"},
//                                        {"password", "secret"}};

//   user_service->create_user(std::move(user_json))
//       .then([&](User created_user) {
//         // Test get by ID
//         return user_service->get_user(created_user.id(), false);
//       })
//       .map([](OpUser user) {
//         EXPECT_EQ(user->name(), "testuser");
//         EXPECT_EQ(user->email(), "test@example.com");
//         EXPECT_NE(user->password(), "");  // password should be included
//         return user;
//       })
//       .then([&](OpUser user) {
//         // Test get by email
//         return user_service->get_user(user->email(), true);
//       })
//       .map([](OpUser user) {
//         EXPECT_EQ(user->name(), "testuser");
//         EXPECT_EQ(user->password(), "");  // password should be erased
//         return user;
//       })
//       .catch_then([](const Error& err) {
//         ADD_FAILURE() << "Failed in get_user test: " << err.what;
//         return OptionalUserIO::pure(User());
//       })
//       .run([&notifier](auto result) { notifier.notify(); });

//   notifier.waitForNotification();
//   ac.stop();
// }

// TEST(UserServiceTest, update_user) {
//   cjj365::misc::ThreadNotifier notifier;
//   using namespace monad;
//   using cjj365::meta::User;

//   // Reset test database
//   int rc = std::system(
//       "dbmate --env-file db/.env_local drop && dbmate --env-file
//       db/.env_local " "up");
//   ASSERT_EQ(rc, 0);

//   cjj365::AppConfigCommon ac = load_app_config_instance();
//   bbdb::sql::MysqlPoolWrapper mysql_pool(ac);
//   const auto injector = di::make_injector(
//       base_injector(ac, mysql_pool),
//       di::bind<service::IUserService>().to<dbservice::UserServiceMysql>().in(
//           di::unique));
//   auto user_service =
//   injector.create<std::shared_ptr<service::IUserService>>();
//   ASSERT_TRUE(user_service != nullptr);

//   // Create a test user first
//   json::value user_json = json::object{{"name", "original"},
//                                        {"email", "original@test.com"},
//                                        {"password", "original"}};

//   using OptionalUserIO = monad::IO<std::optional<User>>;
//   using OpUser = std::optional<User>;
//   user_service->create_user(std::move(user_json))
//       .then([&](User user) {
//         // Prepare updates
//         json::value updates = json::object{{"name", "updated"},
//                                            {"email", "updated@test.com"},
//                                            {"roles", json::array{"admin"}}};
//         DEBUG_PRINT("pppppppppppppppppp1");
//         return user_service->update_user(user.id(), std::move(updates));
//       })
//       .map([](json::object result) {
//         // Check the update result
//         EXPECT_EQ(result["name"].as_string(), "updated");
//         EXPECT_EQ(result["email"].as_string(), "updated@test.com");
//         // Could add more checks here
//         return result;
//       })
//       .then([&](json::object) {
//         // Verify the update by getting the user again
//         return user_service->get_user("updated@test.com", false);
//       })
//       .map([](OpUser user) {
//         EXPECT_EQ(user->name(), "updated");
//         EXPECT_EQ(user->email(), "updated@test.com");
//         return user;
//       })
//       .catch_then([](const Error& err) {
//         ADD_FAILURE() << "Failed in update_user test: " << err.what;
//         return OptionalUserIO::pure(User());
//       })
//       .run([&notifier](auto result) { notifier.notify(); });

//   notifier.waitForNotification();
//   ac.stop();
// }

// monad::IO<void> chain_users_lazy(
//     std::shared_ptr<dbservice::UserServiceMysql> user_service,
//     std::vector<json::value> users) {
//   auto users_ptr =
//   std::make_shared<std::vector<json::value>>(std::move(users)); auto
//   index_ptr = std::make_shared<size_t>(0);

//   std::function<monad::IO<void>()> loop;
//   loop = [index_ptr, users_ptr, user_service, &loop]() -> monad::IO<void> {
//     if (*index_ptr >= users_ptr->size()) {
//       return monad::IO<void>::pure();  // end of chain
//     }

//     size_t i = *index_ptr;
//     ++(*index_ptr);  // increment eagerly

//     DEBUG_PRINT("Creating user " << i + 1 << " " <<
//     (*users_ptr)[i].at("name"));

//     return user_service->create_user(std::move((*users_ptr)[i]))
//         .then([=](cjj365::meta::User) {
//           return loop();  // defer the next step
//         });
//   };

//   return monad::IO<void>::pure().then(loop);
// }

// TEST(UserServiceTest, delete_user) {
//   cjj365::misc::ThreadNotifier notifier;
//   using namespace monad;
//   using cjj365::meta::User;

//   // Reset test database
//   int rc = std::system(
//       "dbmate --env-file db/.env_local drop && dbmate --env-file
//       db/.env_local " "up");
//   ASSERT_EQ(rc, 0);

//   cjj365::AppConfigCommon ac = load_app_config_instance();
//   bbdb::sql::MysqlPoolWrapper mysql_pool(ac);
//   const auto injector = di::make_injector(
//       base_injector(ac, mysql_pool),
//       di::bind<service::IUserService>().to<dbservice::UserServiceMysql>().in(
//           di::unique));
//   auto user_service =
//   injector.create<std::shared_ptr<service::IUserService>>();
//   ASSERT_TRUE(user_service != nullptr);

//   // Create a test user first
//   json::value user_json = json::object{{"name", "todelete"},
//                                        {"email", "delete@test.com"},
//                                        {"password", "todelete"}};
//   using OptionalUserIO = monad::IO<std::optional<User>>;
//   using OpUser = std::optional<User>;
//   user_service->create_user(std::move(user_json))
//       .then([&](User user) {
//         // Delete the user
//         DEBUG_PRINT("user_id: " << user.id());
//         return user_service->delete_user(user.id());
//       })
//       .then([&]() {
//         // Verify deletion by trying to get the user
//         return user_service->get_user("delete@test.com", false);
//       })
//       .then([](OpUser user) {
//         EXPECT_FALSE(user.has_value());
//         return OptionalUserIO::pure(std::move(user));
//       })
//       .catch_then([](const Error& err) {
//         // We expect an error here since the user should be deleted
//         ADD_FAILURE() << "User should not exist after deletion";
//         return OptionalUserIO::fail(err);  // ✅ Return same IO<User> type
//       })
//       .run([&notifier](auto result) { notifier.notify(); });

//   notifier.waitForNotification();
//   ac.stop();
// }

// TEST(UserServiceTest, delete_user_1) {
//   cjj365::misc::ThreadNotifier notifier;
//   using namespace monad;
//   using cjj365::meta::User;

//   // Reset test database
//   int rc = std::system(
//       "dbmate --env-file db/.env_local drop && dbmate --env-file
//       db/.env_local " "up");
//   ASSERT_EQ(rc, 0);

//   using OptionalUserIO = monad::IO<std::optional<User>>;
//   using OpUser = std::optional<User>;
//   cjj365::AppConfigCommon ac = load_app_config_instance();
//   bbdb::sql::MysqlPoolWrapper mysql_pool(ac);
//   const auto injector = di::make_injector(
//       base_injector(ac, mysql_pool),
//       di::bind<service::IUserService>().to<dbservice::UserServiceMysql>().in(
//           di::unique));
//   auto user_service =
//   injector.create<std::shared_ptr<service::IUserService>>();
//   ASSERT_TRUE(user_service != nullptr);

//   user_service->get_user("delete@test.com", false)
//       .then([](OpUser user) {
//         EXPECT_FALSE(user.has_value());
//         return OptionalUserIO::pure(std::move(user));
//       })
//       .catch_then([](const Error& err) {
//         // We expect an error here since the user should be deleted
//         ADD_FAILURE() << "User should not exist after deletion";
//         return OptionalUserIO::fail(err);  // ✅ Return same IO<User> type
//       })
//       .run([&notifier](auto result) { notifier.notify(); });

//   notifier.waitForNotification();
//   ac.stop();
// }

// TEST(UserServiceTest, delete_user_2) {
//   cjj365::misc::ThreadNotifier notifier;
//   using namespace monad;
//   using cjj365::meta::User;

//   // Reset test database
//   int rc = std::system(
//       "dbmate --env-file db/.env_local drop && dbmate --env-file
//       db/.env_local " "up");
//   ASSERT_EQ(rc, 0);

//   cjj365::AppConfigCommon ac = load_app_config_instance();
//   bbdb::sql::MysqlPoolWrapper mysql_pool(ac);
//   const auto injector = di::make_injector(
//       base_injector(ac, mysql_pool),
//       di::bind<service::IUserService>().to<dbservice::UserServiceMysql>().in(
//           di::unique));
//   auto user_service =
//   injector.create<std::shared_ptr<service::IUserService>>();
//   ASSERT_TRUE(user_service != nullptr);

//   auto mysql_session =
//   injector.create<std::shared_ptr<MonadicMysqlSession>>();

//   mysql_session->run_query("SELECT * FROM cjj365_users WHERE id = 1")
//       .then([](MysqlSessionState state) {
//         if (state.has_error()) {
//           BOOST_LOG_TRIVIAL(error)
//               << "Failed to query user: " << state.error_message();
//           return IO<void>::fail(Error{1, "Query failed"});
//         }
//         EXPECT_EQ(state.result.size(), 1);
//         if (state.result.empty() || state.result[0].rows().empty()) {
//           BOOST_LOG_TRIVIAL(warning) << "No user found with ID 1";
//           return IO<void>::fail(Error{2, "User not found"});
//         }
//         return IO<void>::pure();
//       })
//       .run([&](auto r) { notifier.notify(); });
//   notifier.waitForNotification();
//   ac.stop();
// }
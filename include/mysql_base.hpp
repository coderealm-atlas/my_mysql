#pragma once

#include <boost/asio.hpp>      // IWYU pragma: keep
#include <boost/asio/ssl.hpp>  // IWYU pragma: keep
#include <boost/json.hpp>      // IWYU pragma: keep
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/move/utility_core.hpp>
#include <boost/mysql.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/url.hpp>  // IWYU pragma: keep
#include <filesystem>

#include "base64.h"
#include "mysql_base.hpp"

namespace ssl = boost::asio::ssl;  // from <boost/asio/ssl.hpp>
namespace asio = boost::asio;
namespace logging = boost::log;
namespace trivial = logging::trivial;
namespace logsrc = boost::log::sources;
namespace json = boost::json;
namespace fs = std::filesystem;
namespace mysql = boost::mysql;
using tcp = asio::ip::tcp;

namespace sql {

struct MysqlConfig {
  std::string host;
  int port;
  std::string username;
  std::string password;
  std::string database;
  bool thread_safe;
  std::string ca_str;
  std::string cert_str;
  std::string cert_key_str;
  int ssl;
  bool multi_queries;
  std::string unix_socket;
  std::string username_socket;
  std::string password_socket;

  friend MysqlConfig tag_invoke(const json::value_to_tag<MysqlConfig>&,
                                const json::value& jv) {
    std::vector<std::string> all_field_names = {"host",
                                                "port",
                                                "username",
                                                "password",
                                                "ca_str",
                                                "cert_str",
                                                "cert_key_str",
                                                "database",
                                                "ssl",
                                                "multi_queries",
                                                "unix_socket",
                                                "username_socket",
                                                "password_socket",
                                                "thread_safe"};
    for (const auto& field_name : all_field_names) {
      if (!jv.as_object().contains(field_name)) {
        throw std::runtime_error(field_name + " not found in json MysqlConfig");
      }
    }
    MysqlConfig mc;
    mc.host = json::value_to<std::string>(jv.at("host"));
    mc.port = jv.at("port").to_number<int>();
    mc.username = json::value_to<std::string>(jv.at("username"));
    mc.password = json::value_to<std::string>(jv.at("password"));
    mc.database = json::value_to<std::string>(jv.at("database"));
    mc.ca_str = json::value_to<std::string>(jv.at("ca_str"));
    mc.cert_str = json::value_to<std::string>(jv.at("cert_str"));
    mc.cert_key_str = json::value_to<std::string>(jv.at("cert_key_str"));
    mc.ssl = jv.at("ssl").to_number<int>();
    mc.multi_queries = jv.at("multi_queries").as_bool();
    mc.unix_socket = json::value_to<std::string>(jv.at("unix_socket"));
    mc.username_socket = json::value_to<std::string>(jv.at("username_socket"));
    mc.password_socket = json::value_to<std::string>(jv.at("password_socket"));
    mc.thread_safe = jv.at("thread_safe").as_bool();
    return mc;
  }

  friend void tag_invoke(json::value_from_tag, json::value& jv,
                         const MysqlConfig& mysqlConfig) {
    json::object jo;
    jo["host"] = mysqlConfig.host;
    jo["port"] = mysqlConfig.port;
    jo["username"] = mysqlConfig.username;
    jo["password"] = mysqlConfig.password;
    jo["database"] = mysqlConfig.database;
    jo["ca_str"] = mysqlConfig.ca_str;
    jo["cert_str"] = mysqlConfig.cert_str;
    jo["cert_key_str"] = mysqlConfig.cert_key_str;
    jo["ssl"] = mysqlConfig.ssl;
    jo["multi_queries"] = mysqlConfig.multi_queries;
    jo["unix_socket"] = mysqlConfig.unix_socket;
    jo["username_socket"] = mysqlConfig.username_socket;
    jo["password_socket"] = mysqlConfig.password_socket;
    jo["thread_safe"] = mysqlConfig.thread_safe;
    jv = std::move(jo);
  }
};

struct MysqlSessionState {
  boost::mysql::pooled_connection conn;
  boost::mysql::results result;
  boost::mysql::error_code error;
  boost::mysql::diagnostics diag;
  json::object updates;

  MysqlSessionState() = default;

  // Move constructor
  MysqlSessionState(MysqlSessionState&& other) noexcept
      : conn(std::move(other.conn)),
        result(std::move(other.result)),
        error(std::move(other.error)),
        diag(std::move(other.diag)),
        updates(std::move(other.updates)) {}

  // Move assignment
  MysqlSessionState& operator=(MysqlSessionState&& other) noexcept {
    conn = std::move(other.conn);
    result = std::move(other.result);
    error = std::move(other.error);
    diag = std::move(other.diag);
    updates = std::move(other.updates);
    return *this;
  }

  // Prevent copying
  MysqlSessionState(const MysqlSessionState&) = delete;
  MysqlSessionState& operator=(const MysqlSessionState&) = delete;

  bool has_error() const { return static_cast<bool>(error); }
  std::string error_message() const { return error.message(); }
};

inline mysql::pool_params params(const MysqlConfig& config) {
  mysql::pool_params params;
  /// var/run/mysqld/mysqld.sock
  // SHOW VARIABLES LIKE 'socket';
  if (config.unix_socket.empty()) {
    params.server_address.emplace_host_and_port(config.host, config.port);
    if (config.ssl > 0) {
      params.ssl = config.ssl == 0
                       ? mysql::ssl_mode::disable
                       : (config.ssl == 1 ? mysql::ssl_mode::enable
                                          : mysql::ssl_mode::require);
      ssl::context client_ssl_ctx{ssl::context::tlsv12};
      client_ssl_ctx.set_default_verify_paths();
      client_ssl_ctx.set_verify_mode(boost::asio::ssl::verify_peer);
      std::string ca_str = base64_decode(config.ca_str);
      std::string cert_str = base64_decode(config.cert_str);
      std::string cert_key_str = base64_decode(config.cert_key_str);

      client_ssl_ctx.add_certificate_authority(
          asio::const_buffer{ca_str.data(), ca_str.size()});
      client_ssl_ctx.use_certificate_chain(
          asio::const_buffer{cert_str.data(), cert_str.size()});
      client_ssl_ctx.use_private_key(
          asio::const_buffer{
              cert_key_str.data(),
              cert_key_str.size(),
          },
          ssl::context::file_format::pem);
      params.ssl_ctx = std::move(client_ssl_ctx);
    } else {
    }
    params.username = config.username;
    params.password = config.password;
  } else {
    params.server_address.emplace_unix_path(config.unix_socket);
    params.username = config.username_socket;
    params.password = config.password_socket;
  }

  params.database = config.database;
  params.thread_safe = config.thread_safe;
  params.multi_queries = config.multi_queries;
  return params;
}

struct MysqlPoolWrapper {
  MysqlPoolWrapper(asio::io_context& ioc, MysqlConfig& mysql_config)
      : pool_(ioc, params(mysql_config)) {
    pool_.async_run(asio::detached);
  }

  ~MysqlPoolWrapper() {}

  mysql::connection_pool& get() { return pool_; }

 private:
  mysql::connection_pool pool_;
};
}  // namespace sql

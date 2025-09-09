#pragma once

#include <boost/asio.hpp>  // IWYU pragma: keep
#include <boost/asio/io_context.hpp>
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
#include <boost/mysql/resultset_view.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/url.hpp>  // IWYU pragma: keep
#include <cstdint>

#include "base64.h"
#include "common_macros.hpp"
#include "db_errors.hpp"
#include "io_context_manager.hpp"
#include "mysql_config_provider.hpp"
#include "result_monad.hpp"

namespace ssl = boost::asio::ssl;  // from <boost/asio/ssl.hpp>
namespace asio = boost::asio;
namespace json = boost::json;
namespace mysql = boost::mysql;
using tcp = asio::ip::tcp;

namespace sql {

inline uint64_t epoch_milliseconds(boost::mysql::field_view f) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             f.as_datetime().as_time_point().time_since_epoch())
      .count();
}

struct MysqlSessionState {
  boost::mysql::pooled_connection conn;
  boost::mysql::results results;
  boost::mysql::error_code error;
  boost::mysql::diagnostics diag;
  json::object updates;

  MysqlSessionState() = default;

  // Move constructor
  MysqlSessionState(MysqlSessionState&& other) noexcept
      : conn(std::move(other.conn)),
        results(std::move(other.results)),
        error(std::move(other.error)),
        diag(std::move(other.diag)),
        updates(std::move(other.updates)) {}

  // Move assignment
  MysqlSessionState& operator=(MysqlSessionState&& other) noexcept {
    conn = std::move(other.conn);
    results = std::move(other.results);
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
  std::string diagnostics() const { return diag.server_message(); }

  monad::MyVoidResult expect_no_error(const std::string& message) {
    if (has_error()) {
      return monad::MyVoidResult::Err(
          monad::Error{db_errors::SQL_EXEC::SQL_FAILED, diagnostics()});
    }
    return monad::MyVoidResult();
  }

  monad::MyResult<mysql::row_view> expect_one_row(const std::string& message,
                                                  int result_index,
                                                  int id_column_index) {
    if (has_error()) {
      return monad::MyResult<mysql::row_view>::Err(
          monad::Error{db_errors::SQL_EXEC::SQL_FAILED, diagnostics()});
    }
    if (results.size() <= result_index) {
      return monad::MyResult<mysql::row_view>::Err(
          monad::Error{db_errors::SQL_EXEC::INDEX_OUT_OF_BOUNDS, message});
    }
    if (results[result_index].rows().empty()) {
      return monad::MyResult<mysql::row_view>::Err(
          monad::Error{db_errors::SQL_EXEC::NO_ROWS, message});
    }
    if (results[result_index].rows().size() != 1) {
      return monad::MyResult<mysql::row_view>::Err(
          monad::Error{db_errors::SQL_EXEC::MULTIPLE_RESULTS, message});
    }
    if (results[result_index].rows()[0].size() <= id_column_index) {
      std::string nm =
          std::format("{}, id column index {}", message, id_column_index);
      return monad::MyResult<mysql::row_view>::Err(
          monad::Error{db_errors::SQL_EXEC::INDEX_OUT_OF_BOUNDS, nm});
    }
    if (results[result_index].rows()[0].at(id_column_index).is_null()) {
      return monad::MyResult<mysql::row_view>::Err(
          monad::Error{db_errors::SQL_EXEC::NULL_ID, message});
    }
    return monad::MyResult<mysql::row_view>::Ok(
        results[result_index].rows()[0]);
  }

  monad::MyVoidResult expect_affected_one_row(const std::string& message,
                                              int result_index) {
    if (has_error()) {
      return monad::MyVoidResult::Err(
          monad::Error{db_errors::SQL_EXEC::SQL_FAILED, diagnostics()});
    }
    if (results.size() <= result_index) {
      return monad::MyVoidResult::Err(
          monad::Error{db_errors::SQL_EXEC::INDEX_OUT_OF_BOUNDS, message});
    }
    if (results[result_index].affected_rows() != 1) {
      return monad::MyVoidResult::Err(
          monad::Error{db_errors::SQL_EXEC::MULTIPLE_RESULTS, message});
    }
    return monad::MyVoidResult();
  }

  monad::MyResult<uint64_t> expect_affected_rows(const std::string& message,
                                                 int result_index) {
    if (has_error()) {
      return monad::MyResult<uint64_t>::Err(
          monad::Error{db_errors::SQL_EXEC::SQL_FAILED, diagnostics()});
    }
    if (results.size() <= result_index) {
      return monad::MyResult<uint64_t>::Err(
          monad::Error{db_errors::SQL_EXEC::INDEX_OUT_OF_BOUNDS, message});
    }
    return monad::MyResult<uint64_t>::Ok(results[result_index].affected_rows());
  }

  monad::MyResult<std::pair<mysql::resultset_view, int64_t>>
  expect_list_of_rows(const std::string& message, int rows_result_index,
                      int total_result_index) {
    using RtypeIO = monad::MyResult<std::pair<mysql::resultset_view, int64_t>>;
    if (has_error()) {
      return RtypeIO::Err(
          monad::Error{db_errors::SQL_EXEC::SQL_FAILED, diagnostics()});
    }
    if (results.size() <= rows_result_index ||
        results.size() <= total_result_index) {
      return RtypeIO::Err(
          monad::Error{db_errors::SQL_EXEC::INDEX_OUT_OF_BOUNDS, message});
    }
    auto rows_resultset = results[rows_result_index];
    if (rows_result_index == total_result_index) {
      // If both results are the same, we can return directly
      return RtypeIO::Ok(std::make_pair(std::move(rows_resultset),
                                        rows_resultset.rows().size()));
    }
    if (results[total_result_index].rows().empty()) {
      std::string nm = "missing total rows result in " + message;
      return RtypeIO::Err(monad::Error{db_errors::SQL_EXEC::NO_ROWS, nm});
    }
    uint64_t total = results[total_result_index].rows().at(0).at(0).as_int64();
    return RtypeIO::Ok(std::make_pair(std::move(rows_resultset), total));
  }

  monad::MyResult<std::pair<mysql::resultset_view, int64_t>>
  expect_all_list_of_rows(const std::string& message, int rows_result_index) {
    return expect_list_of_rows(message, rows_result_index, rows_result_index);
  }
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

// struct MysqlPoolWrapper {
//   MysqlPoolWrapper(asio::io_context& ioc,
//                    IMysqlConfigProvider& mysql_config_provider)
//       : pool_(ioc, params(mysql_config_provider.get())) {
//     pool_.async_run(asio::detached);
//     DEBUG_PRINT("[MysqlPoolWrapper] Constructor called.");
//   }

//   ~MysqlPoolWrapper() { DEBUG_PRINT("[MysqlPoolWrapper] Destructor called.");
//   }

//   void stop() {
//     pool_.cancel();  // ✅ cancel timers
//   }

//   mysql::connection_pool& get() { return pool_; }

//  private:
//   mysql::connection_pool pool_;
// };

struct MysqlPoolWrapper {
  MysqlPoolWrapper(cjj365::IoContextManager& ioc_manager,
                   IMysqlConfigProvider& mysql_config_provider)
      : pool_(ioc_manager.ioc(), params(mysql_config_provider.get())) {
    // Attach an error-reporting completion handler instead of asio::detached so
    // we don't silently swallow errors.
    pool_.async_run([this](const boost::system::error_code& ec) {
      if (ec) {
        DEBUG_PRINT("[MysqlPoolWrapper] async_run error: " << ec.message());
      } else {
        DEBUG_PRINT("[MysqlPoolWrapper] async_run exited cleanly.");
      }
    });
    DEBUG_PRINT("[MysqlPoolWrapper] Constructor called.");
  }

  // Non-copyable / non-movable to avoid multiple owners referencing the same
  // pool lifecycle implicitly.
  MysqlPoolWrapper(const MysqlPoolWrapper&) = delete;
  MysqlPoolWrapper& operator=(const MysqlPoolWrapper&) = delete;
  MysqlPoolWrapper(MysqlPoolWrapper&&) = delete;
  MysqlPoolWrapper& operator=(MysqlPoolWrapper&&) = delete;

  ~MysqlPoolWrapper() {
    stop();
    DEBUG_PRINT("[MysqlPoolWrapper] Destructor called.");
  }

  void stop() noexcept {
    if (!stopped_) {
      stopped_ = true;
      pool_.cancel();  // cancel timers / outstanding waits; connections return
                       // as they finish.
      DEBUG_PRINT("[MysqlPoolWrapper] stop() invoked.");
    }
  }

  mysql::connection_pool& get() { return pool_; }
  const mysql::connection_pool& get() const { return pool_; }

 private:
  mysql::connection_pool pool_;
  bool stopped_{false};
};
}  // namespace sql

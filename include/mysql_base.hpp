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
#include <numbers>

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
  struct TrackedPooledConn {
    mysql::pooled_connection inner;
    TrackedPooledConn() = default;
    TrackedPooledConn(mysql::pooled_connection&& pc) : inner(std::move(pc)) {}
    TrackedPooledConn(TrackedPooledConn&& o) noexcept
        : inner(std::move(o.inner)) {}
    TrackedPooledConn& operator=(TrackedPooledConn&& o) noexcept {
      inner = std::move(o.inner);
      return *this;
    }
    TrackedPooledConn(const TrackedPooledConn&) = delete;
    TrackedPooledConn& operator=(const TrackedPooledConn&) = delete;
    ~TrackedPooledConn() {
#ifdef BB_MYSQL_VERBOSE
      if (inner.valid()) {
        std::cerr
            << "[instrument][TrackedPooledConn] releasing pooled_connection"
            << std::endl;
      }
#endif
    }
    bool valid() const { return inner.valid(); }
    mysql::pooled_connection& get() { return inner; }
    mysql::pooled_connection* operator->() { return &inner; }
    const mysql::pooled_connection* operator->() const { return &inner; }
    mysql::pooled_connection&& move_out() { return std::move(inner); }
  };
  TrackedPooledConn conn;
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

  ~MysqlSessionState() {
#ifdef BB_MYSQL_VERBOSE
    std::cerr << "[instrument] MysqlSessionState dtor conn.valid="
              << (conn.valid() ? 1 : 0) << std::endl;
#endif
  }

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

  monad::MyResult<mysql::row_view> expect_one_row_cols_gt(
      const std::string& message, int cols) {
    if (has_error()) {
      return monad::MyResult<mysql::row_view>::Err(
          monad::Error{db_errors::SQL_EXEC::SQL_FAILED, diagnostics()});
    }
    // Iterate resultsets; return the first row whose column count is > cols.
    for (const auto& rs : results) {
      if (rs.rows().empty()) continue;  // skip empty sets
      const auto& row0 = rs.rows()[0];
      if (static_cast<int>(row0.size()) > cols) {
        return monad::MyResult<mysql::row_view>::Ok(row0);
      }
    }

    return monad::MyResult<mysql::row_view>::Err(
        monad::Error{db_errors::SQL_EXEC::NO_ROWS, message});
  }

  // Returns a BORROWED row_view. Must extract values before this state moves or
  // is destroyed.
  monad::MyResult<mysql::row_view> expect_one_row_borrowed(
      const std::string& message, int result_index, int id_column_index) {
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
    auto id_column = results[result_index].rows()[0].at(id_column_index);
    if (id_column.is_null()) {
      return monad::MyResult<mysql::row_view>::Err(
          monad::Error{db_errors::SQL_EXEC::NULL_ID, message});
    }
    return monad::MyResult<mysql::row_view>::Ok(
        results[result_index].rows()[0]);
  }

  monad::MyResult<std::optional<mysql::row_view>> maybe_one_row_borrowed(
      int result_index, int id_column_index) {
    return expect_one_row_borrowed("maybe_one_row_borrowed", result_index,
                                   id_column_index)
        .and_then([](mysql::row_view row) {
          return monad::MyResult<std::optional<mysql::row_view>>::Ok(
              std::make_optional(row));
        })
        .catch_then([this](monad::Error err) {
          DEBUG_PRINT("maybe_one_row_borrowed: caught error code "
                      << err.code << ", message: " << err.what);
          if (err.code == db_errors::SQL_EXEC::NO_ROWS ||
              err.code == db_errors::SQL_EXEC::NULL_ID) {
            return monad::MyResult<std::optional<mysql::row_view>>::Ok(
                std::nullopt);
          }
          return monad::MyResult<std::optional<mysql::row_view>>::Err(err);
        });
  }

  // visit_one_row
  // --------------------------------------------------------------------
  // Purpose:
  //   Safely transform the single required row into a value while the
  //   underlying buffers (inside this MysqlSessionState) are still alive.
  //   Prevents accidental storage of a dangling mysql::row_view.
  // Contract:
  //   - Executes expect_one_row_borrowed(message, result_index, id_column_index)
  //   - If that succeeds, invokes F(row_view) exactly once.
  //   - Returns MyResult<R> where R = invoke_result_t<F,row_view>.
  // Lifetime / Safety:
  //   - row_view is BORROWED. Do NOT store it or any field_view beyond the lambda.
  //   - Extract primitives (int64/string/blob copies) inside F and return them.
  // Error Propagation:
  //   - Any error from expect_one_row_borrowed propagates (SQL_FAILED, NO_ROWS, etc.).
  //   - F itself must be noexcept w.r.t. domain logic (use monadic conversions instead of throwing).
  // Common Use:
  //   return state.visit_one_row("wallet missing", 0, 0, [](mysql::row_view rv){
  //       Wallet w; w.id = rv.at(0).as_int64(); w.user_id = rv.at(1).as_int64(); return w;
  //   });
  // Why prefer over expect_one_row_borrowed + map:
  //   - Centralizes the borrow/consume pattern; reviewers instantly know row_view doesn't escape.
  //   - Reduces chance of accidentally returning the view or capturing it in outer scope.
  template <class F>
  auto visit_one_row(const std::string& message, int result_index,
                     int id_column_index, F&& f)
      -> monad::MyResult<std::invoke_result_t<F, mysql::row_view>> {
    using R = std::invoke_result_t<F, mysql::row_view>;
    return expect_one_row_borrowed(message, result_index, id_column_index)
        .map([&](mysql::row_view rv) {
          return std::invoke(std::forward<F>(f), rv);
        });
  }

  // visit_maybe_one_row
  // --------------------------------------------------------------------
  // Purpose:
  //   Like visit_one_row, but tolerant of absence. Produces optional<R>.
  // Semantics:
  //   - NO_ROWS or NULL_ID -> Ok(std::nullopt)
  //   - Other errors propagate.
  // Lifetime:
  //   - Same BORROWED constraints; extract inside F.
  // Example:
  //   return state.visit_maybe_one_row(0, 0, [](mysql::row_view rv){
  //       return IdVersion{ rv.at(0).as_int64(), (int)rv.at(1).as_int64() };
  //   });
  // Migration Tip:
  //   Replace maybe_one_row_borrowed(...).map(...) chains with this helper to
  //   reduce duplicated optional handling logic.
  template <class F>
  auto visit_maybe_one_row(int result_index, int id_column_index, F&& f)
      -> monad::MyResult<
          std::optional<std::invoke_result_t<F, mysql::row_view>>> {
    using R = std::invoke_result_t<F, mysql::row_view>;
    return maybe_one_row_borrowed(result_index, id_column_index)
        .map([&](std::optional<mysql::row_view> orv) -> std::optional<R> {
          if (!orv) return std::nullopt;
          return std::optional<R>(std::invoke(std::forward<F>(f), *orv));
        });
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

  monad::MyResult<int64_t> expect_count(const std::string& message,
                                        int result_index,
                                        int count_column_index = 0) {
    return expect_one_value<int64_t>(message, result_index, count_column_index);
  }

  template <typename T>
  monad::MyResult<T> expect_one_value(const std::string& message,
                                      int result_index, int column_index = 0) {
    using monad::MyResult;
    if (has_error()) {
      return MyResult<T>::Err(
          monad::Error{db_errors::SQL_EXEC::SQL_FAILED, diagnostics()});
    }
    if (results.size() <= result_index) {
      return MyResult<T>::Err(
          monad::Error{db_errors::SQL_EXEC::INDEX_OUT_OF_BOUNDS, message});
    }
    const auto& rs = results[result_index];
    if (rs.rows().empty()) {
      return MyResult<T>::Err(
          monad::Error{db_errors::SQL_EXEC::NO_ROWS, message});
    }
    const auto& row0 = rs.rows()[0];
    if (row0.size() <= column_index) {
      return MyResult<T>::Err(
          monad::Error{db_errors::SQL_EXEC::INDEX_OUT_OF_BOUNDS, message});
    }
    auto fv = row0.at(column_index);
    if (fv.is_null()) {
      return MyResult<T>::Err(
          monad::Error{db_errors::SQL_EXEC::NULL_ID, message});
    }

    // Type conversion based on T
    if constexpr (std::is_same_v<T, int64_t>) {
      if (fv.kind() == mysql::field_kind::int64) {
        return MyResult<int64_t>::Ok(fv.as_int64());
      } else if (fv.kind() == mysql::field_kind::uint64) {
        return MyResult<int64_t>::Ok(static_cast<int64_t>(fv.as_uint64()));
      }
      return MyResult<T>::Err(monad::Error{db_errors::PARSE::BAD_VALUE_ACCESS,
                                           message + ": expecting int64_t"});
    } else if constexpr (std::is_same_v<T, uint64_t>) {
      if (fv.kind() == mysql::field_kind::uint64) {
        return MyResult<uint64_t>::Ok(fv.as_uint64());
      } else if (fv.kind() == mysql::field_kind::int64) {
        auto v = fv.as_int64();
        if (v < 0) {
          return MyResult<T>::Err(
              monad::Error{db_errors::PARSE::BAD_VALUE_ACCESS,
                           message + ": negative to uint64_t"});
        }
        return MyResult<uint64_t>::Ok(static_cast<uint64_t>(v));
      }
      return MyResult<T>::Err(monad::Error{db_errors::PARSE::BAD_VALUE_ACCESS,
                                           message + ": expecting uint64_t"});
    } else if constexpr (std::is_same_v<T, double>) {
      if (fv.kind() == mysql::field_kind::double_) {
        return MyResult<double>::Ok(fv.as_double());
      }
      return MyResult<T>::Err(monad::Error{db_errors::PARSE::BAD_VALUE_ACCESS,
                                           message + ": expecting double"});
    } else if constexpr (std::is_same_v<T, bool>) {
      if (fv.kind() == mysql::field_kind::int64) {
        return MyResult<bool>::Ok(fv.as_int64() != 0);
      } else if (fv.kind() == mysql::field_kind::uint64) {
        return MyResult<bool>::Ok(fv.as_uint64() != 0);
      }
      return MyResult<T>::Err(
          monad::Error{db_errors::PARSE::BAD_VALUE_ACCESS,
                       message + ": expecting bool (tinyint)"});
    } else if constexpr (std::is_same_v<T, std::string>) {
      if (fv.kind() == mysql::field_kind::string) {
        return MyResult<std::string>::Ok(std::string(fv.as_string()));
      }
      return MyResult<T>::Err(monad::Error{db_errors::PARSE::BAD_VALUE_ACCESS,
                                           message + ": expecting string"});
    } else {
      // Unsupported type
      return MyResult<T>::Err(
          monad::Error{db_errors::PARSE::BAD_VALUE_ACCESS,
                       message + ": unsupported target type"});
    }
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
  // Explicitly set pool sizes for diagnostics
  // Set to 0 to avoid possible race during eager opening while diagnosing stall
  params.initial_size = config.initial_size;  // open on-demand
  params.max_size = config.max_size;          // allow up to 16
  // Instrument pool params
  std::cerr << "[instrument][pool_params] host="
            << (config.unix_socket.empty() ? config.host.c_str()
                                           : config.unix_socket.c_str())
            << " port=" << config.port << " db=" << config.database
            << " thread_safe=" << (params.thread_safe ? 1 : 0)
            << " multi_queries=" << (params.multi_queries ? 1 : 0)
            << " initial_size=" << params.initial_size
            << " max_size=" << params.max_size << std::endl;
  return params;
}

struct MysqlPoolWrapper {
  MysqlPoolWrapper(cjj365::IIoContextManager& ioc_manager,
                   IMysqlConfigProvider& mysql_config_provider)
      : pool_(ioc_manager.ioc(), params(mysql_config_provider.get())) {
    active_conns_.store(0);
    // Attach an error-reporting completion handler instead of asio::detached so
    // we don't silently swallow errors.
    pool_.async_run([this](const boost::system::error_code& ec) {
      if (ec) {
        DEBUG_PRINT("[MysqlPoolWrapper] async_run error: " << ec.message());
      } else {
        DEBUG_PRINT("[MysqlPoolWrapper] async_run exited cleanly.");
      }
    });
#ifdef BB_MYSQL_VERBOSE
    // Heartbeat to verify executor alive
    auto hb_timer = std::make_shared<asio::steady_timer>(pool_.get_executor());
    auto hb_fn = std::make_shared<std::function<void(int)>>();
    *hb_fn = [hb_timer, hb_fn](int iter) {
      hb_timer->expires_after(std::chrono::seconds(2));
      hb_timer->async_wait([hb_timer, hb_fn,
                            iter](const boost::system::error_code& ec) {
        if (ec) return;  // cancelled
        std::cerr << "[instrument][pool_heartbeat] iter=" << iter << std::endl;
        (*hb_fn)(iter + 1);
      });
    };
    (*hb_fn)(1);
#endif
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
  void inc_active() {
    auto v = active_conns_.fetch_add(1) + 1;
    // std::cerr << "[instrument][active_conns] + now=" << v << std::endl;
  }
  void dec_active() {
    auto v = active_conns_.fetch_sub(1) - 1;
    // std::cerr << "[instrument][active_conns] - now=" << v << std::endl;
  }
  int active() const { return active_conns_.load(); }

 private:
  mysql::connection_pool pool_;
  bool stopped_{false};
  std::atomic<int> active_conns_{0};
};
}  // namespace sql

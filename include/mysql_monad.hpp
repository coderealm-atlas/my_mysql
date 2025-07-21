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

#include "common_macros.hpp"
#include "io_monad.hpp"
#include "mysql_base.hpp"
#include "result_monad.hpp"

namespace asio = boost::asio;
namespace logging = boost::log;
namespace trivial = logging::trivial;
namespace logsrc = boost::log::sources;
namespace mysql = boost::mysql;
using tcp = asio::ip::tcp;

namespace monad {

using MysqlSessionState = sql::MysqlSessionState;
using MysqlPoolWrapper = sql::MysqlPoolWrapper;

class MonadicMysqlSession
    : public std::enable_shared_from_this<MonadicMysqlSession> {
 public:
  using Factory = std::function<std::shared_ptr<MonadicMysqlSession>()>;
  static inline std::atomic<int> instance_count{0};
  MonadicMysqlSession(MysqlPoolWrapper& pool)
      : pool_(pool),
        executor_(pool.get().get_executor()),
        strand_(asio::make_strand(executor_)) {
    ++instance_count;
    DEBUG_PRINT(
        "[MonadicMysqlSession +] instance_count = " << instance_count.load());
  }

  ~MonadicMysqlSession() {
    --instance_count;
    DEBUG_PRINT(
        "[MonadicMysqlSession -] instance_count = " << instance_count.load());
  }

  IO<MysqlSessionState> run_query(
      const std::string& sql,
      std::chrono::seconds timeout = std::chrono::seconds(5)) {
    return get_connection(timeout).then(
        [self = shared_from_this(), sql](MysqlSessionState state) mutable {
          if (state.has_error()) {
            return IO<MysqlSessionState>::pure(std::move(state));
          }
          return self->execute_sql(std::move(state), sql);
        });
  }

  IO<MysqlSessionState> run_query(
      std::function<MyResult<std::string>(mysql::pooled_connection&)>
          sql_generator,
      std::chrono::seconds timeout = std::chrono::seconds(5)) {
    return get_connection(timeout).then(
        [self = shared_from_this(), sql_generator = std::move(sql_generator)](
            MysqlSessionState state) mutable {
          if (state.has_error()) {
            return IO<MysqlSessionState>::fail(Error{1, state.error_message()});
          }
          auto sql = sql_generator(state.conn);
          if (sql.is_err()) {
            return IO<MysqlSessionState>::fail(std::move(sql.error()));
          }
          return self->execute_sql(std::move(state), sql.value());
        });
  }

 private:
  MysqlPoolWrapper& pool_;
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;
  logsrc::severity_logger<trivial::severity_level> lg;

  IO<MysqlSessionState> get_connection(std::chrono::seconds timeout) {
    return IO<MysqlSessionState>([self = shared_from_this(),
                                  timeout](IO<MysqlSessionState>::Callback cb) {
      self->pool_.get().async_get_connection(asio::cancel_after(
          timeout, asio::bind_executor(
                       self->strand_, [self, cb = std::move(cb)](
                                          boost::system::error_code ec,
                                          mysql::pooled_connection conn) {
                         MysqlSessionState state;
                         if (ec) {
                           BOOST_LOG_SEV(self->lg, trivial::error)
                               << "get_connection error: " << ec.message();
                           state.error = ec;
                         } else {
                           state.conn = std::move(conn);
                         }
                         cb(std::move(state));
                       })));
    });
  }

  IO<MysqlSessionState> execute_sql(MysqlSessionState state,
                                    const std::string& sql) {
    auto state_ptr = std::make_shared<MysqlSessionState>(std::move(state));

    return IO<MysqlSessionState>(
        [state_ptr, sql, self = shared_from_this()](auto cb) {
          state_ptr->conn->async_execute(
              sql, state_ptr->results, state_ptr->diag,
              [cb = std::move(cb), state_ptr](mysql::error_code ec) mutable {
                state_ptr->error = ec;
                cb(std::move(*state_ptr));  // move the object back out
              });
        });
  }
};

}  // namespace monad

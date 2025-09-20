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
#include "log_stream.hpp"
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

// Concurrency model:
//  - Each run_query() acquires a pooled connection, runs one statement, returns
//  it.
//  - No attempt is made to serialize queries submitted through the same
//    MonadicMysqlSession instance; they may run concurrently on different
//    pooled connections (subject to pool availability).
//  - There is no session-level transaction continuity or ordering guarantee.
//  - If you need ordered multi-statement workflows or transactions, introduce
//    a dedicated long-lived session variant that holds a single connection and
//    serializes operations (e.g., using a strand) instead of modifying this
//    class.
//  - Logging calls may interleave across threads; IOutput implementation must
//    be thread-safe if higher verbosity levels are enabled.
class MonadicMysqlSession
    : public std::enable_shared_from_this<MonadicMysqlSession> {
  MysqlPoolWrapper& pool_;
  asio::any_io_executor executor_;
  logsrc::severity_logger<trivial::severity_level> lg;
  customio::IOutput& output_;

 public:
  using Factory = std::function<std::shared_ptr<MonadicMysqlSession>()>;
  static inline std::atomic<int> instance_count{0};
  MonadicMysqlSession(MysqlPoolWrapper& pool, customio::IOutput& output)
      : pool_(pool), executor_(pool.get().get_executor()), output_(output) {
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
    static std::atomic<long long> qid_counter{0};
    long long qid = ++qid_counter;
    // Capture log stream locally to ensure lifetime extends across chained <<
    // operations. There have been intermittent crashes here
    // (RegisterStrongPasswordSucceeds test) indicating a potential lifetime or
    // UB issue when returning temporary LogStream. Defensive: wrap logging in
    // try/catch; logging must never crash query execution path.
    return get_connection(timeout).then(
        [self = shared_from_this(), sql, qid](MysqlSessionState state) mutable {
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
    static std::atomic<long long> qid_counter{0};
    long long qid = ++qid_counter;
#ifdef BB_MYSQL_VERBOSE
    std::cerr << "[instrument] run_query(gen) ENTER qid=" << qid
              << " timeout=" << timeout.count() << "s" << std::endl;
#endif
    return get_connection(timeout).then(
        [self = shared_from_this(), sql_generator = std::move(sql_generator),
         qid](MysqlSessionState state) mutable {
          if (state.has_error()) {
            return IO<MysqlSessionState>::fail(Error{1, state.error_message()});
          }
          const void* raw_conn_ptr =
              state.conn.valid() ? static_cast<const void*>(&state.conn.get())
                                 : nullptr;
#ifdef BB_MYSQL_VERBOSE
          std::cerr << "[instrument] qid=" << qid
                    << " acquired pooled_connection handle_addr="
                    << raw_conn_ptr << std::endl;
#endif
          auto sql = sql_generator(state.conn.get());
          if (sql.is_err()) {
            return IO<MysqlSessionState>::fail(std::move(sql.error()));
          }
          auto preview = sql.value().substr(0, 120);
          return self->execute_sql(std::move(state), sql.value());
        });
  }

 private:
  IO<MysqlSessionState> get_connection(std::chrono::seconds timeout) {
    return IO<MysqlSessionState>([self = shared_from_this(), timeout](auto cb) {
#ifdef BB_MYSQL_VERBOSE
      std::cerr << "[instrument] get_connection IO thunk start timeout="
                << timeout.count() << "s" << std::endl;
#endif
      // watchdog instrumentation to detect stall obtaining connection
      auto done_flag = std::make_shared<std::atomic<bool>>(false);
      auto start_tp = std::make_shared<std::chrono::steady_clock::time_point>(
          std::chrono::steady_clock::now());
      auto watchdog_timer = std::make_shared<asio::steady_timer>(
          self->pool_.get().get_executor());
      auto arm_watchdog = std::make_shared<std::function<void(int)>>();
      *arm_watchdog = [watchdog_timer, done_flag, start_tp,
                       arm_watchdog](int iter) mutable {
        if (done_flag->load()) return;
        watchdog_timer->expires_after(std::chrono::seconds(1));
        watchdog_timer->async_wait(
            [watchdog_timer, done_flag, start_tp, iter,
             arm_watchdog](const boost::system::error_code& ec) mutable {
              if (done_flag->load() || ec) return;
              auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::steady_clock::now() - *start_tp)
                                 .count();
#ifdef BB_MYSQL_VERBOSE
              std::cerr
                  << "[instrument][watchdog] async_get_connection pending iter="
                  << iter << " elapsed=" << elapsed << "s" << std::endl;
#endif
              (*arm_watchdog)(iter + 1);
            });
      };
      (*arm_watchdog)(1);
      // Manual timeout implementation (no cancel_after) now that root stall is
      // resolved.
      auto timeout_timer = std::make_shared<asio::steady_timer>(
          self->pool_.get().get_executor());
      timeout_timer->expires_after(timeout);
      timeout_timer->async_wait(
          [done_flag, cb,
           timeout_timer](const boost::system::error_code& ec) mutable {
            if (done_flag->load()) return;  // already completed
            if (ec) return;                 // cancelled
            std::cerr << "[instrument][timeout] get_connection exceeded timeout"
                      << std::endl;
            done_flag->store(true);
            MysqlSessionState state;
            state.error = boost::asio::error::timed_out;
            cb(IO<MysqlSessionState>::IOResult::Ok(std::move(state)));
          });

#ifdef BB_MYSQL_VERBOSE
      std::cerr
          << "[instrument] async_get_connection launching (no artificial delay)"
          << std::endl;
#endif
      self->pool_.get().async_get_connection(
          [self, cb = std::move(cb), done_flag, timeout_timer](
              boost::system::error_code ec,
              mysql::pooled_connection conn) mutable {
            if (done_flag->load()) {
              // raced with timeout; release connection immediately if obtained
              if (!ec && conn.valid()) {
#ifdef BB_MYSQL_VERBOSE
                std::cerr << "[instrument][race] connection arrived after "
                             "timeout; releasing"
                          << std::endl;
#endif
              }
              return;  // timeout already delivered
            }
#ifdef BB_MYSQL_VERBOSE
            std::cerr
                << "[instrument] get_connection completion handler invoked ec="
                << (ec ? ec.message() : "OK") << " (immediate path)"
                << std::endl;
#endif
            done_flag->store(true);
            timeout_timer->cancel();
            MysqlSessionState state;
            if (ec) {
              state.error = ec;
            } else {
              state.conn =
                  MysqlSessionState::TrackedPooledConn(std::move(conn));
              self->pool_.inc_active();
            }
            cb(IO<MysqlSessionState>::IOResult::Ok(std::move(state)));
          });
    });
  }

  IO<MysqlSessionState> execute_sql(MysqlSessionState state,
                                    const std::string& sql) {
    auto state_ptr = std::make_shared<MysqlSessionState>(std::move(state));
#ifdef BB_MYSQL_VERBOSE
    const void* raw_conn_ptr =
        state_ptr->conn.valid()
            ? static_cast<const void*>(&state_ptr->conn.get())
            : nullptr;
    std::cerr << "[instrument] execute_sql start conn_handle_addr="
              << raw_conn_ptr
              << " state_ptr.use_count=" << state_ptr.use_count() << std::endl;
    auto preview = sql.substr(0, 100);
#endif
    return IO<MysqlSessionState>([state_ptr, sql,
                                  self = shared_from_this()](auto cb) {
#ifdef BB_MYSQL_VERBOSE
      const void* raw_conn_ptr_inner =
          state_ptr->conn.valid()
              ? static_cast<const void*>(&state_ptr->conn.get())
              : nullptr;
      std::cerr << "[instrument] execute_sql dispatch conn_handle_addr="
                << raw_conn_ptr_inner
                << " state_ptr.use_count=" << state_ptr.use_count()
                << std::endl;
#endif
      state_ptr->conn.get()->async_execute(
          sql, state_ptr->results, state_ptr->diag,
          [cb = std::move(cb), state_ptr, self](mysql::error_code ec) mutable {
            state_ptr->error = ec;
#ifdef BB_MYSQL_VERBOSE
            const void* raw_conn_ptr_done =
                state_ptr->conn.valid()
                    ? static_cast<const void*>(&state_ptr->conn.get())
                    : nullptr;
            std::cerr << "[instrument] execute_sql completion conn_handle_addr="
                      << raw_conn_ptr_done
                      << " state_ptr.use_count=" << state_ptr.use_count()
                      << std::endl;
            // Only access results metadata if no error; calling size() on a
            // disengaged boost::mysql::results asserts (observed in billing test).
            std::size_t rs_count = 0;
            if (!ec) {
              try {
                rs_count = state_ptr->results.size();
              } catch (const std::exception& ex) {
                self->output_.error()
                    << "[execute_sql] unexpected exception querying results.size(): "
                    << ex.what();
              }
            } else {
              self->output_.error()
                  << "[execute_sql] completion error ec=" << ec.value()
                  << " msg=" << ec.message() << " diag="
                  << state_ptr->diag.server_message();
            }
            if (state_ptr->conn.valid()) {
              self->pool_.dec_active();
            }
#endif
            cb(IO<MysqlSessionState>::IOResult::Ok(
                std::move(*state_ptr)));  // move the object back out
          });
    });
  }
};

}  // namespace monad

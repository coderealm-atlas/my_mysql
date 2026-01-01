#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <thread>
#include <atomic>
#include <iostream>
#include <memory>

#include "openssl_thread_cleanup.hpp"

namespace cjj365 {

class MysqlIoContextManager {
public:
    MysqlIoContextManager()
        : state_(std::make_shared<State>()),
          stopped_(false)
    {
        // Launch exactly one thread that just runs this io_context forever
        // Thread captures owning state so that even if stop() is invoked from
        // within this thread (and we must detach), we won't UAF `this`.
        thread_ = std::thread([state = state_] {
            OpenSslThreadCleanup openssl_guard;
            try {
                auto count = state->ioc.run();
                std::cerr << "[MysqlIoContextManager] io_context stopped, run() count="
                          << count << "\n";
            } catch (const std::exception& e) {
                std::cerr << "[MysqlIoContextManager] io_context.run() exception: "
                          << e.what() << "\n";
            }
        });

        std::cerr << "[MysqlIoContextManager] started dedicated Redis IO thread\n";
    }

    boost::asio::io_context& ioc() { return state_->ioc; }

    void stop() {
        if (stopped_.exchange(true)) {
            return;
        }
        state_->work_guard.reset(); // allow io_context.run() to exit when no work
        state_->ioc.stop();

        if (thread_.joinable()) {
            // If stop() is called from the same thread, avoid self-join deadlock
            if (std::this_thread::get_id() == thread_.get_id()) {
                thread_.detach();
            } else {
                thread_.join();
            }
        }
    }

    ~MysqlIoContextManager() {
        stop();
        OpenSslThreadCleanup cleanup_guard;
    }

private:
    struct State {
        State()
            : ioc(1),
              work_guard(std::make_unique<boost::asio::executor_work_guard<
                             boost::asio::io_context::executor_type>>(
                  boost::asio::make_work_guard(ioc))) {}

        boost::asio::io_context ioc;
        std::unique_ptr<
            boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
            work_guard;
    };

    std::shared_ptr<State> state_;
    std::thread thread_;
    std::atomic<bool> stopped_;
};

} // namespace cjj365

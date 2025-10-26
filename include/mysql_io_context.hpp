#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <thread>
#include <atomic>
#include <iostream>

#include "openssl_thread_cleanup.hpp"

namespace cjj365 {

class MysqlIoContextManager {
public:
    MysqlIoContextManager()
        : ioc_(1),  // 1 = concurrency hint
          work_guard_(boost::asio::make_work_guard(ioc_)),
          stopped_(false)
    {
        // Launch exactly one thread that just runs this io_context forever
        thread_ = std::thread([this] {
            OpenSslThreadCleanup openssl_guard;
            try {
                auto count = ioc_.run();
                std::cerr << "[MysqlIoContextManager] io_context stopped, run() count="
                          << count << "\n";
            } catch (const std::exception& e) {
                std::cerr << "[MysqlIoContextManager] io_context.run() exception: "
                          << e.what() << "\n";
            }
        });

        std::cerr << "[MysqlIoContextManager] started dedicated Redis IO thread\n";
    }

    boost::asio::io_context& ioc() { return ioc_; }

    void stop() {
        if (stopped_.exchange(true)) {
            return;
        }
        work_guard_.reset(); // allow io_context.run() to exit when no work
        ioc_.stop();

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
    boost::asio::io_context ioc_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    std::thread thread_;
    std::atomic<bool> stopped_;
};

} // namespace cjj365

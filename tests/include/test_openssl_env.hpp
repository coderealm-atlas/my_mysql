#pragma once

#include <atomic>
#include <cstdlib>
#include <mutex>

#include <openssl/crypto.h>
#include <openssl/ssl.h>

namespace testinfra {

class OpenSslTestGlobalState {
 public:
  OpenSslTestGlobalState() { init_once(); }
  ~OpenSslTestGlobalState() { cleanup_once(); }

 private:
  static void init_once() {
    std::call_once(init_flag_, []() {
      OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr);
      OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr);
      std::atexit([]() {
        OpenSslTestGlobalState::cleanup_once();
      });
      initialized_.store(true, std::memory_order_release);
    });
  }

  static void cleanup_once() {
    bool expected = true;
    if (initialized_.compare_exchange_strong(expected, false,
                                             std::memory_order_acq_rel)) {
      OPENSSL_cleanup();
    }
  }

  static std::once_flag init_flag_;
  static std::atomic<bool> initialized_;
};

inline std::once_flag OpenSslTestGlobalState::init_flag_;
inline std::atomic<bool> OpenSslTestGlobalState::initialized_{false};

inline OpenSslTestGlobalState openssl_test_global_state_instance{};

}  // namespace testinfra

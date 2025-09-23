#include <benchmark/benchmark.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <memory>

#include "boost/di.hpp"
#include "common_macros.hpp"
#include "io_context_manager.hpp"
#include "log_stream.hpp"
#include "misc_util.hpp"
#include "my_di_extension.hpp"
#include "mysql_base.hpp"
#include "mysql_config_provider.hpp"
#include "mysql_monad.hpp"
#include "simple_data.hpp"
#include "tutil.hpp"

using namespace monad;
namespace di = boost::di;
namespace fs = std::filesystem;

// Helper functions (copied from integration test)
static cjj365::ConfigSources& config_sources() {
  static cjj365::ConfigSources instance({fs::path{"config_dir"}}, {"test", "develop"});
  return instance;
}

static customio::ConsoleOutputWithColor& output() {
  static customio::ConsoleOutputWithColor instance(4);
  return instance;
}

class SakilaBenchmark : public benchmark::Fixture {
protected:
  void SetUp(const ::benchmark::State& state) override {
    if (!injector_) {
      // Reset and migrate database (same as integration test)
      int rc = std::system(
          "dbmate --env-file db/.env_test --migrations-dir db/test_migrations "
          "drop && dbmate --env-file "
          "db/.env_test --migrations-dir db/test_migrations up");
      
      if (rc != 0) {
        throw std::runtime_error("Failed to setup test database");
      }

      // Create injector using void* to avoid complex template types
      auto real_injector = new auto(di::make_injector(
          di::bind<sql::IMysqlConfigProvider>().to<sql::MysqlConfigProviderFile>(),
          di::bind<cjj365::ConfigSources>().to(config_sources()),
          di::bind<customio::IOutput>().to(output()),
      di::bind<cjj365::IIoContextManager>().to<cjj365::IoContextManager>(),
          bind_shared_factory<monad::MonadicMysqlSession>(),
          di::bind<cjj365::IIocConfigProvider>()
              .to<cjj365::IocConfigProviderFile>()));
      
      injector_ = std::unique_ptr<void, std::function<void(void*)>>(
          real_injector, 
          [](void* ptr) { 
            delete static_cast<decltype(real_injector)>(ptr); 
          }
      );

      session_factory_ = static_cast<decltype(real_injector)>(injector_.get())
          ->create<monad::MonadicMysqlSession::Factory>();
      
      // Test that we can create a session (but don't store it - create fresh ones per benchmark)
      auto test_session = session_factory_();
      if (!test_session) {
        throw std::runtime_error("Failed to create MySQL session factory");
      }
    }
  }

  void TearDown(const ::benchmark::State& state) override {
    // No need to clean up individual sessions - they're created per benchmark
  }

protected:
  std::unique_ptr<void, std::function<void(void*)>> injector_;
  monad::MonadicMysqlSession::Factory session_factory_;
  
  // Helper method to create a fresh session for each benchmark
  std::shared_ptr<monad::MonadicMysqlSession> createSession() {
    return session_factory_();
  }
};

// Simple SELECT benchmark
BENCHMARK_F(SakilaBenchmark, SimpleSelect)(benchmark::State& state) {
  for (auto _ : state) {
    bool completed = false;
    
    // Create a fresh session for each benchmark iteration
    auto session = createSession();
    
    session->run_query("SELECT COUNT(*) FROM film")
        .then([&](auto state) {
          auto result = state.expect_one_row_borrowed("Expected film count", 0, 0);
          benchmark::DoNotOptimize(result);
          completed = true;
          return IO<MysqlSessionState>::pure(std::move(state));
        })
        .run([&](auto r) {
          // Complete
        });
    
    // Wait for completion
    while (!completed) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  }
}

// JOIN query benchmark  
BENCHMARK_F(SakilaBenchmark, JoinQuery)(benchmark::State& state) {
  for (auto _ : state) {
    bool completed = false;
    
    // Create a fresh session for each benchmark iteration
    auto session = createSession();
    
    session->run_query(
        "SELECT f.title, c.name "
        "FROM film f "
        "JOIN film_category fc ON f.film_id = fc.film_id "
        "JOIN category c ON fc.category_id = c.category_id "
        "LIMIT 10")
        .then([&](auto state) {
          // Just access the result to ensure query executed
          benchmark::DoNotOptimize(state.results.rows().size());
          completed = true;
          return IO<MysqlSessionState>::pure(std::move(state));
        })
        .run([&](auto r) {
          // Complete
        });
    
    // Wait for completion
    while (!completed) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  }
}

BENCHMARK_MAIN();
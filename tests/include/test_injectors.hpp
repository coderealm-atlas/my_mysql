#pragma once
#include <filesystem>
#include <cstdlib>
#include "boost/di.hpp"
#include "mysql_config_provider.hpp"
#include "io_context_manager.hpp"
#include "my_di_extension.hpp"
#include "mysql_monad.hpp"
#include "log_stream.hpp"

namespace di = boost::di;
namespace fs = std::filesystem;

// Shared helpers for tests (unit + integration)
namespace test_injectors {

inline cjj365::ConfigSources& shared_config_sources() {
  static cjj365::ConfigSources instance({fs::path{"config_dir"}}, {"test", "develop"});
  return instance;
}

inline int compute_log_level() {
  if (const char* lvl = std::getenv("TEST_LOG_LEVEL")) {
    try {
      int v = std::stoi(lvl);
      if (v < 0) v = 0; if (v > 6) v = 6; // clamp reasonable range
      return v;
    } catch (...) { /* fallthrough */ }
  }
  return 4; // default
}

inline customio::ConsoleOutputWithColor& shared_output() {
  static customio::ConsoleOutputWithColor instance(compute_log_level());
  return instance;
}

inline auto build_base_injector() {
  return di::make_injector(
      di::bind<cjj365::ConfigSources>().to(shared_config_sources()),
      di::bind<customio::IOutput>().to(shared_output()),
      di::bind<cjj365::IIoContextManager>().to<cjj365::IoContextManager>(),
      di::bind<sql::IMysqlConfigProvider>().to<sql::MysqlConfigProviderFile>().in(di::singleton),
      di::bind<sql::MysqlPoolWrapper>().in(di::singleton),
      di_utils::safe_factory_binding<monad::MonadicMysqlSession, sql::MysqlPoolWrapper, customio::IOutput>(),
      di::bind<cjj365::IIocConfigProvider>().to<cjj365::IocConfigProviderFile>()
  );
}

// Specialized builders (if future divergence needed)
inline auto build_unit_test_injector() { return build_base_injector(); }
inline auto build_integration_test_injector() { return build_base_injector(); }

} // namespace test_injectors

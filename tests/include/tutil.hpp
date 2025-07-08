#pragma once
#include <boost/asio.hpp>      // IWYU pragma: keep
#include <boost/asio/ssl.hpp>  // IWYU pragma: keep
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace ssl = boost::asio::ssl;  // from <boost/asio/ssl.hpp>
namespace fs = std::filesystem;

namespace t {

inline std::unique_ptr<ssl::context> client_ssl_ctx() {
  auto ctx = std::make_unique<ssl::context>(ssl::context::tlsv12);
  ctx->set_default_verify_paths();
  return ctx;
}

// Helper function to replace ${VARIABLE} or ${VARIABLE:-default} with the
// environment variable
inline std::string replace_env_var(
    const std::string& input,
    const std::map<std::string, std::string>& extra_map) {
  std::string output = input;

  // Basic parsing for ${VARIABLE} or ${VARIABLE:-default} patterns
  size_t start = output.find("${");
  size_t end = output.find('}', start);
  if (start != std::string::npos && end != std::string::npos) {
    std::string env_var = output.substr(start + 2, end - start - 2);
    std::cerr << "ENV NAME: " << env_var << std::endl;
    std::string default_val;

    // Check for the ":-" syntax for default values
    size_t delim = env_var.find(":-");
    if (delim != std::string::npos) {
      default_val = env_var.substr(delim + 2);
      env_var = env_var.substr(0, delim);
    }

    if (extra_map.find(env_var) != extra_map.end()) {
      output.replace(start, end - start + 1, extra_map.at(env_var));
    } else {
      // Substitute environment variable or default
      const char* env_value = std::getenv(env_var.c_str());
      if (env_value) {
        output.replace(start, end - start + 1, env_value);
      } else {
        return default_val;
      }
    }
  }
  return output;
}

inline std::string replace_all_env_vars(
    const std::string& input,
    const std::map<std::string, std::string>& extra_map) {
  std::string output = input;
  size_t start = 0;

  while ((start = output.find("${", start)) != std::string::npos) {
    size_t end = output.find('}', start);
    if (end == std::string::npos) break;  // malformed placeholder, exit

    std::string expr = output.substr(start + 2, end - start - 2);
    std::string default_val;
    std::string key = expr;

    // Handle default value syntax
    size_t delim = expr.find(":-");
    if (delim != std::string::npos) {
      key = expr.substr(0, delim);
      default_val = expr.substr(delim + 2);
    }

    std::string replacement;

    if (auto it = extra_map.find(key); it != extra_map.end()) {
      replacement = it->second;
    } else if (const char* env = std::getenv(key.c_str()); env) {
      replacement = env;
    } else {
      replacement = default_val;
    }

    output.replace(start, end - start + 1, replacement);
    start += replacement.size();  // advance to avoid infinite loop
  }

  return output;
}

inline std::map<std::string, std::string> parse_envrc(const fs::path& envrc) {
  // #export CMAKE_HOME=/opt/cmake
  // export HARBOR_SECRET=aekuXaeph3cohdohje9vohN5iasikeDa
  // export RABBIT_USER=La5ye0goo1miunuesooDaig8tieph1me
  std::string line;
  std::map<std::string, std::string> env;
  std::ifstream ifs(envrc);
  if (!ifs) {
    std::cerr << "Failed to open " << envrc << std::endl;
    return env;
  }
  while (std::getline(ifs, line)) {
    size_t pos = line.find_first_not_of(' ');
    if (pos == std::string::npos || line[pos] == '#') continue;
    pos = line.find("export ", pos);
    if (pos == std::string::npos) {
      std::cerr << "Invalid line: " << line << std::endl;
      continue;
    }
    pos = line.find_first_not_of(' ', pos + 7);
    if (pos == std::string::npos) {
      std::cerr << "Invalid line: " << line << std::endl;
      continue;
    }
    size_t eq_pos = line.find('=', pos);
    if (eq_pos == std::string::npos) {
      std::cerr << "Invalid line: " << line << std::endl;
      continue;
    }
    std::string key = line.substr(pos, eq_pos - pos);
    pos = line.find_first_not_of(' ', eq_pos + 1);
    if (pos == std::string::npos) {
      std::cerr << "Invalid line: " << line << std::endl;
      continue;
    }
    std::string value = line.substr(pos);
    env[key] = value;
  }
  return env;
}

}  // namespace t
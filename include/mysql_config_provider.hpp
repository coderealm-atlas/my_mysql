#pragma once

#include <boost/json.hpp>

#include "i_output.hpp"
#include "json_util.hpp"
#include "log_stream.hpp"
#include "simple_data.hpp"

namespace json = boost::json;
namespace sql {

struct MysqlConfig {
  std::string host;
  int port;
  std::string username;
  std::string password;
  std::string database;
  bool thread_safe;
  std::string ca_str;
  std::string cert_str;
  std::string cert_key_str;
  int ssl;
  bool multi_queries;
  std::string unix_socket;
  std::string username_socket;
  std::string password_socket;

  friend MysqlConfig tag_invoke(const json::value_to_tag<MysqlConfig>&,
                                const json::value& jv) {
    std::vector<std::string> all_field_names = {"host",
                                                "port",
                                                "username",
                                                "password",
                                                "ca_str",
                                                "cert_str",
                                                "cert_key_str",
                                                "database",
                                                "ssl",
                                                "multi_queries",
                                                "unix_socket",
                                                "username_socket",
                                                "password_socket",
                                                "thread_safe"};
    for (const auto& field_name : all_field_names) {
      if (!jv.as_object().contains(field_name)) {
        throw std::runtime_error(field_name + " not found in json MysqlConfig");
      }
    }
    MysqlConfig mc;
    mc.host = json::value_to<std::string>(jv.at("host"));
    mc.port = jv.at("port").to_number<int>();
    mc.username = json::value_to<std::string>(jv.at("username"));
    mc.password = json::value_to<std::string>(jv.at("password"));
    mc.database = json::value_to<std::string>(jv.at("database"));
    mc.ca_str = json::value_to<std::string>(jv.at("ca_str"));
    mc.cert_str = json::value_to<std::string>(jv.at("cert_str"));
    mc.cert_key_str = json::value_to<std::string>(jv.at("cert_key_str"));
    mc.ssl = jv.at("ssl").to_number<int>();
    mc.multi_queries = jv.at("multi_queries").as_bool();
    mc.unix_socket = json::value_to<std::string>(jv.at("unix_socket"));
    mc.username_socket = json::value_to<std::string>(jv.at("username_socket"));
    mc.password_socket = json::value_to<std::string>(jv.at("password_socket"));
    mc.thread_safe = jv.at("thread_safe").as_bool();
    return mc;
  }

  friend void tag_invoke(json::value_from_tag, json::value& jv,
                         const MysqlConfig& mysqlConfig) {
    json::object jo;
    jo["host"] = mysqlConfig.host;
    jo["port"] = mysqlConfig.port;
    jo["username"] = mysqlConfig.username;
    jo["password"] = mysqlConfig.password;
    jo["database"] = mysqlConfig.database;
    jo["ca_str"] = mysqlConfig.ca_str;
    jo["cert_str"] = mysqlConfig.cert_str;
    jo["cert_key_str"] = mysqlConfig.cert_key_str;
    jo["ssl"] = mysqlConfig.ssl;
    jo["multi_queries"] = mysqlConfig.multi_queries;
    jo["unix_socket"] = mysqlConfig.unix_socket;
    jo["username_socket"] = mysqlConfig.username_socket;
    jo["password_socket"] = mysqlConfig.password_socket;
    jo["thread_safe"] = mysqlConfig.thread_safe;
    jv = std::move(jo);
  }
};

class IMysqlConfigProvider {
 public:
  virtual ~IMysqlConfigProvider() = default;
  virtual const MysqlConfig& get() const = 0;
};

class MysqlConfigProviderFile : public IMysqlConfigProvider {
  MysqlConfig config_;
  customio::IOutput& output_;

 public:
  explicit MysqlConfigProviderFile(cjj365::AppProperties& app_properties,
                                   cjj365::ConfigSources& config_sources,
                                   customio::IOutput& output)
      : output_(output) {
    auto r = config_sources.json_content("mysql_config");
    if (r.is_err()) {
      output_.error() << "Failed to load MySQL config: " << r.error();
      throw std::runtime_error("Failed to load MySQL config.");
    }
    json::value jv = r.value();
    jsonutil::substitue_envs(jv, app_properties.properties);
    output_.debug() << "Loaded MySQL config: " << jv << std::endl;
    config_ = json::value_to<MysqlConfig>(std::move(jv));
  }

  const MysqlConfig& get() const override { return config_; }
};

}  // namespace sql
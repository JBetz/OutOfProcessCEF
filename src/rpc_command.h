#pragma once

#include <rpc.h>
#include <memory>
#include <string>
#include <optional>
#include <functional>
#include <unordered_map>

#include "include/cef_base.h"
#include "include/cef_browser.h"
#include "include/cef_urlrequest.h"
#include "include/cef_values.h"

class BrowserProcessHandler;

class RpcCommand {
 public:
  virtual ~RpcCommand() = default;
  std::optional<std::string> Initialize(const CefString& id,
                                        CefRefPtr<CefDictionaryValue> args);

 protected:
  CefString id;
  virtual void ReadArguments() = 0;

  template <typename T>
  void Require(const std::string& key, T& out) {
    if (!rawArgs || !rawArgs->HasKey(key)) {
      errors.push_back("Missing required field '" + key + "'");
      return;
    }
    if (!ExtractValue(rawArgs, key, out)) {
      errors.push_back("Field '" + key + "' has invalid type or format");
    }
  }

  template <typename T>
  void Optional(const std::string& key, std::optional<T>& out) {
    if (!rawArgs || !rawArgs->HasKey(key) ||
        rawArgs->GetType(key) == VTYPE_NULL) {
      out = std::nullopt;
      return;
    }
    T val{};
    if (ExtractValue(rawArgs, key, val)) {
      out = val;
    } else {
      errors.push_back("Optional field '" + key + "' has invalid type");
    }
  }

  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           bool& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           int& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           uint32_t& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           int64_t& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           double& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           CefString& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           std::string& v);

  template <typename EnumType>
    requires std::is_enum_v<EnumType>
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           EnumType& v) {
    if (dict->GetType(k) != VTYPE_INT)
      return false;
    v = static_cast<EnumType>(dict->GetInt(k));
    return true;
  }
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           CefPoint& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           CefRect& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           CefMouseEvent& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           std::map<CefString, CefString>& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           std::vector<CefString>& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           std::vector<CefRefPtr<CefPostDataElement>>& v);
  static bool ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                           const std::string& k,
                           CefRefPtr<CefDictionaryValue>& v);

 private:
  CefRefPtr<CefDictionaryValue> rawArgs;
  std::vector<std::string> errors;
};

template <typename BaseCommand>
class RpcRegistry {
 public:
  using FactoryFunction = std::function<std::unique_ptr<BaseCommand>()>;

  static bool Register(const std::string& name, FactoryFunction factory) {
    GetTable()[name] = std::move(factory);
    return true;
  }

  static std::unique_ptr<BaseCommand> Lookup(const std::string& name) {
    auto& table = GetTable();
    auto it = table.find(name);
    return (it != table.end()) ? it->second() : nullptr;
  }

 private:
  static std::unordered_map<std::string, FactoryFunction>& GetTable() {
    static std::unordered_map<std::string, FactoryFunction> table;
    return table;
  }
};

// C++20 inline static member ensures registration runs automatically
#define REGISTER_RPC_COMMAND(BaseType, MethodName, ClassName)                 \
  inline static const bool _is_registered_##ClassName =                       \
      RpcRegistry<BaseType>::Register(MethodName,                             \
                                      []() -> std::unique_ptr<BaseType> {     \
                                        return std::make_unique<ClassName>(); \
                                      });
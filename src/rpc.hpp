#pragma once

#include <rpc.h>
#include <optional>
#include <string>
#include <variant>
#include "include/internal/cef_types_wrappers.h"
#include "include/cef_v8.h"
#include <unordered_set>

// UUID
inline UUID CreateUuid() {
  UUID uuid;
  RPC_STATUS resultCode = UuidCreate(&uuid);
  if (resultCode == RPC_S_OK) {
    return uuid;
  } else {
    throw std::runtime_error("UuidCreate() failed with error code: " +
                             std::to_string(resultCode));
  }
}

inline std::string uuid_to_string(const UUID& m) {
  RPC_CSTR str;
  RPC_STATUS resultCode = UuidToStringA(const_cast<UUID*>(&m), &str);
  if (resultCode == RPC_S_OK) {
    std::string result = std::string(reinterpret_cast<char*>(str));
    RpcStringFreeA(&str);
    return result;
  } else {
    throw std::runtime_error("UuidToStringA() failed with error code: " +
                             std::to_string(resultCode));
  }
}

inline CefRefPtr<CefValue> CefV8ValueToCefValueInternal(
    CefRefPtr<CefV8Value> v8Value,
    std::unordered_set<int>& visited) {
  auto result = CefValue::Create();
  if (!v8Value || v8Value->IsNull() || v8Value->IsUndefined()) {
    result->SetNull();
    return result;
  }

  if (v8Value->IsBool()) {
    result->SetBool(v8Value->GetBoolValue());
    return result;
  }

  if (v8Value->IsInt()) {
    result->SetInt(v8Value->GetIntValue());
    return result;
  }

  if (v8Value->IsUInt()) {
    result->SetInt(static_cast<int>(v8Value->GetUIntValue()));
    return result;
  }

  if (v8Value->IsDouble()) {
    result->SetDouble(v8Value->GetDoubleValue());
    return result;
  }

  if (v8Value->IsString()) {
    result->SetString(v8Value->GetStringValue());
    return result;
  }

  if (v8Value->IsDate()) {
    result->SetDouble(v8Value->GetDateValue().val);
    return result;
  }

  if (v8Value->IsArray()) {
    auto list = CefListValue::Create();
    int len = v8Value->GetArrayLength();
    for (int i = 0; i < len; ++i) {
      CefRefPtr<CefV8Value> element = v8Value->GetValue(i);
      list->SetValue(i, CefV8ValueToCefValueInternal(element, visited));
    }
    result->SetList(list);
    return result;
  }

  if (v8Value->IsObject()) {
    auto dict = CefDictionaryValue::Create();
    std::vector<CefString> keys;
    v8Value->GetKeys(keys);

    for (const auto& key : keys) {
      CefRefPtr<CefV8Value> val = v8Value->GetValue(key);
      if (val && !val->IsFunction()) {
        dict->SetValue(key, CefV8ValueToCefValueInternal(val, visited));
      }
    }
    result->SetDictionary(dict);
    return result;
  }

  result->SetNull();
  return result;
}

inline CefRefPtr<CefValue> CefV8ValueToCefValue(CefRefPtr<CefV8Value> v8Value) {
  std::unordered_set<int> visited;
  return CefV8ValueToCefValueInternal(v8Value, visited);
}
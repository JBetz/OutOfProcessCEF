#include "rpc_command.h"

#include <optional>
#include <functional>
#include <unordered_map>

#include <include/base/cef_bind.h>
#include <include/wrapper/cef_closure_task.h>

#include "browser_process_handler.h"

std::optional<CefString> RpcCommand::Initialize(
    const CefString& id_,
    CefRefPtr<CefDictionaryValue> args) {
  this->id = id_;
  this->rawArgs = args;
  this->errors.clear();

  // Invoke derived class declarations
  this->ReadArguments();

  if (!errors.empty()) {
    std::ostringstream oss;
    for (size_t i = 0; i < errors.size(); ++i) {
      if (i > 0)
        oss << "; ";
      oss << errors[i];
    }
    return oss.str();
  }

  return std::nullopt;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              bool& v) {
  if (dict->GetType(k) != VTYPE_BOOL)
    return false;
  v = dict->GetBool(k);
  return true;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              int& v) {
  if (dict->GetType(k) != VTYPE_INT)
    return false;
  v = dict->GetInt(k);
  return true;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              uint32_t& v) {
  if (dict->GetType(k) != VTYPE_INT)
    return false;
  v = static_cast<uint32_t>(dict->GetInt(k));
  return true;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              int64_t& v) {
  if (dict->GetType(k) == VTYPE_INT) {
    v = dict->GetInt(k);
    return true;
  }
  if (dict->GetType(k) == VTYPE_DOUBLE) {
    v = static_cast<int64_t>(dict->GetDouble(k));
    return true;
  }
  return false;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              double& v) {
  if (dict->GetType(k) == VTYPE_DOUBLE) {
    v = dict->GetDouble(k);
    return true;
  }
  if (dict->GetType(k) == VTYPE_INT) {
    v = static_cast<double>(dict->GetInt(k));
    return true;
  }
  return false;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              CefString& v) {
  if (dict->GetType(k) != VTYPE_STRING)
    return false;
  v = dict->GetString(k);
  return true;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              CefPoint& v) {
  if (dict->GetType(k) != VTYPE_DICTIONARY)
    return false;
  auto d = dict->GetDictionary(k);
  if (!d->HasKey("x") || !d->HasKey("y"))
    return false;
  v.Set(d->GetInt("x"), d->GetInt("y"));
  return true;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              CefRect& v) {
  if (dict->GetType(k) != VTYPE_DICTIONARY)
    return false;
  auto d = dict->GetDictionary(k);
  if (!d->HasKey("x") || !d->HasKey("y") || !d->HasKey("width") ||
      !d->HasKey("height"))
    return false;
  v.Set(d->GetInt("x"), d->GetInt("y"), d->GetInt("width"),
        d->GetInt("height"));
  return true;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              CefMouseEvent& v) {
  if (dict->GetType(k) != VTYPE_DICTIONARY)
    return false;
  auto d = dict->GetDictionary(k);
  v.x = d->GetInt("x");
  v.y = d->GetInt("y");
  v.modifiers = static_cast<uint32_t>(d->GetInt("modifiers"));
  return true;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              std::map<CefString, CefString>& v) {
  if (dict->GetType(k) != VTYPE_DICTIONARY)
    return false;
  auto d = dict->GetDictionary(k);
  std::vector<CefString> keys;
  d->GetKeys(keys);
  v.clear();
  for (const auto& key : keys) {
    v[key] = d->GetString(key);
  }
  return true;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              std::vector<CefString>& v) {
  if (dict->GetType(k) != VTYPE_LIST)
    return false;
  auto l = dict->GetList(k);
  size_t count = l->GetSize();
  v.clear();
  v.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (l->GetType(i) == VTYPE_STRING) {
      v.push_back(l->GetString(i));
    }
  }
  return true;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              std::vector<CefRefPtr<CefPostDataElement>>& v) {
  if (dict->GetType(k) != VTYPE_LIST)
    return false;
  auto list = dict->GetList(k);
  size_t count = list->GetSize();
  v.clear();
  v.reserve(count);

  for (size_t i = 0; i < count; ++i) {
    if (list->GetType(i) != VTYPE_DICTIONARY)
      continue;
    auto elemDict = list->GetDictionary(i);

    auto element = CefPostDataElement::Create();
    auto type = static_cast<CefPostDataElement::Type>(elemDict->GetInt("type"));

    switch (type) {
      case PDE_TYPE_EMPTY: {
        element->SetToEmpty();
        break;
      }
      case PDE_TYPE_FILE: {
        if (elemDict->HasKey("fileName") &&
            elemDict->GetType("fileName") == VTYPE_STRING) {
          element->SetToFile(elemDict->GetString("fileName"));
        }
        break;
      }
      case PDE_TYPE_BYTES: {
        if (elemDict->HasKey("bytes") &&
            elemDict->GetType("bytes") == VTYPE_LIST) {
          auto byteList = elemDict->GetList("bytes");
          size_t byteCount = byteList->GetSize();
          std::vector<uint8_t> buffer(byteCount);
          for (size_t j = 0; j < byteCount; ++j) {
            buffer[j] = static_cast<uint8_t>(byteList->GetInt(j));
          }
          element->SetToBytes(buffer.size(), buffer.data());
        }
        break;
      }
    }

    v.push_back(element);
  }
  return true;
}

bool RpcCommand::ExtractValue(CefRefPtr<CefDictionaryValue> dict,
                              const CefString& k,
                              CefRefPtr<CefDictionaryValue>& v) {
  if (dict->GetType(k) != VTYPE_DICTIONARY)
    return false;
  v = dict->GetDictionary(k);
  return true;
}
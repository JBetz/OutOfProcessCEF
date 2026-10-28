#include "http_request_client.h"

#include "browser_process_handler.h"
#include "rpc.hpp"
#include <include/cef_parser.h>

HttpRequestClient::HttpRequestClient(BrowserProcessHandler* handler,
                                     int httpRequestId)
    : handler(handler), httpRequestId(httpRequestId) {}

void HttpRequestClient::SendRpcRequest(
    std::string methodName,
    CefRefPtr<CefDictionaryValue> arguments) {
  CefString requestId = uuid_to_string(CreateUuid());
  CefRefPtr<CefValue> request = CefValue::Create();
  CefRefPtr<CefDictionaryValue> payload = CefDictionaryValue::Create();
  payload->SetString("id", requestId);
  payload->SetString("class", "HttpRequest");
  payload->SetString("method", methodName);
  payload->SetInt("instanceId", httpRequestId);
  payload->SetDictionary("arguments", arguments);
  request->SetDictionary(payload);
  CefString jsonRequest = CefWriteJSON(request, JSON_WRITER_DEFAULT);
  handler->SendMessage(jsonRequest);
}

void HttpRequestClient::OnRequestComplete(CefRefPtr<CefURLRequest> request) {
  handler->RemoveHttpRequest(httpRequestId);
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
 
  if (request->GetRequestStatus() == UR_SUCCESS &&
      request->GetRequestError() == ERR_NONE) {
    CefRefPtr<CefResponse> response = request->GetResponse();
    arguments->SetInt("statusCode", response->GetStatus());
    arguments->SetString("statusText", response->GetStatusText());
    CefResponse::HeaderMap headerMap;
    response->GetHeaderMap(headerMap);
    CefRefPtr<CefDictionaryValue> headers = CefDictionaryValue::Create();
    for (const auto& [k, v] : headerMap) {
      headers->SetString(k, v);
    }
    arguments->SetDictionary("headers", headers);
    this->SendRpcRequest("OnResponse", arguments);
  } else {
    arguments->SetInt("requestStatus", request->GetRequestStatus());
    arguments->SetInt("errorCode", request->GetRequestError());
    this->SendRpcRequest("OnError", arguments);
  }
}

void HttpRequestClient::OnDownloadData(CefRefPtr<CefURLRequest> request,
                                       const void* data,
                                       size_t data_length) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetBinary("data", CefBinaryValue::Create(data, data_length));
  this->SendRpcRequest("OnData", arguments);
}

bool HttpRequestClient::GetAuthCredentials(bool isProxy,
                                           const CefString& host,
                                           int port,
                                           const CefString& realm,
                                           const CefString& scheme,
                                           CefRefPtr<CefAuthCallback> callback) {
  return false;
}

void HttpRequestClient::OnUploadProgress(CefRefPtr<CefURLRequest> request,
                                         int64_t current,
                                         int64_t total) {}

void HttpRequestClient::OnDownloadProgress(CefRefPtr<CefURLRequest> request,
                                           int64_t current,
                                           int64_t total) {}
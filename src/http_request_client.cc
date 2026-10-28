#include "http_request_client.h"

#include "browser_process_handler.h"
#include "rpc.hpp"
#include <include/cef_parser.h>

HttpRequestClient::HttpRequestClient(BrowserProcessHandler* handler,
                                     int httpRequestId)
    : handler(handler), httpRequestId(httpRequestId) {}


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
    handler->SendRequest("HttpRequest", httpRequestId, "OnResponse", arguments);
  } else {
    arguments->SetInt("requestStatus", request->GetRequestStatus());
    arguments->SetInt("errorCode", request->GetRequestError());
    handler->SendRequest("HttpRequest", httpRequestId, "OnError", arguments);
  }
}

void HttpRequestClient::OnDownloadData(CefRefPtr<CefURLRequest> request,
                                       const void* data,
                                       size_t data_length) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetBinary("data", CefBinaryValue::Create(data, data_length));
  handler->SendRequest("HttpRequest", httpRequestId, 
                       "OnDownloadData", arguments);
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
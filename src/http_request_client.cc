#include "http_request_client.h"

#include "browser_process_handler.h"
#include "json.hpp"
#include "rpc.hpp"

using json = nlohmann::json;

HttpRequestClient::HttpRequestClient(BrowserProcessHandler* handler,
                                     int httpRequestId)
    : handler(handler), httpRequestId(httpRequestId) {}

void HttpRequestClient::OnRequestComplete(CefRefPtr<CefURLRequest> request) {
  handler->RemoveHttpRequest(httpRequestId);

  RpcRequest notification;
  notification.id = CreateUuid();
  notification.className = "HttpRequest";
  notification.instanceId = httpRequestId;

  if (request->GetRequestStatus() == UR_SUCCESS &&
      request->GetRequestError() == ERR_NONE) {
    CefRefPtr<CefResponse> cefResponse = request->GetResponse();
    HttpClient_OnResponse response;
    response.statusCode = cefResponse->GetStatus();
    response.statusText = cefResponse->GetStatusText().ToString();
    CefResponse::HeaderMap headerMap;
    cefResponse->GetHeaderMap(headerMap);
    for (const auto& [k, v] : headerMap) {
      response.headers[k.ToString()] = v.ToString();
    }
    notification.methodName = "OnResponse";
    notification.arguments = response;
  } else {
    HttpClient_OnError error;
    error.requestStatus = request->GetRequestStatus();
    error.errorCode = request->GetRequestError();
    notification.methodName = "OnError";
    notification.arguments = error;
  }

  json jsonNotification = notification;
  handler->SendMessage(jsonNotification.dump());
}

void HttpRequestClient::OnDownloadData(CefRefPtr<CefURLRequest> request,
                                       const void* data,
                                       size_t data_length) {
  HttpClient_OnData chunk;
  chunk.data =
      std::vector<uint8_t>(static_cast<const uint8_t*>(data),
                           static_cast<const uint8_t*>(data) + data_length);

  RpcRequest notification;
  notification.id = CreateUuid();
  notification.className = "HttpRequest";
  notification.methodName = "OnData";
  notification.instanceId = httpRequestId;
  notification.arguments = chunk;

  json jsonNotification = notification;
  handler->SendMessage(jsonNotification.dump());
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
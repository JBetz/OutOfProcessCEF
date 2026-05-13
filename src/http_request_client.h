#pragma once

#include "include/cef_urlrequest.h"
#include "rpc.hpp"

class BrowserProcessHandler;

class HttpRequestClient : public CefURLRequestClient {
 public:
  HttpRequestClient(BrowserProcessHandler* handler, int httpRequestId);

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override;
  void OnDownloadData(CefRefPtr<CefURLRequest> request,
                      const void* data,
                      size_t data_length) override;
  bool GetAuthCredentials(bool isProxy,
                          const CefString& host,
                          int port,
                          const CefString& realm,
                          const CefString& scheme,
                          CefRefPtr<CefAuthCallback> callback) override;
  void OnUploadProgress(CefRefPtr<CefURLRequest> request,
                        int64_t current,
                        int64_t total) override;
  void OnDownloadProgress(CefRefPtr<CefURLRequest> request,
                          int64_t current,
                          int64_t total) override;

 private:
  BrowserProcessHandler* handler;
  int httpRequestId;

  IMPLEMENT_REFCOUNTING(HttpRequestClient);
};
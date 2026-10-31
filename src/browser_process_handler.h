#pragma once

#include <rpc.h>
#include <windows.h>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "include/base/cef_callback.h"
#include "include/cef_base.h"
#include "include/cef_browser_process_handler.h"
#include "include/cef_server.h"
#include "process_handler.h"
#include "rpc.hpp"

class BrowserHandler;

using PendingCallback = std::function<void(CefRefPtr<CefValue>)>;

class BrowserProcessHandler : public ProcessHandler,
                              public CefBrowserProcessHandler,
                              public CefServerHandler {
 public:
  BrowserProcessHandler(int serverPort,
                        HANDLE applicationProcessHandle,
                        std::optional<CefString> serverReadyEventName);
  ~BrowserProcessHandler() override;

  // Accessors.
  CefRefPtr<CefServer> GetServer();
  CefRefPtr<CefBrowser> GetBrowser(int browserId);
  CefRefPtr<BrowserHandler> GetBrowserHandler(int browserId);
  void RemoveBrowserHandler(int browserId);
  HANDLE GetApplicationProcessHandle();

  // CefBrowserProcessHandler methods.
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
  void OnContextInitialized() override;

  // CefServerHandler methods.
  void OnServerCreated(CefRefPtr<CefServer> server) override;
  void OnServerDestroyed(CefRefPtr<CefServer> server) override;
  void OnClientConnected(CefRefPtr<CefServer> server,
                         int connection_id) override;
  void OnClientDisconnected(CefRefPtr<CefServer> server,
                            int connection_id) override;
  void OnHttpRequest(CefRefPtr<CefServer> server,
                     int connection_id,
                     const CefString& client_address,
                     CefRefPtr<CefRequest> request) override;
  void OnWebSocketRequest(CefRefPtr<CefServer> server,
                          int connection_id,
                          const CefString& client_address,
                          CefRefPtr<CefRequest> request,
                          CefRefPtr<CefCallback> callback) override;
  void OnWebSocketConnected(CefRefPtr<CefServer> server,
                            int connection_id) override;
  void OnWebSocketMessage(CefRefPtr<CefServer> server,
                          int connection_id,
                          const void* data,
                          size_t data_size) override;

  // RPC handling (runs on TID_IO).
  void HandleRpcRequest(const CefString& requestId,
                        const CefString& className,
                        int instanceId,
                        const CefString& methodName,
                        CefRefPtr<CefDictionaryValue> arguments);
  void HandleRpcResponse(const CefString& requestId,
                         bool success,
                         CefRefPtr<CefValue> returnValue,
                         CefRefPtr<CefValue> error);
  void RegisterPendingCallback(const CefString& requestId,
                               PendingCallback callback);
  void CancelPendingCallback(const CefString& requestId);
  std::map<CefString, PendingCallback> GetPendingCallbacks();

  // Incoming RPC messages (dispatched to UI thread where required).
  void CreateBrowser(const CefString& requestId,
                     const CefString& url,
                     const CefPoint& windowOrigin,
                     const CefRect& rectangle,
                     HWND parentWindowHandle,
                     bool windowless,
                     bool hardwareAccelerated,
                     bool private_);
  void CreateHttpRequest(
      const CefString& requestId,
      const CefString& url,
      const CefString& method,
      const std::optional<CefRefPtr<CefDictionaryValue>> headers,
      const std::optional<CefString>& body);
  void Shutdown();
  void CloseBrowser(const CefRefPtr<CefBrowser> browser, bool forceClose);
  void TryCloseBrowser(const CefRefPtr<CefBrowser> browser,
                       const CefString& requestId);
  void GetBrowserFrameRate(const CefRefPtr<CefBrowser> browser,
                    const CefString& requestId);
  void RemoveHttpRequest(int httpRequestId);

  // Outgoing RPC messages.
  CefString SendRequest(const CefString& className,
                        int instanceId,
                        const CefString& methodName,
                        CefRefPtr<CefDictionaryValue> arguments);
  CefString SendRequest(const CefString& className,
                        int instanceId,
                        const CefString& methodName);
  void SendResponse(const CefString& requestId,
                    CefRefPtr<CefValue> returnValue);
  void SendErrorResponse(const CefString& requestId, const CefString& message);
  void SendUnknownInstanceErrorResponse(const CefString& requestId,
                                        const CefString& className,
                                        int instanceId);
  void SendDoesNotUnderstandErrorResponse(const CefString& requestId,
                                          const CefString& className,
                                          const CefString& methodName);
  void SendParseErrorResponse(const CefString& requestId,
                              const CefString& className,
                              int instanceId,
                              const CefString& methodName,
                              const CefString& message);
  void SendLogMessage(const cef_log_severity_t severity,
                      const CefString& message);
  void SendMessage(const CefString& payload);

 private:
  int serverPort;
  HANDLE applicationProcessHandle;
  std::optional<CefString> serverReadyEventName;

  CefRefPtr<CefServer> server;
  std::optional<int> activeConnectionId;

  std::mutex pendingCallbacksMutex;
  std::map<CefString, PendingCallback> pendingCallbacks;

  std::mutex entriesMutex;
  std::map<int, std::pair<CefRefPtr<BrowserHandler>, CefRefPtr<CefBrowser>>>
      browserEntries;
  std::map<int, CefRefPtr<CefURLRequest>> httpRequestEntries;

  int nextHttpRequestId = 1;
  bool isShuttingDown;

  IMPLEMENT_REFCOUNTING(BrowserProcessHandler);
  DISALLOW_COPY_AND_ASSIGN(BrowserProcessHandler);
};
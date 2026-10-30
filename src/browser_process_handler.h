#pragma once

#include <rpc.h>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "SDL3_net/SDL_net.h"
#include "include/cef_base.h"
#include "process_handler.h"
#include "rpc.hpp"
#include "thread_safe_queue.hpp"

class BrowserHandler;

using PendingCallback = std::function<void(CefRefPtr<CefValue>)>;

class BrowserProcessHandler : public ProcessHandler,
                              public CefBrowserProcessHandler {
 public:
  BrowserProcessHandler(int serverPort,
                        HANDLE applicationProcessHandle,
                        HWND applicationMessageWindowHandle,
                        int windowMessageId);
  ~BrowserProcessHandler() override;

  // Accessors.
  NET_Server* GetSocketServer();
  CefRefPtr<CefBrowser> GetBrowser(int browserId);
  CefRefPtr<BrowserHandler> GetBrowserHandler(int browserId);
  void RemoveBrowserHandler(int browserId);
  HANDLE GetApplicationProcessHandle();
  HWND GetApplicationMessageWindowHandle();
  int GetWindowMessageId();

  // CefBrowserProcessHandler methods.
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
  void OnContextInitialized() override;

  // RPC handling.
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

  // Incoming RPC messages.
  void Client_CreateBrowserRpc(const CefString& requestId,
                               const CefString& url,
                               const CefPoint& windowOrigin,
                               const CefRect& rectangle,
                               HWND parentWindowHandle,
                               bool windowless,
                               bool hardwareAccelerated,
                               bool private_);
  void Client_CreateHttpRequestRpc(
      const CefString& requestId,
      const CefString& url,
      const CefString& method,
      const std::optional<CefRefPtr<CefDictionaryValue>> headers,
      const std::optional<CefString>& body);
  void Client_ShutdownRpc();
  void Browser_CloseRpc(const CefRefPtr<CefBrowser> browser, bool forceClose);
  void Browser_TryCloseRpc(const CefRefPtr<CefBrowser> browser,
                           const CefString& requestId);
  void Browser_GetFrameRateRpc(const CefRefPtr<CefBrowser> browser,
                               const CefString& requestId);
  void HttpRequest_CancelRpc(int httpRequestId);
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

  // RPC threads.
  static void RpcReceiveThread(CefRefPtr<BrowserProcessHandler> handler);
  static void RpcSendThread(CefRefPtr<BrowserProcessHandler> handler);

 private:
  int serverPort;
  HANDLE applicationProcessHandle;
  HWND applicationWindowHandle;
  HWND applicationMessageWindowHandle;
  int windowMessageId;
  ThreadSafeQueue<std::string> outgoingMessageQueue;
  std::mutex pendingCallbacksMutex;
  std::map<CefString, PendingCallback> pendingCallbacks;
  std::map<int, std::pair<CefRefPtr<BrowserHandler>, CefRefPtr<CefBrowser>>>
      browserEntries;
  std::map<int, CefRefPtr<CefURLRequest>> httpRequestEntries;
  int nextHttpRequestId = 1;
  bool isShuttingDown;

  NET_Server* socketServer;
  NET_StreamSocket* streamSocket;

  IMPLEMENT_REFCOUNTING(BrowserProcessHandler);
  DISALLOW_COPY_AND_ASSIGN(BrowserProcessHandler);
};
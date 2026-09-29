#pragma once

#include <rpc.h>
#include "SDL3_net/SDL_net.h"
#include "include/cef_base.h"
#include "process_handler.h"
#include "rpc.hpp"
#include "thread_safe_queue.hpp"

class BrowserHandler;

class BrowserProcessHandler : public ProcessHandler, public CefBrowserProcessHandler {
public:
 BrowserProcessHandler(HANDLE applicationProcessHandle, HWND applicationMessageWindowHandle, int windowMessageId);
 ~BrowserProcessHandler();

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
  void HandleRpcRequest(RpcRequest request);
  void HandleRpcResponse(RpcResponse response);

  // Incoming RPC messages.
  void Client_CreateBrowserRpc(const UUID& requestId, const CefString& url, const CefRect& rectangle, HWND parentWindowHandle, bool windowless, bool hardwareAccelerated, bool private_);
  void Client_CreateHttpRequestRpc(const UUID& requestId, Client_CreateHttpRequest args);
  void Client_ShutdownRpc();
  void Browser_CloseRpc(const CefRefPtr<CefBrowser> browser, bool forceClose);
  void Browser_TryCloseRpc(const CefRefPtr<CefBrowser> browser, const UUID& requestId);
  void Browser_GetFrameRateRpc(const CefRefPtr<CefBrowser> browser, const UUID& requestId);
  void HttpRequest_CancelRpc(int httpRequestId);
  void RemoveHttpRequest(int httpRequestId);

  // Outgoing RPC messages.
  void SendMessage(std::string payload);
  void SendErrorResponse(const UUID& requestId, std::string message);
  void SendLogMessage(const SDL_LogPriority level, const std::string& message);
  template<typename T> std::optional<T> WaitForResponse(UUID id);

  // RPC threads, need to be static.
  static int RpcReceiveThread(void* browserProcessHandlerPtr);
  static int RpcSendThread(void* browserProcessHandlerPtr);

 private:
  HANDLE applicationProcessHandle;
  HWND applicationWindowHandle;
  HWND applicationMessageWindowHandle;
  int windowMessageId;
  ThreadSafeQueue<std::string> outgoingMessageQueue;
  SDL_Mutex* responseMapMutex = nullptr;
  std::map<UUID, std::unique_ptr<ResponseEntry>> responseEntries;
  std::map<int, std::pair<CefRefPtr<BrowserHandler>, CefRefPtr<CefBrowser>>> browserEntries;
  std::map<int, CefRefPtr<CefURLRequest>> httpRequestEntries;
  int nextHttpRequestId = 1;
  bool isShuttingDown;

  NET_Server* socketServer;
  NET_StreamSocket* streamSocket;

  IMPLEMENT_REFCOUNTING(BrowserProcessHandler);
  DISALLOW_COPY_AND_ASSIGN(BrowserProcessHandler);
};

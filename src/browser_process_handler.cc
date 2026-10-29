#include <stdio.h>
#include <windows.h>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <variant>

#include <SDL3/sdl.h>
#include <SDL3_net/SDL_net.h>
#include <include/base/cef_bind.h>
#include <include/base/cef_callback.h>
#include <include/cef_parser.h>
#include <include/cef_task.h>
#include <include/cef_urlrequest.h>
#include <include/wrapper/cef_closure_task.h>

#include "browser_handler.h"
#include "browser_process_handler.h"
#include "guid_ext.hpp"
#include "rpc.hpp"
#include "thread_safe_queue.hpp"
#include "http_request_client.h"
#include "client_rpc_commands.h"
#include "browser_rpc_commands.h"
#include "http_request_rpc_commands.h"

const char kEvalMessage[] = "Eval";

BrowserProcessHandler::BrowserProcessHandler(
    int serverPort,
    HANDLE applicationProcessHandle,
    HWND applicationMessageWindowHandle,
    int windowMessageId)
    : serverPort(serverPort),
      applicationProcessHandle(applicationProcessHandle),
      applicationMessageWindowHandle(applicationMessageWindowHandle),
      windowMessageId(windowMessageId),
      outgoingMessageQueue(),
      pendingCallbacksMutex(SDL_CreateMutex()),
      socketServer(NULL),
      browserEntries(),
      isShuttingDown(false),
      streamSocket(nullptr) {}

BrowserProcessHandler::~BrowserProcessHandler() {
  SDL_DestroyMutex(pendingCallbacksMutex);
  pendingCallbacksMutex = nullptr;
}

NET_Server* BrowserProcessHandler::GetSocketServer() {
  return socketServer;
}

CefRefPtr<CefBrowser> BrowserProcessHandler::GetBrowser(int browserId) {
  auto it = browserEntries.find(browserId);
  if (it != browserEntries.end()) {
    return it->second.second;
  }
  return nullptr;
}

CefRefPtr<BrowserHandler> BrowserProcessHandler::GetBrowserHandler(
    int browserId) {
  auto it = browserEntries.find(browserId);
  if (it != browserEntries.end()) {
    return it->second.first;
  }
  return nullptr;
}

void BrowserProcessHandler::RemoveBrowserHandler(int browserId) {
  browserEntries.erase(browserId);

  if (isShuttingDown && browserEntries.empty()) {
    CefPostTask(TID_UI, base::BindOnce([]() { CefQuitMessageLoop(); }));
  }
}

CefRefPtr<CefBrowserProcessHandler>
BrowserProcessHandler::GetBrowserProcessHandler() {
  return this;
}

HANDLE BrowserProcessHandler::GetApplicationProcessHandle() {
  return this->applicationProcessHandle;
}

HWND BrowserProcessHandler::GetApplicationMessageWindowHandle() {
  return this->applicationMessageWindowHandle;
}

int BrowserProcessHandler::GetWindowMessageId() {
  return this->windowMessageId;
}

void BrowserProcessHandler::OnContextInitialized() {
  this->CefBrowserProcessHandler::OnContextInitialized();

  if (!SDL_Init(SDL_INIT_EVENTS)) {
    SDL_Log(SDL_GetError());
    abort();
  }

  if (!NET_Init()) {
    SDL_Log(SDL_GetError());
    abort();
  }

  socketServer = NET_CreateServer(NULL, serverPort);
  if (socketServer == NULL) {
    SDL_Log(SDL_GetError());
    abort();
  }

  SDL_Log("Server started on port %d", serverPort);

  // Signal that the server is ready, then block until a client connects.
  HANDLE hEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE, L"ChromiumSocketReady");
  if (hEvent == NULL) {
    SDL_Log("Error opening ChromiumSocketReady event: %lu", GetLastError());
    abort();
  }
  if (!SetEvent(hEvent)) {
    SDL_Log("Error signaling ChromiumSocketReady event: %lu", GetLastError());
    abort();
  }
  CloseHandle(hEvent);

    // Wait for a client to connect before spawning I/O threads.
  void* waitSockets[] = {static_cast<void*>(socketServer)};
  NET_WaitUntilInputAvailable(waitSockets, 1, -1);

  if (!NET_AcceptClient(socketServer, &streamSocket)) {
    SDL_Log("Accept error: %s", SDL_GetError());
    abort();
  }
  if (!streamSocket) {
    SDL_Log("No client connected after WaitUntilInputAvailable");
    abort();
  }
  SDL_Log("Client connected!");

  SDL_Thread* receiveThread =
      SDL_CreateThread(RpcReceiveThread, "CefRpcReceive", this);
  if (receiveThread == NULL) {
    SDL_Log("Failed creating RPC receive thread: %s", SDL_GetError());
    abort();
  }

  SDL_Thread* sendThread = SDL_CreateThread(RpcSendThread, "CefRpcSend", this);
  if (sendThread == NULL) {
    SDL_Log("Failed creating RPC send thread: %s", SDL_GetError());
    abort();
  }
}

void BrowserProcessHandler::Client_CreateBrowserRpc(const CefString& requestId,
                                                    const CefString& url,
                                                    const CefPoint& windowOrigin,
                                                    const CefRect& rectangle,
                                                    HWND parentWindowHandle,
                                                    bool windowless,
                                                    bool hardwareAccelerated,
                                                    bool private_) {
  CefWindowInfo windowInfo;
  if (windowless) {
    windowInfo.SetAsWindowless(parentWindowHandle);  // no OS parent 
    windowInfo.shared_texture_enabled = hardwareAccelerated;
  } else {
    windowInfo.SetAsChild(parentWindowHandle, rectangle);
  }
  windowInfo.bounds = rectangle;

  CefBrowserSettings browserSettings;
  browserSettings.windowless_frame_rate = 30;

  CefRefPtr<CefDictionaryValue> extraInfo = CefDictionaryValue::Create();

  CefRefPtr<BrowserHandler> handler = new BrowserHandler(this, windowOrigin, rectangle);

  CefRefPtr<CefRequestContext> requestContext =
      private_ ? CefRequestContext::CreateContext(CefRequestContextSettings(),
                                                  nullptr)
               : CefRequestContext::GetGlobalContext();
  CefRefPtr<CefBrowser> browser = CefBrowserHost::CreateBrowserSync(windowInfo, handler, url, browserSettings, extraInfo, requestContext);
  
  int browserId = -1;
  if (browser) {
    browserId = browser->GetIdentifier();
    browserEntries[browserId] = {handler, browser};
    SDL_Log("Created browser on UI thread; id=%d url=%s", browserId,
            url.ToString().c_str());
    CefRefPtr<CefValue> returnValue = CefValue::Create();
    returnValue->SetInt(browserId);
    this->SendResponse(requestId, returnValue);
    handler->MarkCreated();
  } else {
    SDL_Log("CreateBrowserSync returned null");
  }
}

void BrowserProcessHandler::Client_CreateHttpRequestRpc(
    const CefString& requestId,
    const CefString& url,
    const CefString& method,
    const std::optional<CefRefPtr<CefDictionaryValue>> headers,
    const std::optional<CefString>& body) {
  int httpRequestId = nextHttpRequestId++;
  CefRefPtr<CefRequest> cefRequest = CefRequest::Create();
  cefRequest->SetURL(url);
  cefRequest->SetMethod(method);
  if (body.has_value()) {
    CefRefPtr<CefPostData> postData = CefPostData::Create();
    CefRefPtr<CefPostDataElement> element = CefPostDataElement::Create();
    element->SetToBytes(body->size(), body->ToString().c_str());
    postData->AddElement(element);
    cefRequest->SetPostData(postData);
  }
  if (headers.has_value()) {
    CefRefPtr<CefDictionaryValue> headersDictionary =
        headers.value();
    std::vector<CefString> keyList;
    headersDictionary->GetKeys(keyList);
    for (const CefString& key : keyList) {
      cefRequest->SetHeaderByName(key, headersDictionary->GetString(key), true);
    }
  }
  CefRefPtr<HttpRequestClient> client =
      new HttpRequestClient(this, httpRequestId);
  CefRefPtr<CefURLRequest> urlRequest =
      CefURLRequest::Create(cefRequest, client, CefRequestContext::GetGlobalContext());
  httpRequestEntries[httpRequestId] = urlRequest;
  CefRefPtr<CefValue> returnValue = CefValue::Create();
  returnValue->SetInt(httpRequestId);
  this->SendResponse(requestId, returnValue);
}

void BrowserProcessHandler::Client_ShutdownRpc() {
  isShuttingDown = true;
  if (browserEntries.empty()) {
    SDL_Log("No browser entries during shutdown.");
    CefQuitMessageLoop();
  } else {
    SDL_Log("%d browser entries during shutdown.", browserEntries.size());
    for (const auto& [id, entry] : browserEntries) {
      entry.second->GetHost()->CloseBrowser(true);
    }
  }
}

void BrowserProcessHandler::Browser_CloseRpc(
    const CefRefPtr<CefBrowser> browser,
    bool forceClose) {
  browser->GetHost()->CloseBrowser(forceClose);
}

void BrowserProcessHandler::Browser_TryCloseRpc(
    const CefRefPtr<CefBrowser> browser,
    const CefString& requestId) {
  bool canClose = browser->GetHost()->TryCloseBrowser();
  CefRefPtr<CefValue> returnValue = CefValue::Create();
  returnValue->SetBool(canClose);
  this->SendResponse(requestId, returnValue);
}

void BrowserProcessHandler::Browser_GetFrameRateRpc(
    const CefRefPtr<CefBrowser> browser,
    const CefString& requestId) {
  int frameRate = browser->GetHost()->GetWindowlessFrameRate();
  CefRefPtr<CefValue> returnValue = CefValue::Create();
  returnValue->SetInt(frameRate);
  this->SendResponse(requestId, returnValue);
}

void BrowserProcessHandler::SendMessage(const CefString& payload) {
  outgoingMessageQueue.push(payload.ToString());
}

CefString BrowserProcessHandler::SendRequest(const CefString& className, int instanceId, const CefString& methodName, CefRefPtr<CefDictionaryValue> arguments) {
  const CefString& requestId = uuid_to_string(CreateUuid());
  CefRefPtr<CefValue> request = CefValue::Create();
  CefRefPtr<CefDictionaryValue> payload = CefDictionaryValue::Create();
  payload->SetString("id", requestId);
  payload->SetString("class", className);
  payload->SetInt("instanceId", instanceId);
  payload->SetString("method", methodName);
  payload->SetDictionary("arguments", arguments);
  request->SetDictionary(payload);
  CefString jsonRequest = CefWriteJSON(request, JSON_WRITER_DEFAULT);
  this->SendMessage(jsonRequest);
  return requestId;
}

CefString BrowserProcessHandler::SendRequest(
    const CefString& className,
    int instanceId,
    const CefString& methodName) {
  return this->SendRequest(className, instanceId, methodName, CefDictionaryValue::Create());
}

void BrowserProcessHandler::SendResponse(const CefString& requestId,
                                         CefRefPtr<CefValue> returnValue) {
  auto response = CefDictionaryValue::Create();
  response->SetString("requestId", requestId);
  response->SetBool("success", true);
  if (returnValue) {
    response->SetValue("returnValue", returnValue);
  } else {
    response->SetNull("returnValue");
  }
  response->SetNull("error");
  auto root = CefValue::Create();
  root->SetDictionary(response);
  this->SendMessage(CefWriteJSON(root, JSON_WRITER_DEFAULT));
}

void BrowserProcessHandler::SendErrorResponse(const CefString& requestId,
                                              const CefString& errorMessage) {
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, errorMessage.ToString().c_str());
  auto response = CefDictionaryValue::Create();
  response->SetString("requestId", requestId);
  response->SetBool("success", false);
  response->SetNull("returnValue");
  response->SetString("error", errorMessage);
  auto root = CefValue::Create();
  root->SetDictionary(response);
  this->SendMessage(CefWriteJSON(root, JSON_WRITER_DEFAULT).ToString());
}

void BrowserProcessHandler::SendUnknownInstanceErrorResponse(
    const CefString& requestId,
    const CefString& className,
    int instanceId) {
    this->SendErrorResponse(requestId,
                        className.ToString() + " instance " + std::to_string(instanceId) +
                            " not found");
}

void BrowserProcessHandler::SendDoesNotUnderstandErrorResponse(const CefString& requestId, const CefString& className, const CefString& methodName) {
  this->SendErrorResponse(requestId,
                          "Unknown " + className.ToString()  + " method: " + methodName.ToString());
}

void BrowserProcessHandler::SendParseErrorResponse(const CefString& requestId,
    const CefString& className,
    int instanceId,
    const CefString& methodName,
    const CefString& message) {
  this->SendErrorResponse(requestId, "Error when trying to execute " +
                                         className.ToString() + ">>" +
                                         methodName.ToString() + " on instance " +
                                         std::to_string(instanceId) + ": " +
                                         message.ToString());
}


void BrowserProcessHandler::SendLogMessage(const SDL_LogPriority level,
                                           const CefString& message) {
  SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, level, message.ToString().c_str());
  auto arguments = CefDictionaryValue::Create();
  arguments->SetInt("level", level);
  arguments->SetString("message", message);
  this->SendRequest("Client", 1,
                    "OnLogMessage", arguments);
}

void BrowserProcessHandler::HandleRpcRequest(
    const CefString& requestId,
    const CefString& className,
    int instanceId,
    const CefString& methodName,
    CefRefPtr<CefDictionaryValue> arguments) {
  if (className == "Client") {
    auto cmd = RpcRegistry<ClientCommand>::Lookup(methodName);
    if (!cmd) {
      this->SendDoesNotUnderstandErrorResponse(requestId, className,
                                               methodName);
      return;
    }
    auto err = cmd->Initialize(requestId, arguments);
    if (err.has_value()) {
      this->SendParseErrorResponse(requestId, className, instanceId, methodName,
                                   err.value().ToString());
      return;
    }
    cmd->Execute(this);
    return;
  }

  if (className == "Browser") {
    CefRefPtr<CefBrowser> browser = GetBrowser(instanceId);
    CefRefPtr<BrowserHandler> browserHandler = GetBrowserHandler(instanceId);
    if (!browser || !browserHandler) {
      this->SendDoesNotUnderstandErrorResponse(requestId, className, methodName);
      return;
    }
    auto cmd = RpcRegistry<BrowserCommand>::Lookup(methodName);
    if (!cmd) {
      this->SendDoesNotUnderstandErrorResponse(requestId, className,
                                               methodName);
      return;
    }
    auto err = cmd->Initialize(requestId, arguments);
    if (err.has_value()) {
      this->SendParseErrorResponse(requestId, className, instanceId, methodName, err.value().ToString());
      return;
    }
    cmd->Execute(this, browser, browserHandler);
    return;
  }

  if (className == "HttpRequest") {
    auto it = httpRequestEntries.find(instanceId);
    if (it == httpRequestEntries.end()) {
      this->SendDoesNotUnderstandErrorResponse(requestId, className,
                                               methodName);
      return;
    }
    auto cmd = RpcRegistry<HttpRequestCommand>::Lookup(methodName);
    if (!cmd) {
      this->SendDoesNotUnderstandErrorResponse(requestId, className, methodName);
      return;
    }
    auto err = cmd->Initialize(requestId, arguments);
    if (err.has_value()) {
      this->SendParseErrorResponse(requestId, className, instanceId, methodName,
                                   err.value().ToString());
      return;
    }
    cmd->Execute(this, it->second);
    return;
  }

  this->SendErrorResponse(requestId, "Unknown RPC target class: " + className.ToString());
}

void BrowserProcessHandler::RegisterPendingCallback(const CefString& requestId,
                                                    PendingCallback callback) {
  SDL_LockMutex(this->pendingCallbacksMutex);
  this->pendingCallbacks[requestId] = std::move(callback);
  SDL_UnlockMutex(this->pendingCallbacksMutex);
}

void BrowserProcessHandler::CancelPendingCallback(const CefString& requestId) {
  SDL_LockMutex(this->pendingCallbacksMutex);
  this->pendingCallbacks.erase(requestId);
  SDL_UnlockMutex(this->pendingCallbacksMutex);
}

std::map<CefString, PendingCallback> BrowserProcessHandler::GetPendingCallbacks() {
  SDL_LockMutex(this->pendingCallbacksMutex);
  std::map<CefString, PendingCallback> copy = this->pendingCallbacks;
  SDL_UnlockMutex(this->pendingCallbacksMutex);
  return copy;
}

void BrowserProcessHandler::HandleRpcResponse(const CefString& requestId,
                                              bool success,
                                              CefRefPtr<CefValue> returnValue,
                                              CefRefPtr<CefValue> error) {
  PendingCallback callback = nullptr;

  SDL_LockMutex(this->pendingCallbacksMutex);
  auto it = this->pendingCallbacks.find(requestId);
  if (it != this->pendingCallbacks.end()) {
    callback = std::move(it->second);
    this->pendingCallbacks.erase(it);
  }
  SDL_UnlockMutex(this->pendingCallbacksMutex);

  if (callback) {
    if (success) {
      // Execute callback (which handles posting to TID_UI if needed)
      callback(returnValue);
    } else {
      SDL_Log("RPC response error for request: %s",
              error->GetString().ToString().c_str());
    }
  }
}

int BrowserProcessHandler::RpcReceiveThread(void* browserProcessHandlerPtr) {
  CefRefPtr<BrowserProcessHandler> handler =
      base::WrapRefCounted<BrowserProcessHandler>(
          static_cast<BrowserProcessHandler*>(browserProcessHandlerPtr));

  SDL_Log("Receive thread running");

  std::vector<uint8_t> recvBuffer;
  uint8_t temp[1024];

  while (true) {
    // Block until the client sends data.
    void* waitSockets[] = {static_cast<void*>(handler->streamSocket)};
    NET_WaitUntilInputAvailable(waitSockets, 1, -1);

    int received =
        NET_ReadFromStreamSocket(handler->streamSocket, temp, sizeof(temp));
    if (received > 0) {
      recvBuffer.insert(recvBuffer.end(), temp, temp + received);
    } else if (received < 0) {
      SDL_Log("Read error: %s", SDL_GetError());
      break;
    }

    // Process as many full framed messages as available.
    while (recvBuffer.size() >= 4) {
      uint32_t msgLen;
      memcpy(&msgLen, recvBuffer.data(), 4);

      if (recvBuffer.size() < 4 + msgLen)
        break;

      std::string tempJsonString(reinterpret_cast<const char*>(recvBuffer.data() + 4),
                          msgLen);
      CefString jsonString(tempJsonString);
      
      // Consume framed bytes from recvBuffer
      recvBuffer.erase(recvBuffer.begin(), recvBuffer.begin() + 4 + msgLen);
            
      CefString errorMessageOut;
      CefRefPtr<CefValue> jsonMessage = CefParseJSONAndReturnError(jsonString, JSON_PARSER_ALLOW_TRAILING_COMMAS,
          errorMessageOut);
      if (!jsonMessage) {
        SDL_Log(
            "RpcReceiveThread: JSON parse_error: %s",
                errorMessageOut.ToString().c_str());
        continue;
      }

      CefRefPtr<CefDictionaryValue> jsonMessageDictionary = jsonMessage->GetDictionary();
      try {
        if (jsonMessageDictionary->HasKey("requestId")) {
          CefString requestId = jsonMessageDictionary->GetString("requestId");
          bool success = jsonMessageDictionary->GetBool("success");
          CefRefPtr<CefValue> returnValue = jsonMessageDictionary->GetValue("returnValue");
          CefRefPtr<CefValue> error = jsonMessageDictionary->GetValue("error");
          handler->HandleRpcResponse(requestId, success, returnValue, error);
        } else {
          CefString requestId = jsonMessageDictionary->GetString("id");
          CefString className = jsonMessageDictionary->GetString("class");
          int instanceId = jsonMessageDictionary->GetInt("instanceId");
          CefString methodName = jsonMessageDictionary->GetString("method");
          CefRefPtr<CefDictionaryValue> arguments =
              jsonMessageDictionary->GetDictionary("arguments");
          handler->HandleRpcRequest(requestId, className, instanceId,
                                    methodName, arguments); 
        }
      } catch (const std::exception& e) {
        SDL_Log("RpcReceiveThread: exception during dispatch: %s", e.what());
      }
    }
  }
  return 0;
}

int BrowserProcessHandler::RpcSendThread(void* browserProcessHandlerPtr) {
  CefRefPtr<BrowserProcessHandler> handler =
      base::WrapRefCounted<BrowserProcessHandler>(
          static_cast<BrowserProcessHandler*>(browserProcessHandlerPtr));

  SDL_Log("Send thread running");

  while (true) {
    // Block until an outgoing message is available.
    std::string outMsg = handler->outgoingMessageQueue.pop();

    uint32_t len = static_cast<uint32_t>(outMsg.size());
    std::vector<uint8_t> sendBuf;
    sendBuf.resize(4 + outMsg.size());
    memcpy(sendBuf.data(), &len, 4);
    if (!outMsg.empty()) {
      memcpy(sendBuf.data() + 4, outMsg.data(), outMsg.size());
    }

    int total = static_cast<int>(sendBuf.size());
    if (!NET_WriteToStreamSocket(handler->streamSocket, sendBuf.data(),
                                 total)) {
      SDL_Log("NET_WriteToStreamSocket failed or connection closed: %s",
              SDL_GetError());
      break;
    }
    NET_WaitUntilStreamSocketDrained(handler->streamSocket, -1);
    PostMessageW(handler->applicationMessageWindowHandle,
                 handler->windowMessageId, 0, 0);
  }
  return 0;
}

void BrowserProcessHandler::RemoveHttpRequest(const int requestId) {
  httpRequestEntries.erase(requestId);
}
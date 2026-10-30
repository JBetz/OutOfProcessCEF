#include "browser_process_handler.h"

#include <windows.h>
#include <iostream>
#include <string>

#include <include/base/cef_bind.h>
#include <include/base/cef_callback.h>
#include <include/base/cef_logging.h>
#include <include/cef_parser.h>
#include <include/cef_task.h>
#include <include/cef_urlrequest.h>
#include <include/wrapper/cef_closure_task.h>

#include "browser_handler.h"
#include "browser_rpc_commands.h"
#include "client_rpc_commands.h"
#include "http_request_client.h"
#include "http_request_rpc_commands.h"
#include "rpc.hpp"

const char kEvalMessage[] = "Eval";

BrowserProcessHandler::BrowserProcessHandler(
    int serverPort,
    HANDLE applicationProcessHandle)
    : serverPort(serverPort),
      applicationProcessHandle(applicationProcessHandle),
      server(nullptr),
      activeConnectionId(std::nullopt),
      browserEntries(),
      isShuttingDown(false) {}

BrowserProcessHandler::~BrowserProcessHandler() {
  if (server) {
    server->Shutdown();
    server = nullptr;
  }
}

CefRefPtr<CefServer> BrowserProcessHandler::GetServer() {
  return server;
}

CefRefPtr<CefBrowser> BrowserProcessHandler::GetBrowser(int browserId) {
  std::lock_guard<std::mutex> lock(entriesMutex);
  auto it = browserEntries.find(browserId);
  if (it != browserEntries.end()) {
    return it->second.second;
  }
  return nullptr;
}

CefRefPtr<BrowserHandler> BrowserProcessHandler::GetBrowserHandler(
    int browserId) {
  std::lock_guard<std::mutex> lock(entriesMutex);
  auto it = browserEntries.find(browserId);
  if (it != browserEntries.end()) {
    return it->second.first;
  }
  return nullptr;
}

void BrowserProcessHandler::RemoveBrowserHandler(int browserId) {
  std::lock_guard<std::mutex> lock(entriesMutex);
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

void BrowserProcessHandler::OnContextInitialized() {
  this->CefBrowserProcessHandler::OnContextInitialized();

  CefServer::CreateServer("127.0.0.1", static_cast<uint16_t>(serverPort), 10,
                          this);
  LOG(INFO) << "Starting WebSocket server on port " << serverPort;
}

void BrowserProcessHandler::OnServerCreated(CefRefPtr<CefServer> server_) {
  this->server = server_;
  LOG(INFO) << "WebSocket server listening on "
            << server->GetAddress().ToString();

  // Signal ChromiumSocketReady once the server is successfully listening.
  HANDLE hEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE, L"ChromiumSocketReady");
  if (hEvent != NULL) {
    SetEvent(hEvent);
    CloseHandle(hEvent);
  }
}

void BrowserProcessHandler::OnServerDestroyed(CefRefPtr<CefServer> server_) {
  LOG(INFO) << "WebSocket server destroyed";
  this->server = nullptr;
  this->activeConnectionId.reset();
}

void BrowserProcessHandler::OnClientConnected(CefRefPtr<CefServer> server_,
                                              int connection_id) {
  LOG(INFO) << "TCP client connected: connection_id=" << connection_id;
}

void BrowserProcessHandler::OnClientDisconnected(CefRefPtr<CefServer> server_,
                                                 int connection_id) {
  LOG(INFO) << "Client disconnected: connection_id=" << connection_id;
  if (activeConnectionId.has_value() &&
      activeConnectionId.value() == connection_id) {
    activeConnectionId.reset();
  }
}

void BrowserProcessHandler::OnHttpRequest(CefRefPtr<CefServer> server_,
                                          int connection_id,
                                          const CefString& client_address,
                                          CefRefPtr<CefRequest> request) {
  // Reject standard HTTP requests - only WebSockets are accepted.
  server->SendHttp500Response(connection_id, "WebSocket endpoint only!");
}

void BrowserProcessHandler::OnWebSocketRequest(
    CefRefPtr<CefServer> server_,
    int connection_id,
    const CefString& client_address,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  // Enforce single-client controller policy.
  if (activeConnectionId.has_value()) {
    LOG(WARNING) << "Rejecting connection from " << client_address.ToString()
                 << ": host client already connected (id="
                 << *activeConnectionId << ")";
    callback->Cancel();
    return;
  }
  callback->Continue();
}

void BrowserProcessHandler::OnWebSocketConnected(CefRefPtr<CefServer> server_,
                                                 int connection_id) {
  activeConnectionId = connection_id;
  LOG(INFO) << "WebSocket client connected successfully: connection_id="
            << connection_id;
}

void BrowserProcessHandler::OnWebSocketMessage(CefRefPtr<CefServer> server_,
                                               int connection_id,
                                               const void* data,
                                               size_t data_size) {
  if (!data || data_size == 0) {
    return;
  }
  std::string jsonStr(reinterpret_cast<const char*>(data), data_size);
  CefString errorMessageOut;
  CefRefPtr<CefValue> jsonMessage = CefParseJSONAndReturnError(
      jsonStr, JSON_PARSER_ALLOW_TRAILING_COMMAS, errorMessageOut);
  if (!jsonMessage) {
    LOG(ERROR) << "OnWebSocketMessage: JSON parse error: "
               << errorMessageOut.ToString();
    return;
  }
  CefRefPtr<CefDictionaryValue> jsonMessageDictionary =
      jsonMessage->GetDictionary();
  if (!jsonMessageDictionary) {
    LOG(ERROR) << "OnWebSocketMessage: Root JSON value is not a dictionary";
    return;
  }
  try {
    if (jsonMessageDictionary->HasKey("requestId")) {
      CefString requestId = jsonMessageDictionary->GetString("requestId");
      bool success = jsonMessageDictionary->GetBool("success");
      CefRefPtr<CefValue> returnValue =
          jsonMessageDictionary->GetValue("returnValue");
      CefRefPtr<CefValue> error = jsonMessageDictionary->GetValue("error");
      this->HandleRpcResponse(requestId, success, returnValue, error);
    } else {
      CefString requestId = jsonMessageDictionary->GetString("id");
      CefString className = jsonMessageDictionary->GetString("class");
      int instanceId = jsonMessageDictionary->GetInt("instanceId");
      CefString methodName = jsonMessageDictionary->GetString("method");
      CefRefPtr<CefDictionaryValue> arguments =
          jsonMessageDictionary->GetDictionary("arguments");
      this->HandleRpcRequest(requestId, className, instanceId, methodName, arguments);
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "OnWebSocketMessage: exception during dispatch: " << e.what();
  }
}

void BrowserProcessHandler::Client_CreateBrowserRpc(
    const CefString& requestId,
    const CefString& url,
    const CefPoint& windowOrigin,
    const CefRect& rectangle,
    HWND parentWindowHandle,
    bool windowless,
    bool hardwareAccelerated,
    bool private_) {
  CefWindowInfo windowInfo;
  if (windowless) {
    windowInfo.SetAsWindowless(parentWindowHandle);
    windowInfo.shared_texture_enabled = hardwareAccelerated;
  } else {
    windowInfo.SetAsChild(parentWindowHandle, rectangle);
  }
  windowInfo.bounds = rectangle;

  CefBrowserSettings browserSettings;
  browserSettings.windowless_frame_rate = 30;

  CefRefPtr<CefDictionaryValue> extraInfo = CefDictionaryValue::Create();
  CefRefPtr<BrowserHandler> handler =
      new BrowserHandler(this, windowOrigin, rectangle);

  CefRefPtr<CefRequestContext> requestContext =
      private_ ? CefRequestContext::CreateContext(CefRequestContextSettings(),
                                                  nullptr)
               : CefRequestContext::GetGlobalContext();

  CefRefPtr<CefBrowser> browser = CefBrowserHost::CreateBrowserSync(
      windowInfo, handler, url, browserSettings, extraInfo, requestContext);

  if (browser) {
    int browserId = browser->GetIdentifier();
    {
      std::lock_guard<std::mutex> lock(entriesMutex);
      browserEntries[browserId] = {handler, browser};
    }
    LOG(INFO) << "Created browser on UI thread; id=" << browserId
              << " url=" << url;
    CefRefPtr<CefValue> returnValue = CefValue::Create();
    returnValue->SetInt(browserId);
    this->SendResponse(requestId, returnValue);
    handler->MarkCreated();
  } else {
    LOG(ERROR) << "CreateBrowserSync returned null";
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
    CefRefPtr<CefDictionaryValue> headersDictionary = headers.value();
    std::vector<CefString> keyList;
    headersDictionary->GetKeys(keyList);
    for (const CefString& key : keyList) {
      cefRequest->SetHeaderByName(key, headersDictionary->GetString(key), true);
    }
  }

  CefRefPtr<HttpRequestClient> client =
      new HttpRequestClient(this, httpRequestId);
  CefRefPtr<CefURLRequest> urlRequest = CefURLRequest::Create(
      cefRequest, client, CefRequestContext::GetGlobalContext());

  {
    std::lock_guard<std::mutex> lock(entriesMutex);
    httpRequestEntries[httpRequestId] = urlRequest;
  }

  CefRefPtr<CefValue> returnValue = CefValue::Create();
  returnValue->SetInt(httpRequestId);
  this->SendResponse(requestId, returnValue);
}

void BrowserProcessHandler::Client_ShutdownRpc() {
  isShuttingDown = true;
  std::lock_guard<std::mutex> lock(entriesMutex);
  if (browserEntries.empty()) {
    LOG(INFO) << "No browser entries during shutdown.";
    CefQuitMessageLoop();
  } else {
    LOG(INFO) << browserEntries.size() << " browser entries during shutdown.";
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
  if (!server || !activeConnectionId.has_value()) {
    LOG(WARNING) << "Cannot send WebSocket message: no active connection";
    return;
  }

  const std::string& str = payload.ToString();
  server->SendWebSocketMessage(activeConnectionId.value(), str.data(), str.size());
}

CefString BrowserProcessHandler::SendRequest(
    const CefString& className,
    int instanceId,
    const CefString& methodName,
    CefRefPtr<CefDictionaryValue> arguments) {
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

CefString BrowserProcessHandler::SendRequest(const CefString& className,
                                             int instanceId,
                                             const CefString& methodName) {
  return this->SendRequest(className, instanceId, methodName,
                           CefDictionaryValue::Create());
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
  LOG(ERROR) << errorMessage;
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
  this->SendErrorResponse(requestId, className.ToString() + " instance " +
                                         std::to_string(instanceId) +
                                         " not found");
}

void BrowserProcessHandler::SendDoesNotUnderstandErrorResponse(
    const CefString& requestId,
    const CefString& className,
    const CefString& methodName) {
  this->SendErrorResponse(requestId, "Unknown method: " + className.ToString() + ">>" + methodName.ToString());
}

void BrowserProcessHandler::SendParseErrorResponse(const CefString& requestId,
                                                   const CefString& className,
                                                   int instanceId,
                                                   const CefString& methodName,
                                                   const CefString& message) {
  this->SendErrorResponse(
      requestId, "Error when trying to execute " + className.ToString() + ">>" +
                     methodName.ToString() + " on instance " +
                     std::to_string(instanceId) + ": " + message.ToString());
}

void BrowserProcessHandler::SendLogMessage(const cef_log_severity_t severity,
                                           const CefString& message) {
  if (severity == LOGSEVERITY_DEFAULT || severity == LOGSEVERITY_VERBOSE || severity == LOGSEVERITY_INFO) {
    LOG(INFO) << message;
 } else if (severity == LOGSEVERITY_WARNING) {
    LOG(WARNING) << message;
  } else if (severity == LOGSEVERITY_ERROR) {
    LOG(ERROR) << message;
  } else if (severity == LOGSEVERITY_FATAL) {
    LOG(FATAL) << message;
  }
  auto arguments = CefDictionaryValue::Create();
  arguments->SetInt("severity", severity);
  arguments->SetString("message", message);
  this->SendRequest("Client", 1, "OnLogMessage", arguments);
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
      this->SendUnknownInstanceErrorResponse(requestId, className, instanceId);
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
      this->SendParseErrorResponse(requestId, className, instanceId, methodName,
                                   err.value().ToString());
      return;
    }
    cmd->Execute(this, browser, browserHandler);
    return;
  }

  if (className == "HttpRequest") {
    CefRefPtr<CefURLRequest> request;
    {
      std::lock_guard<std::mutex> lock(entriesMutex);
      auto it = httpRequestEntries.find(instanceId);
      if (it != httpRequestEntries.end()) {
        request = it->second;
      }
    }
    if (!request) {
      this->SendUnknownInstanceErrorResponse(requestId, className, instanceId);
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
    cmd->Execute(this, request);
    return;
  }

  this->SendErrorResponse(requestId,
                          "Unknown RPC target class: " + className.ToString());
}

void BrowserProcessHandler::RegisterPendingCallback(const CefString& requestId,
                                                    PendingCallback callback) {
  std::lock_guard<std::mutex> lock(this->pendingCallbacksMutex);
  this->pendingCallbacks[requestId] = std::move(callback);
}

void BrowserProcessHandler::CancelPendingCallback(const CefString& requestId) {
  std::lock_guard<std::mutex> lock(this->pendingCallbacksMutex);
  this->pendingCallbacks.erase(requestId);
}

std::map<CefString, PendingCallback>
BrowserProcessHandler::GetPendingCallbacks() {
  std::lock_guard<std::mutex> lock(this->pendingCallbacksMutex);
  return this->pendingCallbacks;
}

void BrowserProcessHandler::HandleRpcResponse(const CefString& requestId,
                                              bool success,
                                              CefRefPtr<CefValue> returnValue,
                                              CefRefPtr<CefValue> error) {
  PendingCallback callback = nullptr;

  {
    std::lock_guard<std::mutex> lock(this->pendingCallbacksMutex);
    auto it = this->pendingCallbacks.find(requestId);
    if (it != this->pendingCallbacks.end()) {
      callback = std::move(it->second);
      this->pendingCallbacks.erase(it);
    }
  }

  if (callback) {
    if (success) {
      callback(returnValue);
    } else {
      LOG(ERROR) << "RPC response error for request: "
                 << (error ? error->GetString() : CefString("unknown error"));
    }
  }
}

void BrowserProcessHandler::RemoveHttpRequest(const int requestId) {
  std::lock_guard<std::mutex> lock(entriesMutex);
  httpRequestEntries.erase(requestId);
}
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
#include <json.hpp>

#include "browser_handler.h"
#include "browser_process_handler.h"
#include "guid_ext.hpp"
#include "rpc.hpp"
#include "thread_safe_queue.hpp"
#include "http_request_client.h"

using json = nlohmann::json;

const char kEvalMessage[] = "Eval";

class DownloadImageCallback : public CefDownloadImageCallback {
 public:
  DownloadImageCallback(BrowserProcessHandler* handler,
                        const UUID& requestId,
                        const std::string& imageUrl)
      : handler(handler), requestId(requestId), imageUrl(imageUrl) {}

  void OnDownloadImageFinished(const CefString& image_url,
                               int http_status_code,
                               CefRefPtr<CefImage> image_) override {
    Browser_DownloadImageResponse arguments;
    arguments.imageUrl = imageUrl;
    arguments.httpStatusCode = http_status_code;

    if (image_ && !image_->IsEmpty()) {
      float scale_factor = 1.0f;
      int pixel_width = 0;
      int pixel_height = 0;

      CefRefPtr<CefBinaryValue> binary =
          image_->GetAsPNG(scale_factor, true, pixel_width, pixel_height);
      if (binary && binary->GetSize() > 0) {
        std::vector<uint8_t> data(binary->GetSize());
        binary->GetData(data.data(), data.size(), 0);
        PNGImageData image;
        image.data = data;
        image.width = pixel_width;
        image.height = pixel_height;
        arguments.images.push_back(image);
      }
    }

    RpcResponse response;
    response.requestId = requestId;
    response.success = true;
    response.returnValue = arguments;
    json jsonResponse = response;

    handler->SendMessage(jsonResponse.dump());
  }

 private:
  BrowserProcessHandler* handler;
  UUID requestId;
  std::string imageUrl;

  IMPLEMENT_REFCOUNTING(DownloadImageCallback);
};

// Callback for CefFrame::GetSource
class GetSourceStringVisitor : public CefStringVisitor {
 public:
  GetSourceStringVisitor(BrowserProcessHandler* handler, const UUID& requestId)
      : handler(handler), requestId(requestId) {}

  void Visit(const CefString& string) override {
    RpcResponse response;
    response.requestId = requestId;
    response.success = true;
    response.returnValue = string.ToString();
    json jsonResponse = response;
    handler->SendMessage(jsonResponse.dump());
  }

 private:
  BrowserProcessHandler* handler;
  UUID requestId;

  IMPLEMENT_REFCOUNTING(GetSourceStringVisitor);
};

BrowserProcessHandler::BrowserProcessHandler(
    HANDLE applicationProcessHandle,
    HWND applicationMessageWindowHandle,
    int windowMessageId)
    : applicationProcessHandle(applicationProcessHandle),
      applicationMessageWindowHandle(applicationMessageWindowHandle),
      windowMessageId(windowMessageId),
      outgoingMessageQueue(),
      responseMapMutex(SDL_CreateMutex()),
      socketServer(NULL),
      browserEntries(),
      isShuttingDown(false),
      streamSocket(nullptr) {}

BrowserProcessHandler::~BrowserProcessHandler() {
  SDL_DestroyMutex(responseMapMutex);
  responseMapMutex = nullptr;
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

  socketServer = NET_CreateServer(NULL, 3000);
  if (socketServer == NULL) {
    SDL_Log(SDL_GetError());
    abort();
  }

  SDL_Log("Server started on port 3000");

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

void BrowserProcessHandler::Client_CreateBrowserRpc(const UUID& requestId,
                                                    const CefString& url,
                                                    const CefRect& rectangle,
                                                    HWND parentWindowHandle,
                                                    bool windowless,
                                                    bool hardwareAccelerated) {
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

  CefRefPtr<CefRequestContext> requestContext =
      CefRequestContext::CreateContext(CefRequestContextSettings(), nullptr);
  CefRefPtr<CefDictionaryValue> extraInfo = CefDictionaryValue::Create();

  CefRefPtr<BrowserHandler> handler = new BrowserHandler(this, rectangle);

  CefRefPtr<CefBrowser> browser = CefBrowserHost::CreateBrowserSync(
      windowInfo, handler, url, browserSettings, extraInfo, requestContext);

  int browserId = -1;
  if (browser) {
    browserId = browser->GetIdentifier();
    browserEntries[browserId] = {handler, browser};
    SDL_Log("Created browser on UI thread; id=%d url=%s", browserId,
            url.c_str());
    RpcResponse response;
    response.requestId = requestId;
    response.success = true;
    response.returnValue = browserId;
    json j = response;
    this->SendMessage(j.dump());
    handler->MarkCreated();
  } else {
    SDL_Log("CreateBrowserSync returned null");
  }
}

void BrowserProcessHandler::Client_CreateHttpRequestRpc(
    const UUID& requestId,
    Client_CreateHttpRequest args) {
  int httpRequestId = nextHttpRequestId++;

  CefRefPtr<CefRequest> cefRequest = CefRequest::Create();
  cefRequest->SetURL(args.url);
  cefRequest->SetMethod(args.method);
  
  if (args.body.has_value()) {
    std::string bodyString = args.body->dump();
    CefRefPtr<CefPostData> postData = CefPostData::Create();
    CefRefPtr<CefPostDataElement> element = CefPostDataElement::Create();
    element->SetToBytes(bodyString.size(), bodyString.data());
    postData->AddElement(element);
    cefRequest->SetPostData(postData);
  }

  if (args.headers.has_value()) {
    for (const auto& [k, v] : args.headers.value()) {
      cefRequest->SetHeaderByName(k.c_str(), v.c_str(), true);
    }
  }

  CefRefPtr<HttpRequestClient> client =
      new HttpRequestClient(this, httpRequestId);
  CefRefPtr<CefURLRequest> urlRequest =
      CefURLRequest::Create(cefRequest, client, CefRequestContext::GetGlobalContext());
  httpRequestEntries[httpRequestId] = urlRequest;

  RpcResponse response;
  response.requestId = requestId;
  response.success = true;
  response.returnValue = httpRequestId;
  json jsonResponse = response;
  SendMessage(jsonResponse.dump());
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
    const UUID& requestId) {
  bool canClose = browser->GetHost()->TryCloseBrowser();
  RpcResponse response;
  response.requestId = requestId;
  response.returnValue = canClose;
  json jsonResponse = response;
  this->SendMessage(jsonResponse.dump());
}

void BrowserProcessHandler::Browser_GetFrameRateRpc(
    const CefRefPtr<CefBrowser> browser,
     const UUID& requestId) {
  int frameRate = browser->GetHost()->GetWindowlessFrameRate();
  RpcResponse response;
  response.requestId = requestId;
  response.success = true;
  response.returnValue = frameRate;
  json jsonResponse = response;
  this->SendMessage(jsonResponse.dump());
}

void BrowserProcessHandler::SendMessage(std::string payload) {
  outgoingMessageQueue.push(payload);
}

void BrowserProcessHandler::SendErrorResponse(const UUID& requestId,
                                             std::string message) {
  RpcResponse response;
  response.requestId = requestId;
  response.success = false;
  response.returnValue = message;
  json jsonResponse = response;
  this->SendMessage(jsonResponse.dump());
}

void BrowserProcessHandler::SendLogMessage(const SDL_LogPriority level,
                                           const std::string& message) {
  RpcRequest request;
  request.id = CreateUuid();
  request.className = "Client";
  request.methodName = "OnLogMessage";
  json args;
  args["level"] = level;
  args["message"] = message;
  request.arguments = args;
  json j = request;
  this->SendMessage(j.dump());
  SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, level, "%s", message.c_str());
}

void BrowserProcessHandler::HandleRpcRequest(RpcRequest request) {
  if (request.className == "Client") {
    if (request.methodName == "CreateBrowser") {
      Client_CreateBrowser arguments =
          request.arguments.get<Client_CreateBrowser>();
      HWND parentWindowHandle =
          reinterpret_cast<HWND>(arguments.parentWindowHandle);
      CefPostTask(
          TID_UI,
          base::BindOnce(&BrowserProcessHandler::Client_CreateBrowserRpc, this,
                         request.id, arguments.url, arguments.rectangle, parentWindowHandle, arguments.windowless, arguments.hardwareAccelerated));
      return;
    }

    if (request.methodName == "CreateHttpRequest") {
      Client_CreateHttpRequest args =
          request.arguments.get<Client_CreateHttpRequest>();
      CefPostTask(
          TID_UI,
          base::BindOnce(&BrowserProcessHandler::Client_CreateHttpRequestRpc,
                         this, request.id, std::move(args)));
      return;
    }

    if (request.methodName == "Shutdown") {
      CefPostTask(
          TID_UI,
          base::BindOnce(&BrowserProcessHandler::Client_ShutdownRpc, this));
      return;
    }
  }

  if (request.className == "Browser") {
    CefRefPtr<BrowserHandler> browserHandler =
        this->GetBrowserHandler(request.instanceId);
    if (!browserHandler) {
      std::string message = "Browser handler for browser instance " +
                            std::to_string(request.instanceId) + " not found.";
      this->SendErrorResponse(request.id, message);
      return;
    }
    CefRefPtr<CefBrowser> browser = this->GetBrowser(request.instanceId);
    if (!browser) {
      std::string message = "Browser instance " +
                            std::to_string(request.instanceId) + " not found.";
      this->SendErrorResponse(request.id, message);
      return;
    }

    if (request.methodName == "EvaluateJavaScript") {
      CefRefPtr<CefFrame> frame = browser->GetMainFrame();
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kEvalMessage);
      json jsonRequest = request;
      message->GetArgumentList()->SetString(0, jsonRequest.dump());
      frame->SendProcessMessage(PID_RENDERER, message);
      return;
    };

    if (request.methodName == "Reload") {
      browser->Reload();
      return;
    }

    if (request.methodName == "Focus") {
      Browser_Focus arguments = request.arguments.get<Browser_Focus>();
      browser->GetHost()->SetFocus(arguments.focus);
      return;
    }

    if (request.methodName == "WasHidden") {
      Browser_WasHidden arguments = request.arguments.get<Browser_WasHidden>();
      browser->GetHost()->WasHidden(arguments.hidden);
      return;
    }

    if (request.methodName == "LoadUrl") {
      Browser_LoadUrl arguments = request.arguments.get<Browser_LoadUrl>();
      CefRefPtr<CefFrame> frame = browser->GetMainFrame();
      frame->LoadURL(arguments.url);
      return;
    }

    if (request.methodName == "LoadRequest") {
      Browser_LoadRequest arguments =
          request.arguments.get<Browser_LoadRequest>();
      CefRefPtr<CefFrame> frame = browser->GetMainFrame();
      CefRefPtr<CefRequest> cefRequest = CefRequest::Create();
      cefRequest->SetURL(arguments.url);
      cefRequest->SetMethod(arguments.method);
      if (arguments.postData.size() > 0) {
        CefRefPtr<CefPostData> postData = CefPostData::Create();
        for (const auto& elementArguments : arguments.postData) {
          CefRefPtr<CefPostDataElement> element = CefPostDataElement::Create();
          switch (elementArguments.type) {
            case CefPostDataElement::Type::PDE_TYPE_EMPTY:
              element->SetToEmpty();
              break;
            case CefPostDataElement::Type::PDE_TYPE_FILE:
              element->SetToFile(elementArguments.fileName.value());
              break;
            case CefPostDataElement::Type::PDE_TYPE_BYTES:
              element->SetToBytes(elementArguments.bytes->size(), elementArguments.bytes->data());
              break;
          }
          postData->AddElement(element);
        }
        cefRequest->SetPostData(postData);
      } 
      for (const auto& [key, value] : arguments.headerMap) {
        cefRequest->SetHeaderByName(key, value, true);
      }
      frame->LoadRequest(cefRequest);
      return;
    }

    if (request.methodName == "WasResized") {
      browser->GetHost()->WasResized();
      return;
    }

    if (request.methodName == "Cut") {
      browser->GetFocusedFrame()->Cut();
      return;
    }

    if (request.methodName == "Copy") {
      browser->GetFocusedFrame()->Copy();
      return;
    }

    if (request.methodName == "Paste") {
      browser->GetFocusedFrame()->Paste();
      return;
    }

    if (request.methodName == "Delete") {
      browser->GetFocusedFrame()->Delete();
      return;
    }

    if (request.methodName == "Undo") {
      browser->GetFocusedFrame()->Undo();
      return;
    }

    if (request.methodName == "Redo") {
      browser->GetFocusedFrame()->Redo();
      return;
    }

    if (request.methodName == "SelectAll") {
      browser->GetFocusedFrame()->SelectAll();
      return;
    }

    if (request.methodName == "OnMouseClick") {
      Browser_OnMouseClick arguments =
          request.arguments.get<Browser_OnMouseClick>();
      browser->GetHost()->SendMouseClickEvent(
          arguments.event,
          static_cast<CefBrowserHost::MouseButtonType>(arguments.button),
          arguments.mouseUp, arguments.clickCount);
      return;
    }

    if (request.methodName == "OnMouseMove") {
      Browser_OnMouseMove arguments =
          request.arguments.get<Browser_OnMouseMove>();
      browser->GetHost()->SendMouseMoveEvent(arguments.event,
                                             arguments.mouseLeave);
      return;
    }

    if (request.methodName == "OnMouseWheel") {
      Browser_OnMouseWheel arguments =
          request.arguments.get<Browser_OnMouseWheel>();
      browser->GetHost()->SendMouseWheelEvent(arguments.event, arguments.deltaX,
                                              arguments.deltaY);
      return;
    }

    if (request.methodName == "OnKeyboardEvent") {
      Browser_OnKeyboardEvent arguments =
          request.arguments.get<Browser_OnKeyboardEvent>();
      CefKeyEvent event;
      event.size = 28;
      event.type = arguments.type;
      event.modifiers = arguments.modifiers;
      event.windows_key_code = arguments.windowsKeyCode;
      event.native_key_code = arguments.nativeKeyCode;
      event.is_system_key = arguments.isSystemKey;
      event.character = 0;
      if (arguments.character.has_value()) {
        event.character = arguments.character.value()[0];
      }
      event.unmodified_character = 0;
      if (arguments.unmodifiedCharacter.has_value()) {
        event.unmodified_character = arguments.unmodifiedCharacter.value()[0];
      }
      event.focus_on_editable_field = arguments.focusOnEditableField ? 1 : 0;
      browser->GetHost()->SendKeyEvent(event);
      return;
    }

    if (request.methodName == "Close") {
      Browser_Close arguments = request.arguments.get<Browser_Close>();
      CefPostTask(TID_UI,
                  base::BindOnce(&BrowserProcessHandler::Browser_CloseRpc, this,
                                 browser, arguments.forceClose));
      return;
    }

    if (request.methodName == "TryClose") {
      CefPostTask(TID_UI,
                  base::BindOnce(&BrowserProcessHandler::Browser_TryCloseRpc,
                                 this, browser, request.id));
      return;
    }

    if (request.methodName == "StartDownload") {
      Browser_StartDownload arguments = request.arguments.get<Browser_StartDownload>();
      browser->GetHost()->StartDownload(arguments.url);
      return;
   }

    if (request.methodName == "DownloadImage") {
      Browser_DownloadImage arguments =
          request.arguments.get<Browser_DownloadImage>();
      CefRefPtr<DownloadImageCallback> callback =
          new DownloadImageCallback(this, request.id, arguments.imageUrl);
      browser->GetHost()->DownloadImage(
          arguments.imageUrl, arguments.isFavicon,
          static_cast<uint32_t>(arguments.maxImageSize), arguments.bypassCache,
          callback);
      return;
    }

    if (request.methodName == "GetSource") {
      CefRefPtr<CefFrame> frame = browser->GetMainFrame();
      CefRefPtr<GetSourceStringVisitor> visitor =
          new GetSourceStringVisitor(this, request.id);
      frame->GetSource(visitor);
      return;
    }

    if (request.methodName == "GetText") {
      CefRefPtr<CefFrame> frame = browser->GetMainFrame();
      CefRefPtr<GetSourceStringVisitor> visitor =
          new GetSourceStringVisitor(this, request.id);
      frame->GetText(visitor);
      return;
    }

    if (request.methodName == "GetFrameRate") {
      CefPostTask(TID_UI,
                  base::BindOnce(&BrowserProcessHandler::Browser_GetFrameRateRpc,
                                 this, browser, request.id));
      return;
    }

    if (request.methodName == "SetFrameRate") {
        Browser_SetFrameRate arguments =
          request.arguments.get<Browser_SetFrameRate>();
        browser->GetHost()->SetWindowlessFrameRate(arguments.frameRate);
        return;
    }
  }

  if (request.className == "HttpRequest") {
    auto it = httpRequestEntries.find(request.instanceId);
    if (it == httpRequestEntries.end()) {
      SDL_Log("RpcWorkerThread: HttpRequest instance %d not found",
              request.instanceId);
      return;
    }

    if (request.methodName == "Cancel") {
      CefPostTask(TID_UI, base::BindOnce(
                              &BrowserProcessHandler::HttpRequest_CancelRpc,
                              this, request.instanceId));
      return;
    }
  }

  SDL_Log("RpcWorkerThread: unknown message method '%s'",
          request.methodName.c_str());
}

void BrowserProcessHandler::HandleRpcResponse(RpcResponse response) {
  SDL_LockMutex(this->responseMapMutex);
  auto it = this->responseEntries.find(response.requestId);
  if (it != this->responseEntries.end()) {
    ResponseEntry* e = it->second.get();
    SDL_LockMutex(e->mutex);
    e->payload = response.returnValue.dump();
    e->ready = true;
    SDL_SignalCondition(e->cond);
    SDL_UnlockMutex(e->mutex);
  }
  SDL_UnlockMutex(this->responseMapMutex);
}

template <typename T>
std::optional<T> BrowserProcessHandler::WaitForResponse(UUID requestId) {
  std::unique_ptr<ResponseEntry> entry = std::make_unique<ResponseEntry>();

  // Insert into map under map mutex
  SDL_LockMutex(responseMapMutex);
  responseEntries[requestId] = std::move(entry);
  ResponseEntry* e = responseEntries[requestId].get();
  SDL_UnlockMutex(responseMapMutex);

  // Wait for signal
  SDL_LockMutex(e->mutex);
  while (!e->ready) {
    SDL_WaitCondition(e->cond, e->mutex);
  }
  SDL_UnlockMutex(e->mutex);

  std::string payload;
  SDL_LockMutex(responseMapMutex);
  auto it = responseEntries.find(requestId);
  if (it != responseEntries.end()) {
    ResponseEntry* entryPtr = it->second.get();
    SDL_LockMutex(entryPtr->mutex);
    payload = std::move(entryPtr->payload);
    SDL_UnlockMutex(entryPtr->mutex);
    responseEntries.erase(it);
  }
  SDL_UnlockMutex(responseMapMutex);

  try {
    json j = json::parse(payload);
    return j.get<T>();
  } catch (const std::exception& e) {
    size_t previewLen = std::min<size_t>(payload.size(), 256);
    std::string preview = payload.substr(0, previewLen);
    std::string msg = std::string("WaitForResponse: ") + e.what() +
                      " payload='" + preview + "'";
    this->SendLogMessage(SDL_LOG_PRIORITY_ERROR, msg);
    return std::nullopt;
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

      std::string jsonMessageString(recvBuffer.begin() + 4,
                          recvBuffer.begin() + 4 + msgLen);
      recvBuffer.erase(recvBuffer.begin(), recvBuffer.begin() + 4 + msgLen);

      json jsonMessage;
      try {
        jsonMessage = json::parse(jsonMessageString);
      } catch (const nlohmann::json::parse_error& e_parse) {
        size_t previewLen = std::min<size_t>(jsonMessageString.size(), 256);
        std::string preview = jsonMessageString.substr(0, previewLen);
        SDL_Log(
            "RpcReceiveThread: JSON parse_error: %s at byte=%u "
            "payload_preview='%s'",
            e_parse.what(), static_cast<unsigned int>(e_parse.byte),
            preview.c_str());
        continue;
      } catch (const std::exception& e) {
        SDL_Log("RpcReceiveThread: JSON exception: %s", e.what());
        continue;
      }

      try {
        if (!jsonMessage.contains("requestId")) {
          handler->HandleRpcRequest(jsonMessage.get<RpcRequest>());
        } else {
          handler->HandleRpcResponse(jsonMessage.get<RpcResponse>());
        }
      } catch (const nlohmann::json::exception& e) {
        SDL_Log("RpcReceiveThread: JSON exception during dispatch: %s", e.what());
        try {
          if (jsonMessage.contains("id")) {
            UUID requestId;
            from_json(jsonMessage.at("id"), requestId);
            handler->SendErrorResponse(requestId, e.what());
          }
        } catch (...) {
          SDL_Log("RpcReceiveThread: could not send error response");
        }
      } catch (const std::exception& e) {
        SDL_Log("RpcReceiveThread: exception during dispatch: %s", e.what());
      }
    }  // end while (recvBuffer.size() >= 4)
  }  // end while (true)
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

void BrowserProcessHandler::HttpRequest_CancelRpc(const int requestId) {
  auto it = httpRequestEntries.find(requestId);
  if (it != httpRequestEntries.end()) {
    it->second->Cancel();
    httpRequestEntries.erase(it);
  }
}

void BrowserProcessHandler::RemoveHttpRequest(const int requestId) {
  httpRequestEntries.erase(requestId);
}

template std::optional<std::monostate>
    BrowserProcessHandler::WaitForResponse<std::monostate>(UUID);
template std::optional<bool>
    BrowserProcessHandler::WaitForResponse<bool>(UUID);
template std::optional<std::string>
    BrowserProcessHandler::WaitForResponse<std::string>(UUID);
template std::optional<CefRect>
    BrowserProcessHandler::WaitForResponse<CefRect>(UUID);
template std::optional<ContextMenuConfiguration>
    BrowserProcessHandler::WaitForResponse<ContextMenuConfiguration>(UUID);
template std::optional<CefPoint>
    BrowserProcessHandler::WaitForResponse<CefPoint>(UUID);
template std::optional<DownloadConfiguration>
    BrowserProcessHandler::WaitForResponse<DownloadConfiguration>(UUID);
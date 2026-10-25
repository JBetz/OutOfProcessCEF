#include "browser_handler.h"
#include <SDL3/sdl.h>
#include <include/cef_scheme.h>
#include <rpc.h>
#include "browser_process_handler.h"
#include "rpc.hpp"

#include <windows.h>
#include <include/base/cef_bind.h>
#include <include/base/cef_callback.h>
#include <include/wrapper/cef_closure_task.h>

using json = nlohmann::json;

BrowserHandler::BrowserHandler(BrowserProcessHandler* browserProcessHandler,
                               CefPoint windowOrigin,
                               CefRect pageRectangle)
    : browserProcessHandler(browserProcessHandler),
      windowOrigin(windowOrigin),
      pageRectangle(pageRectangle) {}

void BrowserHandler::MarkCreated() {
  this->createdAt = std::chrono::steady_clock::now();
}

void BrowserHandler::MarkDestroyed() {
  this->destroyedAt = std::chrono::steady_clock::now();
}

void BrowserHandler::SetPageRectangle(CefRect rectangle) {
  pageRectangle = rectangle;
}

void BrowserHandler::SetWindowOrigin(CefPoint point) {
  windowOrigin = point;
}

std::optional<UUID> BrowserHandler::SendRpcRequest(
    CefRefPtr<CefBrowser> browser,
    std::string methodName,
    json arguments) {
  if (!createdAt.has_value()) {
    browserProcessHandler->SendLogMessage(SDL_LOG_PRIORITY_WARN,
        "Attempted to send RPC request '" + methodName +
        "' but browser has not been created yet");
    return std::nullopt;
  }
  if (destroyedAt.has_value()) {
    browserProcessHandler->SendLogMessage(SDL_LOG_PRIORITY_WARN,
        "Attempted to send RPC request '" + methodName +
        "' but browser has been destroyed");
    return std::nullopt;
  }
  RpcRequest request;
  request.id = CreateUuid();
  request.className = "Browser";
  request.methodName = methodName;
  request.instanceId = browser->GetIdentifier();
  request.arguments = arguments;
  json jsonRequest = request;
  browserProcessHandler->SendMessage(jsonRequest.dump());
  return request.id;
}

std::optional<UUID> BrowserHandler::SendRpcRequest(
    CefRefPtr<CefBrowser> browser,
    std::string methodName) {
  return this->SendRpcRequest(browser, methodName, json::object());
}

CefRefPtr<CefRenderHandler> BrowserHandler::GetRenderHandler() {
  return this;
}

CefRefPtr<CefDisplayHandler> BrowserHandler::GetDisplayHandler() {
  return this;
}

CefRefPtr<CefLifeSpanHandler> BrowserHandler::GetLifeSpanHandler() {
  return this;
}

CefRefPtr<CefContextMenuHandler> BrowserHandler::GetContextMenuHandler() {
  return this;
}

CefRefPtr<CefRequestHandler> BrowserHandler::GetRequestHandler() {
  return this;
}

CefRefPtr<CefLoadHandler> BrowserHandler::GetLoadHandler() {
  return this;
}

CefRefPtr<CefDownloadHandler> BrowserHandler::GetDownloadHandler() {
  return this;
}

bool BrowserHandler::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  if (!args || args->GetSize() == 0) {
    return false;
  }
  if (args->GetType(0) == VTYPE_STRING) {
    std::string payload = args->GetString(0).ToString();
    browserProcessHandler->BrowserProcessHandler::SendMessage(payload);
    return true;
  }
  return false;
}

void BrowserHandler::GetViewRect(CefRefPtr<CefBrowser> browser,
                                 CefRect& rect) {
  rect = pageRectangle;
}

void BrowserHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                             PaintElementType type,
                             const RectList& dirtyRects,
                             const void* buffer,
                             int width,
                             int height) {
  int bufferSize = width * height * 4;
  HANDLE fileMapping = CreateFileMappingW(
      INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, bufferSize, NULL);
  if (fileMapping == NULL) {
    SDL_Log("Error creating file mapping for OnPaint buffer");
    return;
  }
  void* mappedView =
      MapViewOfFile(fileMapping, FILE_MAP_WRITE, 0, 0, bufferSize);
  if (mappedView == NULL) {
    SDL_Log("Error mapping view of file for OnPaint buffer");
    CloseHandle(fileMapping);
    return;
  }
  memcpy(mappedView, buffer, bufferSize);
  UnmapViewOfFile(mappedView);

  HANDLE applicationProcessHandle =
      browserProcessHandler->GetApplicationProcessHandle();
  HANDLE duplicateHandle = NULL;
  bool duplicateSuccess = DuplicateHandle(
      GetCurrentProcess(), fileMapping, applicationProcessHandle,
      &duplicateHandle, 0, FALSE, DUPLICATE_SAME_ACCESS);
  CloseHandle(fileMapping);

  if (!duplicateSuccess) {
    SDL_Log("Error duplicating file mapping handle: DuplicateHandle() call failed");
    return;
  }
  
  Browser_OnPaint arguments;
  arguments.elementType = type;
  arguments.width = width;
  arguments.height = height;
  arguments.sharedMemoryHandle = reinterpret_cast<uintptr_t>(duplicateHandle);
  arguments.sharedMemorySize = bufferSize;
  for (const auto& rect : dirtyRects) {
    arguments.dirtyRects.push_back(rect);
  }
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnPaint", jsonArguments);
}

void BrowserHandler::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                                        PaintElementType type,
                                        const RectList& dirtyRects,
                                        const CefAcceleratedPaintInfo& info) {
  HANDLE sourceHandle = info.shared_texture_handle;
  HANDLE applicationProcessHandle =
      browserProcessHandler->GetApplicationProcessHandle();
  HANDLE duplicateHandle = NULL;
  bool duplicateSuccess = DuplicateHandle(
      GetCurrentProcess(), sourceHandle, applicationProcessHandle,
      &duplicateHandle, 0, FALSE, DUPLICATE_SAME_ACCESS);
  if (!duplicateSuccess) {
    SDL_Log("Error duplicating shared texture: DuplicateHandle() call failed");
    return;
  }
  Browser_OnAcceleratedPaint arguments;
  arguments.elementType = type;
  arguments.format = info.format;
  arguments.sharedTextureHandle = reinterpret_cast<uintptr_t>(duplicateHandle);
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnAcceleratedPaint", jsonArguments);
}

void BrowserHandler::OnTextSelectionChanged(CefRefPtr<CefBrowser> browser,
                                            const CefString& selected_text,
                                            const CefRange& selected_range) {
  Browser_OnTextSelectionChanged arguments;
  arguments.selectedText = selected_text.ToString();
  arguments.selectedRangeFrom = selected_range.from;
  arguments.selectedRangeTo = selected_range.to;
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnTextSelectionChanged", jsonArguments);
}

bool BrowserHandler::GetScreenInfo(CefRefPtr<CefBrowser> browser,
                                   CefScreenInfo& screen_info) {
  return false;
}

bool BrowserHandler::GetScreenPoint(CefRefPtr<CefBrowser> browser,
                                    int viewX,
                                    int viewY,
                                    int& screenX,
                                    int& screenY) {
  screenX = windowOrigin.x + pageRectangle.x + viewX;
  screenY = windowOrigin.y + pageRectangle.y + viewY;
  return true;
}

void BrowserHandler::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) {
  Browser_OnPopupShow arguments;
  arguments.show = show;
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnPopupShow", jsonArguments);
}

void BrowserHandler::OnPopupSize(CefRefPtr<CefBrowser> browser,
                                 const CefRect& rect) {
  Browser_OnPopupSize arguments;
  arguments.rectangle = rect;
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnPopupSize", jsonArguments);
}

void BrowserHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                   const CefString& title) {
  Browser_OnTitleChange arguments;
  arguments.title = title.ToString();
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnTitleChange", jsonArguments);
}

bool BrowserHandler::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                      cef_log_severity_t level,
                                      const CefString& message,
                                      const CefString& source,
                                      int line) {
  Browser_OnConsoleMessage arguments;
  arguments.level = static_cast<int>(level);
  arguments.message = message.ToString();
  arguments.source = source.ToString();
  arguments.line = line;
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnConsoleMessage", jsonArguments);
  return true;
}

void BrowserHandler::OnLoadingProgressChange(CefRefPtr<CefBrowser> browser,
                                             double progress) {
  Browser_OnLoadingProgressChange arguments;
  arguments.progress = progress;
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnLoadingProgressChange", jsonArguments);
}

void BrowserHandler::OnFaviconURLChange(
    CefRefPtr<CefBrowser> browser,
    const std::vector<CefString>& icon_urls) {
  Browser_OnFaviconUrlChange arguments;
  for (const auto& url : icon_urls) {
    arguments.iconUrls.push_back(url.ToString());
  }
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnFaviconUrlChange", jsonArguments);
}

bool BrowserHandler::OnCursorChange(CefRefPtr<CefBrowser> browser,
                                    CefCursorHandle cursor,
                                    cef_cursor_type_t type,
                                    const CefCursorInfo& custom_cursor_info) {
  Browser_OnCursorChange arguments;
  arguments.cursorType = static_cast<int>(type);
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnCursorChange", jsonArguments);
  return true;
}

bool BrowserHandler::DoClose(CefRefPtr<CefBrowser> browser) {
  return false;
}

void BrowserHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  this->MarkDestroyed();
  browserProcessHandler->RemoveBrowserHandler(browser->GetIdentifier());
  this->SendRpcRequest(browser, "OnBeforeClose");
}

bool BrowserHandler::OnBeforePopup(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    int popup_id,
    const CefString& target_url,
    const CefString& target_frame_name,
    CefLifeSpanHandler::WindowOpenDisposition target_disposition,
    bool user_gesture,
    const CefPopupFeatures& popupFeatures,
    CefWindowInfo& windowInfo,
    CefRefPtr<CefClient>& client,
    CefBrowserSettings& settings,
    CefRefPtr<CefDictionaryValue>& extra_info,
    bool* no_javascript_access) {
  Browser_OnBeforePopup arguments;
  arguments.targetUrl = target_url.ToString();
  arguments.targetFrameName = target_frame_name.ToString();
  arguments.targetDisposition = static_cast<int>(target_disposition);
  arguments.userGesture = user_gesture;
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnBeforePopup", jsonArguments);
  return true;
}

bool BrowserHandler::OnBeforeBrowse(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool user_gesture,
    bool is_redirect) {
  Browser_OnBeforeBrowse arguments;
  arguments.frameId = frame->GetIdentifier().ToString();
  arguments.url = request->GetURL().ToString();
  arguments.method = request->GetMethod().ToString();
  arguments.referrerUrl = request->GetReferrerURL().ToString();
  CefRequest::HeaderMap headerMap;
  request->GetHeaderMap(headerMap);
  for (const auto& [key, value] : headerMap) {
    arguments.headers[key.ToString()] = value.ToString();
  }
  arguments.isMainFrame = frame->IsMain();
  arguments.userGesture = user_gesture;
  arguments.isRedirect = is_redirect;
  arguments.transitionType =
      static_cast<int>(request->GetTransitionType());
  arguments.resourceType = static_cast<int>(request->GetResourceType());
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnBeforeBrowse", jsonArguments);
  return false;
}

bool BrowserHandler::OnOpenURLFromTab(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& target_url,
    CefLifeSpanHandler::WindowOpenDisposition target_disposition,
    bool user_gesture) {
  Browser_OnOpenUrlFromTab arguments;
  arguments.targetUrl = target_url.ToString();
  arguments.targetDisposition = static_cast<int>(target_disposition);
  arguments.userGesture = user_gesture;
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnOpenUrlFromTab", jsonArguments);
  return true;
}

void BrowserHandler::OnRenderProcessResponsive(CefRefPtr<CefBrowser> browser) {
  this->SendRpcRequest(browser, "OnRenderProcessResponsive");
}

void BrowserHandler::OnRenderProcessTerminated(
    CefRefPtr <CefBrowser> browser,
    TerminationStatus status,
    int error_code,
    const CefString& error_string) {
  Browser_OnRenderProcessTerminated arguments;
  arguments.status = static_cast<int>(status);
  arguments.errorCode = error_code;
  arguments.errorString = error_string.ToString();
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnRenderProcessTerminated", jsonArguments);
}

bool BrowserHandler::OnRenderProcessUnresponsive(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefUnresponsiveProcessCallback> callback) {
  this->SendRpcRequest(browser, "OnRenderProcessUnresponsive");
  return true;
}

void BrowserHandler::OnRenderViewReady(CefRefPtr<CefBrowser> browser) {
  this->SendRpcRequest(browser, "OnRenderViewReady");
}

void BrowserHandler::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         CefRefPtr<CefContextMenuParams> params,
                                         CefRefPtr<CefMenuModel> model) {}

bool BrowserHandler::RunContextMenu(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model,
    CefRefPtr<CefRunContextMenuCallback> callback) {
  Browser_RunContextMenu arguments;

  if (model) {
    size_t count = model->GetCount();
    for (size_t i = 0; i < count; ++i) {
      ContextMenuCommand cmd;
      cmd.index = static_cast<int>(i);
      cmd.commandId = model->GetCommandIdAt(i);
      cmd.label = model->GetLabelAt(i).ToString();
      cmd.enabled = model->IsEnabledAt(i);
      cmd.checked = model->IsCheckedAt(i);
      arguments.commands.push_back(std::move(cmd));
    }
  }

  json jsonArguments = arguments;
  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "RunContextMenu", arguments);
  if (!requestId.has_value()) {
    return false;
  }


  browserProcessHandler->RegisterPendingCallback(
      requestId.value(), [callback](const json& returnValue) {
        int commandId = returnValue.value("commandId", -1);
        CefPostTask(TID_UI,
                    base::BindOnce(&CefRunContextMenuCallback::Continue,
                                   callback.get(), commandId, EVENTFLAG_NONE));
      });

  return true;
}

bool BrowserHandler::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         CefRefPtr<CefContextMenuParams> params,
                                         int commandId,
                                         cef_event_flags_t eventFlags) {
  Browser_OnContextMenuCommand arguments;
  arguments.commandId = commandId;
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnContextMenuCommand", jsonArguments);
  return true;
}

void BrowserHandler::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                          bool isLoading,
                                          bool canGoBack,
                                          bool canGoForward) {
  Browser_OnLoadingStateChange arguments;
  arguments.isLoading = isLoading;
  arguments.canGoBack = canGoBack;
  arguments.canGoForward = canGoForward;
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnLoadingStateChange", jsonArguments);
}

void BrowserHandler::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 TransitionType transition_type) {
  if (!frame->IsMain()) {
    return;
  }
  Browser_OnLoadStart arguments;
  arguments.transitionType = static_cast<int>(transition_type);
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnLoadStart", jsonArguments);
}

void BrowserHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               int httpStatusCode) {
  if (!frame->IsMain()) {
    return;
  }
  Browser_OnLoadEnd arguments;
  arguments.httpStatusCode = httpStatusCode;
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnLoadEnd", jsonArguments);
}

void BrowserHandler::OnLoadError(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 ErrorCode errorCode,
                                 const CefString& errorText,
                                 const CefString& failedUrl) {
  if (!frame->IsMain()) {
    return;
  }
  Browser_OnLoadError arguments;
  arguments.errorCode = static_cast<int>(errorCode);
  arguments.errorText = errorText.ToString();
  arguments.failedUrl = failedUrl.ToString();
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnLoadError", jsonArguments);
}

bool BrowserHandler::OnTooltip(CefRefPtr<CefBrowser> browser,
                               CefString& text) {
  Browser_OnTooltip arguments;
  arguments.text = text.ToString();
  json jsonArguments = arguments;
  this->SendRpcRequest(browser, "OnTooltip", jsonArguments);
  return true;
}

bool BrowserHandler::CanDownload(CefRefPtr<CefBrowser> browser,
                                 const CefString& url,
                                 const CefString& request_method) {
  return true;
}

bool BrowserHandler::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefDownloadItem> download_item,
                                      const CefString& suggested_name,
                                      CefRefPtr<CefBeforeDownloadCallback> callback) {
  Browser_OnBeforeDownload arguments;
  DownloadItem downloadItem;
  downloadItem.isInProgress = download_item->IsInProgress();
  downloadItem.isComplete = download_item->IsComplete();
  downloadItem.isCanceled = download_item->IsCanceled();
  downloadItem.isInterrupted = download_item->IsInterrupted();
  downloadItem.isPaused = download_item->IsPaused();
  downloadItem.interruptReason = download_item->GetInterruptReason();
  downloadItem.currentSpeed = download_item->GetCurrentSpeed();
  downloadItem.percentComplete = download_item->GetPercentComplete();
  downloadItem.totalBytes = download_item->GetTotalBytes();
  downloadItem.receivedBytes = download_item->GetReceivedBytes();
  downloadItem.startTime = download_item->GetStartTime();
  downloadItem.endTime = download_item->GetEndTime();
  downloadItem.fullPath = download_item->GetFullPath().ToString();
  downloadItem.id = download_item->GetId();
  downloadItem.url = download_item->GetURL().ToString();
  downloadItem.originalUrl = download_item->GetOriginalUrl().ToString();
  downloadItem.suggestedFileName =
      download_item->GetSuggestedFileName().ToString();
  downloadItem.contentDisposition = download_item->GetContentDisposition();
  downloadItem.mimeType = download_item->GetMimeType();
  arguments.downloadItem = downloadItem;

  json jsonArguments = arguments;
  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "OnBeforeDownload", arguments);
  if (!requestId.has_value()) {
    return false;
  }

  browserProcessHandler->RegisterPendingCallback(requestId.value(), [callback](const json& returnValue) {
    DownloadConfiguration config = returnValue.get<DownloadConfiguration>();
    if (config.shouldContinue) {
        CefPostTask(TID_UI,
                    base::BindOnce(&CefBeforeDownloadCallback::Continue,
                                    callback.get(), config.downloadPath,
                                    config.showDialog));
    }
  });

  return true;
}

void BrowserHandler::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefDownloadItem> download_item,
                                       CefRefPtr<CefDownloadItemCallback> callback) {

  Browser_OnDownloadUpdated arguments;
  DownloadItem downloadItem;
  downloadItem.isInProgress = download_item->IsInProgress();
  downloadItem.isComplete = download_item->IsComplete();
  downloadItem.isCanceled = download_item->IsCanceled();
  downloadItem.isInterrupted = download_item->IsInterrupted();
  downloadItem.isPaused = download_item->IsPaused();
  downloadItem.interruptReason = download_item->GetInterruptReason();
  downloadItem.currentSpeed = download_item->GetCurrentSpeed();
  downloadItem.percentComplete = download_item->GetPercentComplete();
  downloadItem.totalBytes = download_item->GetTotalBytes();
  downloadItem.receivedBytes = download_item->GetReceivedBytes();
  downloadItem.startTime = download_item->GetStartTime();
  downloadItem.endTime = download_item->GetEndTime();
  downloadItem.fullPath = download_item->GetFullPath().ToString();
  downloadItem.id = download_item->GetId();
  downloadItem.url = download_item->GetURL().ToString();
  downloadItem.originalUrl = download_item->GetOriginalUrl().ToString();
  downloadItem.suggestedFileName =
      download_item->GetSuggestedFileName().ToString();
  downloadItem.contentDisposition = download_item->GetContentDisposition();
  downloadItem.mimeType = download_item->GetMimeType();
  arguments.downloadItem = downloadItem;
  json jsonArguments = arguments;
  std::optional<UUID> requestId = this->SendRpcRequest(browser, "OnDownloadUpdated", jsonArguments);
  if (!requestId.has_value()) {
    return;
  }

  CefRefPtr<BrowserHandler> self = this;
  browserProcessHandler->RegisterPendingCallback(
      requestId.value(), [self, callback](const json& returnValue) {
        std::string action = returnValue.get<std::string>();
        CefPostTask(TID_UI,
                    base::BindOnce(&BrowserHandler::DispatchDownloadAction,
                                   self, callback, std::move(action)));
      });
}


void BrowserHandler::DispatchDownloadAction(
    CefRefPtr<CefDownloadItemCallback> callback,
    std::string action) {
    if (action == "cancel") {
      callback->Cancel();
    } else if (action == "pause") {
      callback->Pause();
    } else if (action == "resume") {
      callback->Resume();
    } else {
      SDL_Log("Unrecognized download action: %s", action.c_str());
    }
}
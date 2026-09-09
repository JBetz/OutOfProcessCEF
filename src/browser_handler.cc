#include "browser_handler.h"
#include <SDL3/sdl.h>
#include <include/cef_scheme.h>
#include <rpc.h>
#include "browser_process_handler.h"
#include "rpc.hpp"

#include <windows.h>

using json = nlohmann::json;

BrowserHandler::BrowserHandler(BrowserProcessHandler* browserProcessHandler,
                               CefRect initialPageRectangle)
    : browserProcessHandler(browserProcessHandler),
      initialPageRectangle(initialPageRectangle) {}

void BrowserHandler::MarkCreated() {
  this->createdAt = std::chrono::steady_clock::now();
}

void BrowserHandler::MarkDestroyed() {
  this->destroyedAt = std::chrono::steady_clock::now();
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
  if (!createdAt.has_value()) {
    rect = initialPageRectangle;
    return;
  }
  std::optional<UUID> requestId = this->SendRpcRequest(browser, "GetViewRect");
  if (!requestId.has_value()) {
    rect = initialPageRectangle;
  } else {
    std::optional<CefRect> result =
        browserProcessHandler->WaitForResponse<CefRect>(requestId.value());
    rect = result.value_or(initialPageRectangle);
  }
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
  if (!DuplicateHandle(GetCurrentProcess(), fileMapping,
                       applicationProcessHandle, &duplicateHandle, 0, FALSE,
                       DUPLICATE_SAME_ACCESS)) {
    SDL_Log("Error duplicating file mapping handle: DuplicateHandle() call failed");
    CloseHandle(fileMapping);
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
  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "OnPaint", jsonArguments);
  if (!requestId.has_value()) {
    CloseHandle(fileMapping);
    return;
  }
  browserProcessHandler->WaitForResponse<std::monostate>(requestId.value());
  CloseHandle(fileMapping);
}

void BrowserHandler::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                                        PaintElementType type,
                                        const RectList& dirtyRects,
                                        const CefAcceleratedPaintInfo& info) {
  HANDLE sourceHandle = info.shared_texture_handle;
  HANDLE applicationProcessHandle =
      browserProcessHandler->GetApplicationProcessHandle();
  HANDLE duplicateHandle = NULL;
  if (!DuplicateHandle(GetCurrentProcess(), sourceHandle,
                       applicationProcessHandle, &duplicateHandle, 0, FALSE,
                       DUPLICATE_SAME_ACCESS)) {
    SDL_Log("Error duplicating shared texture: DuplicateHandle() call failed");
    return;
  }
  Browser_OnAcceleratedPaint arguments;
  arguments.elementType = type;
  arguments.format = info.format;
  arguments.sharedTextureHandle = reinterpret_cast<uintptr_t>(duplicateHandle);
  json jsonArguments = arguments;
  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "OnAcceleratedPaint", jsonArguments);
  if (!requestId.has_value()) {
    return;
  }
  browserProcessHandler->WaitForResponse<std::monostate>(requestId.value());
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
  Browser_GetScreenPoint arguments;
  arguments.view = CefPoint(viewX, viewY);
  json jsonArguments = arguments;
  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "GetScreenPoint", jsonArguments);
  if (!requestId.has_value()) {
    return false;
  }
  std::optional<CefPoint> result = browserProcessHandler->WaitForResponse<CefPoint>(requestId.value());
  if (!result.has_value()) {
    return false;
  }
  CefPoint screen = result.value();
  screenX = screen.x;
  screenY = screen.y;
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
  std::optional<UUID> requestId = this->SendRpcRequest(browser, "OnBeforeClose");
  if (!requestId.has_value()) {
    return;
  }
  browserProcessHandler->WaitForResponse<std::monostate>(requestId.value());
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
  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "OnBeforePopup", jsonArguments);
  if (!requestId.has_value()) {
    return true;
  }
  std::optional<bool> result =
      browserProcessHandler->WaitForResponse<bool>(requestId.value());
  return result.value_or(true);
}

bool BrowserHandler::OnBeforeBrowse(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool user_gesture,
    bool is_redirect) {
  Browser_OnBeforeBrowse arguments;
  arguments.browserId = browser->GetIdentifier();
  BrowseEvent browseEvent;
  browseEvent.url = request->GetURL().ToString();
  browseEvent.method = request->GetMethod().ToString();
  browseEvent.referrerUrl = request->GetReferrerURL().ToString();
  CefRequest::HeaderMap headerMap;
  request->GetHeaderMap(headerMap);
  for (const auto& [key, value] : headerMap) {
    browseEvent.headers[key.ToString()] = value.ToString();
  }
  browseEvent.userGesture = user_gesture;
  browseEvent.isRedirect = is_redirect;
  browseEvent.transitionType =
      static_cast<int>(request->GetTransitionType());
  browseEvent.resourceType = static_cast<int>(request->GetResourceType());
  arguments.browseEvent = browseEvent;

  json jsonArguments = arguments;
  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "OnBeforeBrowse", jsonArguments);
  if (!requestId.has_value()) {
    return false;
  }
  std::optional<bool> result =
      browserProcessHandler->WaitForResponse<bool>(requestId.value());
  return result.value_or(false);
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
  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "OnOpenUrlFromTab", jsonArguments);
  if (!requestId.has_value()) {
    return true;
  }
  std::optional<bool> result =
      browserProcessHandler->WaitForResponse<bool>(requestId.value());
  return result.value_or(true);
}

void BrowserHandler::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         CefRefPtr<CefContextMenuParams> params,
                                         CefRefPtr<CefMenuModel> model) {
  Browser_OnBeforeContextMenu arguments;
  arguments.origin = CefPoint(params->GetXCoord(), params->GetYCoord());
  arguments.nodeType = static_cast<int>(params->GetTypeFlags());
  arguments.nodeMedia = static_cast<int>(params->GetMediaType());
  arguments.nodeMediaStateFlags =
      static_cast<int>(params->GetMediaStateFlags());
  arguments.nodeEditFlags = static_cast<int>(params->GetEditStateFlags());
  arguments.selectionText = params->GetSelectionText().ToString();
  json jsonArguments = arguments;
  std::optional<UUID> requestId = this->SendRpcRequest(browser, "OnBeforeContextMenu", jsonArguments);
  if (!requestId.has_value()) {
    return;
  }
  std::optional<ContextMenuConfiguration> result =
      browserProcessHandler->WaitForResponse<ContextMenuConfiguration>(
          requestId.value());
  if (!result.has_value()) {
    return;
  }
  model->Clear();
  ContextMenuConfiguration config = result.value();
  for (const auto& command : config.commands) {
      model->InsertItemAt(command.index, command.commandId, command.label);
  }
}

bool BrowserHandler::RunContextMenu(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model,
    CefRefPtr<CefRunContextMenuCallback> callback) {
  return false;
}

bool BrowserHandler::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         CefRefPtr<CefContextMenuParams> params,
                                         int commandId,
                                         cef_event_flags_t eventFlags) {
  Browser_OnContextMenuCommand arguments;
  arguments.commandId = commandId;
  json jsonArguments = arguments;
  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "OnContextMenuCommand", jsonArguments);
  if (!requestId.has_value()) {
    return false;
  }
  std::optional<bool> result =
      browserProcessHandler->WaitForResponse<bool>(requestId.value());
  return result.value_or(false);
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
  Browser_CanDownload arguments;
  arguments.url = url.ToString();
  arguments.requestMethod = request_method.ToString();
  json jsonArguments = arguments;
  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "CanDownload", jsonArguments);
  if (!requestId.has_value()) {
    return false;
  }
  std::optional<bool> response =
      browserProcessHandler->WaitForResponse<bool>(requestId.value());
  return response.value_or(false);
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
  std::optional<UUID> requestId = this->SendRpcRequest(browser, "OnBeforeDownload", jsonArguments);
  if (!requestId.has_value()) {
    return false;
  }
  std::optional<DownloadConfiguration> response =
      browserProcessHandler->WaitForResponse<DownloadConfiguration>(requestId.value());
  if (!response.has_value() || !response.value().shouldContinue) {
    return false;
  }
  callback->Continue(response.value().downloadPath, response.value().showDialog);
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

  std::optional<UUID> requestId =
      this->SendRpcRequest(browser, "OnDownloadUpdated", jsonArguments);
  if (!requestId.has_value()) {
    callback->Pause();
    return;
  }
  std::optional<std::string> response =
      browserProcessHandler->WaitForResponse<std::string>(
          requestId.value());
  std::string action = response.value_or("pause");
  if (action == "cancel") {
    callback->Cancel(); 
  } else if (action == "pause") {
    callback->Pause();
  } else if (action == "resume") {
    callback->Resume();  
  } else {
    browserProcessHandler->SendErrorResponse(
        requestId.value(), "Unknown download action '" + action + "', defaulting to 'pause'.");
    callback->Pause();
  }
}


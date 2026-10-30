#include "browser_handler.h"

#include <windows.h>
#include <include/base/cef_bind.h>
#include <include/base/cef_callback.h>
#include <include/base/cef_logging.h>
#include <include/cef_parser.h>
#include <include/cef_scheme.h>
#include <include/wrapper/cef_closure_task.h>

#include "browser_process_handler.h"
#include "rpc.hpp"

BrowserHandler::BrowserHandler(CefRefPtr<BrowserProcessHandler> browserProcessHandler,
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
  CefString payload = CefWriteJSON(args->GetValue(0), JSON_WRITER_DEFAULT);
  browserProcessHandler->BrowserProcessHandler::SendMessage(payload);
  return true;
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
    LOG(ERROR) << "Error creating file mapping for OnPaint buffer";
    return;
  }
  void* mappedView =
      MapViewOfFile(fileMapping, FILE_MAP_WRITE, 0, 0, bufferSize);
  if (mappedView == NULL) {
    LOG(ERROR) << "Error mapping view of file for OnPaint buffer";
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
    LOG(ERROR) << "Error duplicating file mapping handle: DuplicateHandle() call failed";
    return;
  }
  
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetInt("elementType", type);
  arguments->SetInt("width", width);
  arguments->SetInt("height", height);
  arguments->SetInt("sharedMemoryHandle", reinterpret_cast<uintptr_t>(duplicateHandle));
  arguments->SetInt("sharedMemorySize", bufferSize);
  CefRefPtr<CefListValue> dirtyRectsList = CefListValue::Create();
  for (const auto& rect : dirtyRects) {
    CefRefPtr<CefDictionaryValue> rectangleDictionary = CefDictionaryValue::Create();
    rectangleDictionary->SetInt("x", rect.x);
    rectangleDictionary->SetInt("y", rect.y);
    rectangleDictionary->SetInt("width", rect.width);
    rectangleDictionary->SetInt("height", rect.height);
    CefRefPtr<CefValue> rectangleDictionaryValue = CefValue::Create();
    rectangleDictionaryValue->SetDictionary(rectangleDictionary);
    dirtyRectsList->SetValue(dirtyRectsList->GetSize(), rectangleDictionaryValue);
  }
  arguments->SetList("dirtyRects", dirtyRectsList);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnPaint", arguments);
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
    LOG(ERROR) << "Error duplicating shared texture: DuplicateHandle() call failed";
    return;
  }
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetInt("elementType", type);
  arguments->SetInt("format", info.format);
  arguments->SetInt("sharedTextureHandle", reinterpret_cast<uintptr_t>(duplicateHandle));
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnAcceleratedPaint", arguments);
}

void BrowserHandler::OnTextSelectionChanged(CefRefPtr<CefBrowser> browser,
                                            const CefString& selected_text,
                                            const CefRange& selected_range) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetString("selectedText", selected_text);
  arguments->SetInt("selectedRangeFrom", selected_range.from);
  arguments->SetInt("selectedRangeTo", selected_range.to);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(),
                                     "OnTextSelectionChanged", arguments);
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
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetBool("show", show);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(),
                                     "OnPopupShow", arguments);
}

void BrowserHandler::OnPopupSize(CefRefPtr<CefBrowser> browser,
                                 const CefRect& rect) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  CefRefPtr<CefDictionaryValue> rectangle = CefDictionaryValue::Create();
  rectangle->SetInt("x", rect.x);
  rectangle->SetInt("y", rect.y);
  rectangle->SetInt("width", rect.width);
  rectangle->SetInt("height", rect.height);
  arguments->SetDictionary("rectangle", rectangle);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnPopupSize", arguments);
}

void BrowserHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                   const CefString& title) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetString("title", title);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(),
                                     "OnTitleChange", arguments);
}

void BrowserHandler::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> frame,
                                     const CefString& url) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetString("frameId", frame->GetIdentifier());
  arguments->SetBool("isMainFrame", frame->IsMain());
  arguments->SetString("url", url);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(),
                                     "OnAddressChange", arguments);
}

bool BrowserHandler::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                      cef_log_severity_t level,
                                      const CefString& message,
                                      const CefString& source,
                                      int line) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetInt("level", static_cast<int>(level));
  arguments->SetString("message", message);
  arguments->SetString("source", source);
  arguments->SetInt("line", line);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(),
                                     "OnConsoleMessage", arguments);
  return true;
}

void BrowserHandler::OnLoadingProgressChange(CefRefPtr<CefBrowser> browser,
                                             double progress) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetDouble("progress", progress);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(),
                                     "OnLoadingProgressChange", arguments);
}

void BrowserHandler::OnFaviconURLChange(
    CefRefPtr<CefBrowser> browser,
    const std::vector<CefString>& icon_urls) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  CefRefPtr<CefListValue> iconUrlsList = CefListValue::Create();
  for (const auto& url : icon_urls) {
    iconUrlsList->SetString(iconUrlsList->GetSize(), url);
  }
  arguments->SetList("iconUrls", iconUrlsList);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(),
                                     "OnFaviconUrlChange", arguments);
}

bool BrowserHandler::OnCursorChange(CefRefPtr<CefBrowser> browser,
                                    CefCursorHandle cursor,
                                    cef_cursor_type_t type,
                                    const CefCursorInfo& custom_cursor_info) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetInt("cursor", reinterpret_cast<uintptr_t>(cursor));
  arguments->SetInt("type", static_cast<int>(type));
  if (type == cef_cursor_type_t::CT_CUSTOM) {
    CefRefPtr<CefDictionaryValue> customCursorInfo =
        CefDictionaryValue::Create();
    CefRefPtr<CefDictionaryValue> hotspot = CefDictionaryValue::Create();
    hotspot->SetInt("x", custom_cursor_info.hotspot.x);
    hotspot->SetInt("y", custom_cursor_info.hotspot.y);
    customCursorInfo->SetDictionary("hotspot", hotspot);
    customCursorInfo->SetDouble("imageScaleFactor",
                                custom_cursor_info.image_scale_factor);
    size_t bufferSize = static_cast<size_t>(custom_cursor_info.size.width) *
                        static_cast<size_t>(custom_cursor_info.size.height) * 4;
    CefRefPtr<CefBinaryValue> imageBuffer = CefBinaryValue::Create(
        custom_cursor_info.buffer, bufferSize);
    customCursorInfo->SetBinary("imageBuffer", imageBuffer);
    arguments->SetDictionary("customCursorInfo", customCursorInfo);
  }
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(),
                                     "OnCursorChange", arguments);
  return true;
}

bool BrowserHandler::DoClose(CefRefPtr<CefBrowser> browser) {
  return false;
}

void BrowserHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  this->MarkDestroyed();
  browserProcessHandler->RemoveBrowserHandler(browser->GetIdentifier());
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnBeforeClose");
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
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetString("targetUrl", target_url);
  arguments->SetString("targetFrameName", target_frame_name);
  arguments->SetInt("targetDisposition", static_cast<int>(target_disposition));
  arguments->SetBool("userGesture", user_gesture);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnBeforePopup", arguments);
  return true;
}

bool BrowserHandler::OnBeforeBrowse(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool user_gesture,
    bool is_redirect) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetString("frameId", frame->GetIdentifier());
  arguments->SetString("url", request->GetURL());
  arguments->SetString("method", request->GetMethod());
  arguments->SetString("referrerUrl", request->GetReferrerURL());
  CefRequest::HeaderMap headerMap;
  request->GetHeaderMap(headerMap);
  CefRefPtr<CefDictionaryValue> headersDictionary =
      CefDictionaryValue::Create();
  for (const auto& [key, value] : headerMap) {
     headersDictionary->SetString(key, value);
  }
  arguments->SetDictionary("headers", headersDictionary);
  arguments->SetBool("isMainFrame", frame->IsMain());
  arguments->SetBool("userGesture", user_gesture);
  arguments->SetBool("isRedirect", is_redirect);
  arguments->SetInt("transitionType",
                    static_cast<int>(request->GetTransitionType()));
  arguments->SetInt("resourceType", static_cast<int>(request->GetResourceType()));
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnBeforeBrowse", arguments);
  return false;
}

bool BrowserHandler::OnOpenURLFromTab(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& target_url,
    CefLifeSpanHandler::WindowOpenDisposition target_disposition,
    bool user_gesture) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetString("targetUrl", target_url);
  arguments->SetInt("targetDisposition", static_cast<int>(target_disposition));
  arguments->SetBool("userGesture", user_gesture);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnOpenUrlFromTab", arguments);  
  return true;
}

void BrowserHandler::OnRenderProcessResponsive(CefRefPtr<CefBrowser> browser) {
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnRenderProcessResponsive");
}

void BrowserHandler::OnRenderProcessTerminated(
    CefRefPtr <CefBrowser> browser,
    TerminationStatus status,
    int error_code,
    const CefString& error_string) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetInt("status", static_cast<int>(status));
  arguments->SetInt("errorCode", error_code);
  arguments->SetString("errorString", error_string);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnRenderProcessTerminated", arguments);
}

bool BrowserHandler::OnRenderProcessUnresponsive(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefUnresponsiveProcessCallback> callback) {
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnRenderProcessUnresponsive");
  return true;
}

void BrowserHandler::OnRenderViewReady(CefRefPtr<CefBrowser> browser) {
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnRenderViewReady");
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
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  CefRefPtr<CefListValue> commandsList = CefListValue::Create();
  if (model) {
    size_t count = model->GetCount();
    for (size_t i = 0; i < count; ++i) {
      CefRefPtr<CefDictionaryValue> command = CefDictionaryValue::Create();
      command->SetInt("index", static_cast<int>(i));
      command->SetInt("commandId", model->GetCommandIdAt(i));
      command->SetString("label", model->GetLabelAt(i));
      command->SetBool("enabled", model->IsEnabledAt(i));
      command->SetBool("checked", model->IsCheckedAt(i));
      commandsList->SetDictionary(i, command);
    }
  }
  arguments->SetList("commands", commandsList);
  std::optional<CefString> requestId =
      browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "RunContextMenu", arguments);
  if (!requestId.has_value()) {
    return false;
  }
  browserProcessHandler->RegisterPendingCallback(
      requestId.value(), [callback](CefRefPtr<CefValue> returnValue) {
        int commandId = returnValue->GetInt();
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
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetInt("commandId", commandId);
  arguments->SetInt("eventFlags", eventFlags);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnContextMenuCommand", arguments);
  return true;
}

void BrowserHandler::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                          bool isLoading,
                                          bool canGoBack,
                                          bool canGoForward) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetBool("isLoading", isLoading);
  arguments->SetBool("canGoBack", canGoBack);
  arguments->SetBool("canGoForward", canGoForward);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnLoadingStateChange", arguments);
}

void BrowserHandler::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 TransitionType transition_type) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetString("frameId", frame->GetIdentifier());
  arguments->SetBool("isMainFrame", frame->IsMain());
  arguments->SetInt("transitionType", static_cast<int>(transition_type));
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnLoadStart", arguments);
}

void BrowserHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               int httpStatusCode) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetString("frameId", frame->GetIdentifier());
  arguments->SetBool("isMainFrame", frame->IsMain());
  arguments->SetInt("httpStatusCode", httpStatusCode);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnLoadEnd", arguments);
}

void BrowserHandler::OnLoadError(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 ErrorCode errorCode,
                                 const CefString& errorText,
                                 const CefString& failedUrl) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetString("frameId", frame->GetIdentifier());
  arguments->SetBool("isMainFrame", frame->IsMain());
  arguments->SetInt("errorCode", errorCode);
  arguments->SetString("errorText", errorText);
  arguments->SetString("failedUrl", failedUrl);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnLoadError", arguments);
}

bool BrowserHandler::OnTooltip(CefRefPtr<CefBrowser> browser,
                               CefString& text) {
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  arguments->SetString("text", text);
  browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnTooltip", arguments);
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
  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  CefRefPtr<CefDictionaryValue> downloadItem = CefDictionaryValue::Create();
  downloadItem->SetBool("isInProgress", download_item->IsInProgress());
  downloadItem->SetBool("isComplete", download_item->IsComplete());
  downloadItem->SetBool("isCanceled", download_item->IsCanceled());
  downloadItem->SetBool("isInterrupted", download_item->IsInterrupted());
  downloadItem->SetBool("isPaused", download_item->IsPaused());
  downloadItem->SetInt("interruptReason", download_item->GetInterruptReason());
  downloadItem->SetInt("currentSpeed", download_item->GetCurrentSpeed());
  downloadItem->SetInt("percentComplete", download_item->GetPercentComplete());
  downloadItem->SetInt("totalBytes", download_item->GetTotalBytes());
  downloadItem->SetInt("receivedBytes", download_item->GetReceivedBytes());
  downloadItem->SetInt("startTime", download_item->GetStartTime().val);
  downloadItem->SetInt("endTime", download_item->GetEndTime().val);
  downloadItem->SetString("fullPath", download_item->GetFullPath());
  downloadItem->SetInt("id", download_item->GetId());
  downloadItem->SetString("url", download_item->GetURL());
  downloadItem->SetString("originalUrl", download_item->GetOriginalUrl());
  downloadItem->SetString("suggestedFileName", download_item->GetSuggestedFileName());
  downloadItem->SetString("contentDisposition", download_item->GetContentDisposition());
  downloadItem->SetString("mimeType", download_item->GetMimeType());
  arguments->SetDictionary("downloadItem", downloadItem);
  std::optional<CefString> requestId =
      browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnBeforeDownload", arguments);
  if (!requestId.has_value()) {
    return false;
  }
  browserProcessHandler->RegisterPendingCallback(requestId.value(), [callback](const CefRefPtr<CefValue> returnValue) {
    CefRefPtr<CefDictionaryValue> config = returnValue->GetDictionary();
    bool shouldContinue = config->GetBool("shouldContinue");
    CefString downloadPath = config->GetString("downloadPath");
    bool showDialog = config->GetBool("showDialog");
    if (shouldContinue) {
        CefPostTask(TID_UI,
                    base::BindOnce(&CefBeforeDownloadCallback::Continue,
                                    callback.get(), downloadPath,
                                    showDialog));
    }
  });

  return true;
}

void BrowserHandler::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefDownloadItem> download_item,
                                       CefRefPtr<CefDownloadItemCallback> callback) {

  CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
  CefRefPtr<CefDictionaryValue> downloadItem = CefDictionaryValue::Create();
  downloadItem->SetBool("isInProgress", download_item->IsInProgress());
  downloadItem->SetBool("isComplete", download_item->IsComplete());
  downloadItem->SetBool("isCanceled", download_item->IsCanceled());
  downloadItem->SetBool("isInterrupted", download_item->IsInterrupted());
  downloadItem->SetBool("isPaused", download_item->IsPaused());
  downloadItem->SetInt("interruptReason", download_item->GetInterruptReason());
  downloadItem->SetInt("currentSpeed", download_item->GetCurrentSpeed());
  downloadItem->SetInt("percentComplete", download_item->GetPercentComplete());
  downloadItem->SetInt("totalBytes", download_item->GetTotalBytes());
  downloadItem->SetInt("receivedBytes", download_item->GetReceivedBytes());
  downloadItem->SetInt("startTime", download_item->GetStartTime().val);
  downloadItem->SetInt("endTime", download_item->GetEndTime().val);
  downloadItem->SetString("fullPath", download_item->GetFullPath());
  downloadItem->SetInt("id", download_item->GetId());
  downloadItem->SetString("url", download_item->GetURL());
  downloadItem->SetString("originalUrl", download_item->GetOriginalUrl());
  downloadItem->SetString("suggestedFileName",
                          download_item->GetSuggestedFileName());
  downloadItem->SetString("contentDisposition",
                          download_item->GetContentDisposition());
  downloadItem->SetString("mimeType", download_item->GetMimeType());
  arguments->SetDictionary("downloadItem", downloadItem);
  std::optional<CefString> requestId = browserProcessHandler->SendRequest("Browser", browser->GetIdentifier(), "OnDownloadUpdated", arguments);
  if (!requestId.has_value()) {
    return;
  }
  CefRefPtr<BrowserHandler> self = this;
  browserProcessHandler->RegisterPendingCallback(
      requestId.value(), [self, callback](const CefRefPtr<CefValue> returnValue) {
        CefString action = returnValue->GetString();
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
      LOG(ERROR) << "Unrecognized download action: " << action;
    }
}
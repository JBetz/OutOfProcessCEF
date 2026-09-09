#pragma once

#include <rpc.h>
#include <optional>
#include <string>
#include <variant>
#include "include/internal/cef_types_wrappers.h"
#include "json.hpp"

using json = nlohmann::json;

// monostate - must be in nlohmann namespace for ADL to work
namespace nlohmann {
template <>
struct adl_serializer<std::monostate> {
  static void to_json(json& j, const std::monostate&) { j = nullptr; }

  static void from_json(const json&, std::monostate&) {
    // monostate has no state to restore
  }
};
}  // namespace nlohmann

// HANDLE
inline void to_json(json& j, const HANDLE& m) {
  j = static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(m));
}

inline void from_json(const json& j, HANDLE& m) {
  std::uint64_t v = j.get<std::uint64_t>();
  m = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(v));
}

// UUID
inline UUID CreateUuid() {
  UUID uuid;
  RPC_STATUS resultCode = UuidCreate(&uuid);
  if (resultCode == RPC_S_OK) {
    return uuid;
  } else {
    throw std::runtime_error("UuidCreate() failed with error code: " +
                             std::to_string(resultCode));
  }
}

inline void to_json(json& j, const UUID& m) {
  RPC_CSTR str;
  RPC_STATUS resultCode = UuidToStringA(const_cast<UUID*>(&m), &str);
  if (resultCode == RPC_S_OK) {
    j = std::string(reinterpret_cast<char*>(str));
    RpcStringFreeA(&str);
  } else {
    throw std::runtime_error("UuidToStringA() failed with error code: " +
                             std::to_string(resultCode));
  }
}

inline void from_json(const json& j, UUID& m) {
  std::string str = j.get<std::string>();
  RPC_CSTR rpcStr = reinterpret_cast<RPC_CSTR>(const_cast<char*>(str.c_str()));
  RPC_STATUS resultCode = UuidFromStringA(rpcStr, &m);
  if (resultCode != RPC_S_OK) {
    throw std::runtime_error("UuidFromStringA() failed with error code: " +
                             std::to_string(resultCode));
  }
}

// CEF types
inline void to_json(json& j, const CefRect& m) {
  j = json::object();
  j["x"] = m.x;
  j["y"] = m.y;
  j["width"] = m.width;
  j["height"] = m.height;
}

inline void from_json(const json& j, CefRect& r) {
  j.at("x").get_to(r.x);
  j.at("y").get_to(r.y);
  j.at("width").get_to(r.width);
  j.at("height").get_to(r.height);
}

inline void to_json(json& j, const CefPoint& m) {
  j = json::object();
  j["x"] = m.x;
  j["y"] = m.y;
}

inline void from_json(const json& j, CefPoint& m) {
  j.at("x").get_to(m.x);
  j.at("y").get_to(m.y);
}

inline void to_json(json& j, const CefSize& m) {
  j = json::object();
  j["width"] = m.width;
  j["height"] = m.height;
}

inline void to_json(json& j, const CefCursorInfo& m) {
  j = json::object();
  j["hotspot"] = m.hotspot;
  j["image_scale_factor"] = m.image_scale_factor;
  j["buffer"] = "";
  j["size"] = m.size;
}

inline void from_json(const json& j, CefMouseEvent& m) {
  j.at("x").get_to(m.x);
  j.at("y").get_to(m.y);
  j.at("modifiers").get_to(m.modifiers);
}

inline void from_json(const json& j, CefKeyEvent& m) {
  j.at("type").get_to(m.type);
  j.at("modifiers").get_to(m.modifiers);
  j.at("windows_key_code").get_to(m.windows_key_code);
  j.at("native_key_code").get_to(m.native_key_code);
  j.at("is_system_key").get_to(m.is_system_key);
  std::string character = j.at("character");
  m.character = character.empty() ? 0 : character[0];
  std::string unmodified_character = j.at("unmodified_character");
  m.unmodified_character =
      unmodified_character.empty() ? 0 : unmodified_character[0];
  j.at("focus_on_editable_field").get_to(m.focus_on_editable_field);
}

inline void to_json(json& j, const CefBaseTime& m) {
  j = m.val;
}

// Request messages
struct RpcRequest {
  UUID id;
  std::string className;
  std::string methodName;
  int instanceId;
  json arguments;
};

inline void from_json(const json& j, RpcRequest& m) {
  j.at("id").get_to(m.id);
  j.at("class").get_to(m.className);
  j.at("method").get_to(m.methodName);
  j.at("instanceId").get_to(m.instanceId);
  j.at("arguments").get_to(m.arguments);
}

inline void to_json(json& j, const RpcRequest& m) {
  j = json::object();
  j["id"] = m.id;
  j["class"] = m.className;
  j["method"] = m.methodName;
  j["instanceId"] = m.instanceId;
  j["arguments"] = m.arguments;
}

// Response messages
struct Client_CreateBrowser {
  std::string url;
  CefRect rectangle;
  std::optional<std::string> html;
  uintptr_t parentWindowHandle;
  bool windowless;
  bool hardwareAccelerated;
};

inline void from_json(const json& j, Client_CreateBrowser& m) {
  j.at("url").get_to(m.url);
  j.at("rectangle").get_to(m.rectangle);
  j.at("html").get_to(m.html);
  j.at("parentWindowHandle").get_to(m.parentWindowHandle);
  j.at("windowless").get_to(m.windowless);
  j.at("hardwareAccelerated").get_to(m.hardwareAccelerated);
}

struct Browser_EvalJavaScript {
  std::string code;
  std::string scriptUrl;
  int startLine;
};

inline void from_json(const json& j, Browser_EvalJavaScript& m) {
  j.at("code").get_to(m.code);
  j.at("scriptUrl").get_to(m.scriptUrl);
  j.at("startLine").get_to(m.startLine);
}

struct Browser_OnMouseClick {
  CefMouseEvent event;
  int button;
  bool mouseUp;
  int clickCount;
};

inline void from_json(const json& j, Browser_OnMouseClick& m) {
  j.at("event").get_to(m.event);
  j.at("button").get_to(m.button);
  j.at("mouseUp").get_to(m.mouseUp);
  j.at("clickCount").get_to(m.clickCount);
}

struct Browser_OnMouseMove {
  CefMouseEvent event;
  bool mouseLeave;
};

inline void from_json(const json& j, Browser_OnMouseMove& m) {
  j.at("event").get_to(m.event);
  j.at("mouseLeave").get_to(m.mouseLeave);
}

struct Browser_OnMouseWheel {
  CefMouseEvent event;
  int deltaX;
  int deltaY;
};

inline void from_json(const json& j, Browser_OnMouseWheel& m) {
  j.at("event").get_to(m.event);
  j.at("deltaX").get_to(m.deltaX);
  j.at("deltaY").get_to(m.deltaY);
}

struct Browser_OnKeyboardEvent {
  CefKeyEvent event;
};

inline void from_json(const json& j, Browser_OnKeyboardEvent& m) {
  j.at("event").get_to(m.event);
}

struct Browser_OnMouseOver {
  std::string tagName;
  std::optional<std::string> inputType;
  std::optional<std::string> href;
  CefRect rectangle;
};

inline void to_json(json& j, const Browser_OnMouseOver& m) {
  j = json::object();
  j["tagName"] = m.tagName;
  j["inputType"] = m.inputType;
  j["href"] = m.href;
  j["rectangle"] = m.rectangle;
}

struct Browser_FocusOut {
  std::optional<std::string> tagName;
  std::optional<std::string> inputType;
  std::optional<bool> isEditable;
};

inline void to_json(json& j, const Browser_FocusOut& m) {
  j = json::object();
  j["tagName"] = m.tagName;
  j["inputType"] = m.inputType;
  j["isEditable"] = m.isEditable;
}

// Response messages
struct RpcResponse {
  UUID requestId;
  bool success;
  json returnValue;
  json error;
};

inline void to_json(json& j, const RpcResponse& m) {
  j = json::object();
  j["requestId"] = m.requestId;
  j["success"] = m.success;
  j["returnValue"] = m.returnValue;
  j["error"] = m.error;
}

inline void from_json(const json& j, RpcResponse& m) {
  j.at("requestId").get_to(m.requestId);
  j.at("success").get_to(m.success);
  j.at("returnValue").get_to(m.returnValue);
  j.at("error").get_to(m.error);
}

struct EvalJavaScriptError {
  int endColumn;
  int endPosition;
  int lineNumber;
  std::string message;
  std::string scriptResourceName;
  std::string sourceLine;
  int startColumn;
  int startPosition;
};

inline void to_json(json& j, const EvalJavaScriptError& m) {
  j = json::object();
  j["endColumn"] = m.endColumn;
  j["endPosition"] = m.endPosition;
  j["lineNumber"] = m.lineNumber;
  j["message"] = m.message;
  j["scriptResourceName"] = m.scriptResourceName;
  j["sourceLine"] = m.sourceLine;
  j["startColumn"] = m.startColumn;
  j["startPosition"] = m.startPosition;
}

struct Browser_Focus {
  bool focus;
};

inline void from_json(const json& j, Browser_Focus& m) {
  j.at("focus").get_to(m.focus);
}

struct Browser_WasHidden {
  bool hidden;
};

inline void from_json(const json& j, Browser_WasHidden& m) {
  j.at("hidden").get_to(m.hidden);
}

struct Browser_LoadUrl {
  std::string url;
};

inline void from_json(const json& j, Browser_LoadUrl& m) {
  j.at("url").get_to(m.url);
}

struct PostDataElement {
  CefPostDataElement::Type type;
  std::optional<std::string> fileName;
  std::optional<std::vector<uint8_t>> bytes;
};

inline void from_json(const json& j, PostDataElement& m) {
  j.at("type").get_to(m.type);
  j.at("fileName").get_to(m.fileName);
  j.at("bytes").get_to(m.bytes);
}

struct Browser_LoadRequest {
  std::string url;
  std::string method;
  std::vector<PostDataElement> postData;
  std::map<std::string, std::string> headerMap;
};

inline void from_json(const json& j, Browser_LoadRequest& m) {
  j.at("url").get_to(m.url);
  j.at("method").get_to(m.method);
  j.at("postData").get_to(m.postData);
  j.at("headerMap").get_to(m.headerMap);
}

struct Browser_SetFrameRate {
  int frameRate;
};

inline void from_json(const json& j, Browser_SetFrameRate& m) {
  j.at("frameRate").get_to(m.frameRate);
}

struct Browser_OnAcceleratedPaint {
  int elementType;
  uintptr_t sharedTextureHandle;
  int format;
};

inline void to_json(json& j, const Browser_OnAcceleratedPaint& m) {
  j = json::object();
  j["elementType"] = m.elementType;
  j["sharedTextureHandle"] = m.sharedTextureHandle;
  j["format"] = m.format;
}

struct Browser_OnTextSelectionChanged {
  std::string selectedText;
  int selectedRangeFrom;
  int selectedRangeTo;
};

inline void to_json(json& j, const Browser_OnTextSelectionChanged& m) {
  j = json::object();
  j["selectedText"] = m.selectedText;
  j["selectedRangeFrom"] = m.selectedRangeFrom;
  j["selectedRangeTo"] = m.selectedRangeTo;
}

struct Browser_OnCursorChange {
  int cursorType;
  std::optional<CefCursorInfo> customCursorInfo;
};

inline void to_json(json& j, const Browser_OnCursorChange& m) {
  j = json::object();
  j["cursorType"] = m.cursorType;
  if (m.customCursorInfo.has_value()) {
    j["customCursorInfo"] = m.customCursorInfo.value();
  } else {
    j["customCursorInfo"] = nullptr;
  }
}

struct Browser_OnAddressChange {
  std::string url;
};

inline void to_json(json& j, const Browser_OnAddressChange& m) {
  j = json::object();
  j["url"] = m.url;
}

struct Browser_OnTitleChange {
  std::string title;
};

inline void to_json(json& j, const Browser_OnTitleChange& m) {
  j = json::object();
  j["title"] = m.title;
}

struct Browser_OnConsoleMessage {
  int level;
  std::string message;
  std::string source;
  int line;
};

inline void to_json(json& j, const Browser_OnConsoleMessage& m) {
  j = json::object();
  j["level"] = m.level;
  j["message"] = m.message;
  j["source"] = m.source;
  j["line"] = m.line;
}

struct Browser_OnLoadingProgressChange {
  double progress;
};

inline void to_json(json& j, const Browser_OnLoadingProgressChange& m) {
  j = json::object();
  j["progress"] = m.progress;
}

struct Browser_OnFaviconUrlChange {
  std::vector<std::string> iconUrls;
};

inline void to_json(json& j, const Browser_OnFaviconUrlChange& m) {
  j = json::object();
  j["iconUrls"] = m.iconUrls;
}

struct Browser_OnFocusedNodeChanged {
  std::string tagName;
  std::optional<std::string> inputType;
  bool isEditable;
};

inline void to_json(json& j, const Browser_OnFocusedNodeChanged& m) {
  j = json::object();
  j["tagName"] = m.tagName;
  j["inputType"] = m.inputType;
  j["isEditable"] = m.isEditable;
}

struct Browser_Close {
  bool forceClose;
};

inline void from_json(const json& j, Browser_Close& m) {
  j.at("forceClose").get_to(m.forceClose);
}

struct Browser_OnBeforeContextMenu {
  CefPoint origin;
  int nodeType;
  int nodeMedia;
  int nodeMediaStateFlags;
  int nodeEditFlags;
  std::string selectionText;
};

inline void to_json(json& j, const Browser_OnBeforeContextMenu& m) {
  j = json::object();
  j["origin"] = m.origin;
  j["nodeType"] = m.nodeType;
  j["nodeMedia"] = m.nodeMedia;
  j["nodeMediaStateFlags"] = m.nodeMediaStateFlags;
  j["nodeEditFlags"] = m.nodeEditFlags;
  j["selectionText"] = m.selectionText;
}

struct ContextMenuCommand {
  int index;
  int commandId;
  std::string label;
};

inline void from_json(const json& j, ContextMenuCommand& m) {
  j.at("index").get_to(m.index);
  j.at("commandId").get_to(m.commandId);
  j.at("label").get_to(m.label);
}

struct ContextMenuConfiguration {
  std::vector<ContextMenuCommand> commands;
};

inline void from_json(const json& j, ContextMenuConfiguration& m) {
  j.at("commands").get_to(m.commands);
}

struct Browser_OnPopupShow {
  bool show;
};

inline void to_json(json& j, const Browser_OnPopupShow& m) {
  j = json::object();
  j["show"] = m.show;
}

struct Browser_OnPopupSize {
  CefRect rectangle;
};

inline void to_json(json& j, const Browser_OnPopupSize& m) {
  j = json::object();
  j["rectangle"] = m.rectangle;
}

struct Browser_OnBeforePopup {
  std::string targetUrl;
  std::string targetFrameName;
  int targetDisposition;
  bool userGesture;
};

inline void to_json(json& j, const Browser_OnBeforePopup& m) {
  j = json::object();
  j["targetUrl"] = m.targetUrl;
  j["targetFrameName"] = m.targetFrameName;
  j["targetDisposition"] = m.targetDisposition;
  j["userGesture"] = m.userGesture;
}

struct Browser_OnOpenUrlFromTab {
  std::string targetUrl;
  int targetDisposition;
  bool userGesture;
};

inline void to_json(json& j, const Browser_OnOpenUrlFromTab& m) {
  j = json::object();
  j["targetUrl"] = m.targetUrl;
  j["targetDisposition"] = m.targetDisposition;
  j["userGesture"] = m.userGesture;
}

struct Browser_DownloadImage {
  std::string imageUrl;
  bool isFavicon;
  int maxImageSize;
  bool bypassCache;
};

inline void from_json(const json& j, Browser_DownloadImage& m) {
  j.at("imageUrl").get_to(m.imageUrl);
  j.at("isFavicon").get_to(m.isFavicon);
  j.at("maxImageSize").get_to(m.maxImageSize);
  j.at("bypassCache").get_to(m.bypassCache);
}

struct PNGImageData {
  std::vector<uint8_t> data;
  int width;
  int height;
};

inline void to_json(json& j, const PNGImageData& m) {
  j = json::object();
  j["data"] = m.data;
  j["width"] = m.width;
  j["height"] = m.height;
}

struct Browser_DownloadImageResponse {
  std::string imageUrl;
  int httpStatusCode;
  std::vector<PNGImageData> images;
};

inline void to_json(json& j, const Browser_DownloadImageResponse& m) {
  j = json::object();
  j["imageUrl"] = m.imageUrl;
  j["httpStatusCode"] = m.httpStatusCode;
  j["images"] = m.images;
}

struct Browser_OnLoadingStateChange {
  bool isLoading;
  bool canGoBack;
  bool canGoForward;
};

inline void to_json(json& j, const Browser_OnLoadingStateChange& m) {
  j = json::object();
  j["isLoading"] = m.isLoading;
  j["canGoBack"] = m.canGoBack;
  j["canGoForward"] = m.canGoForward;
}

struct Browser_OnLoadStart {
  int transitionType;
};

inline void to_json(json& j, const Browser_OnLoadStart& m) {
  j = json::object();
  j["transitionType"] = m.transitionType;
}

struct Browser_OnLoadEnd {
  int httpStatusCode;
};

inline void to_json(json& j, const Browser_OnLoadEnd& m) {
  j = json::object();
  j["httpStatusCode"] = m.httpStatusCode;
}

struct Browser_OnLoadError {
  int errorCode;
  std::string errorText;
  std::string failedUrl;
};

inline void to_json(json& j, const Browser_OnLoadError& m) {
  j = json::object();
  j["errorCode"] = m.errorCode;
  j["errorText"] = m.errorText;
  j["failedUrl"] = m.failedUrl;
}

struct Browser_GetScreenPoint {
  CefPoint view;
};

inline void to_json(json& j, const Browser_GetScreenPoint& m) {
  j = json::object();
  j["view"] = m.view;
}

struct Browser_OnContextMenuCommand {
  int commandId;
  int eventFlags;
};

inline void to_json(json& j, const Browser_OnContextMenuCommand& m) {
  j = json::object();
  j["commandId"] = m.commandId;
  j["eventFlags"] = m.eventFlags;
}

struct Browser_OnTooltip {
  std::string text;
};

inline void to_json(json& j, const Browser_OnTooltip& m) {
  j = json::object();
  j["text"] = m.text;
}

struct Browser_OnPaint {
  int elementType;
  int width;
  int height;
  std::vector<CefRect> dirtyRects;
  uintptr_t sharedMemoryHandle;
  int sharedMemorySize;
};

inline void to_json(json& j, const Browser_OnPaint& m) {
  j = json::object();
  j["elementType"] = m.elementType;
  j["width"] = m.width;
  j["height"] = m.height;
  j["dirtyRects"] = m.dirtyRects;
  j["sharedMemoryHandle"] = m.sharedMemoryHandle;
  j["sharedMemorySize"] = m.sharedMemorySize;
}

struct BrowseEvent {
  std::string url;
  std::string method;
  std::string referrerUrl;
  std::map<std::string, std::string> headers;
  bool userGesture;
  bool isRedirect;
  int transitionType;
  int resourceType;
};

inline void to_json(json& j, const BrowseEvent& m) {
  j = json::object();
  j["url"] = m.url;
  j["method"] = m.method;
  j["referrerUrl"] = m.referrerUrl;
  j["headers"] = m.headers;
  j["userGesture"] = m.userGesture;
  j["isRedirect"] = m.isRedirect;
  j["transitionType"] = m.transitionType;
  j["resourceType"] = m.resourceType;
}

struct Browser_OnBeforeBrowse {
  std::string browserId;
  BrowseEvent browseEvent;
};

inline void to_json(json& j, const Browser_OnBeforeBrowse& m) {
  j = json::object();
  j["browserId"] = m.browserId;
  j["browseEvent"] = m.browseEvent;
}

struct Browser_OnPushState {
  std::string state;
  std::string url;
};

inline void to_json(json& j, const Browser_OnPushState& m) {
  j = json::object();
  j["state"] = m.state;
  j["url"] = m.url;
}

struct Browser_OnReplaceState {
  std::string state;
  std::string url;
};

inline void to_json(json& j, const Browser_OnReplaceState& m) {
  j = json::object();
  j["state"] = m.state;
  j["url"] = m.url;
}

struct Browser_OnNavigateByUrl {
  std::string url;
  std::string state;
  std::string history;
  std::string info;
};

inline void to_json(json& j, const Browser_OnNavigateByUrl& m) {
  j = json::object();
  j["url"] = m.url;
  j["state"] = m.state;
  j["history"] = m.history;
  j["info"] = m.info;
}

struct Browser_OnNavigateByDelta {
  int delta;
  std::string info;
};

inline void to_json(json& j, const Browser_OnNavigateByDelta& m) {
  j = json::object();
  j["delta"] = m.delta;
  j["info"] = m.info;
}

struct Browser_OnNavigateByKey {
  std::string key;
  std::string info;
};

inline void to_json(json& j, const Browser_OnNavigateByKey& m) {
  j = json::object();
  j["key"] = m.key;
  j["info"] = m.info;
}

struct Browser_CanDownload {
  std::string url;
  std::string requestMethod;
};

struct DownloadItem {
  bool isInProgress;
  bool isComplete;
  bool isCanceled;
  bool isInterrupted;
  bool isPaused;
  int interruptReason;
  int currentSpeed;
  int percentComplete;
  int totalBytes;
  int receivedBytes;
  CefBaseTime startTime;
  CefBaseTime endTime;
  std::string fullPath;
  int id;
  std::string url;
  std::string originalUrl;
  std::string suggestedFileName;
  std::string contentDisposition;
  std::string mimeType;
};

inline void to_json(json& j, const DownloadItem& m) {
  j = json::object();
  j["isInProgress"] = m.isInProgress;
  j["isComplete"] = m.isComplete;
  j["isCanceled"] = m.isCanceled;
  j["isInterrupted"] = m.isInterrupted;
  j["isPaused"] = m.isPaused;
  j["interruptReason"] = m.interruptReason;
  j["currentSpeed"] = m.currentSpeed;
  j["percentComplete"] = m.percentComplete;
  j["totalBytes"] = m.totalBytes;
  j["receivedBytes"] = m.receivedBytes;
  j["startTime"] = m.startTime;
  j["endTime"] = m.endTime;
  j["fullPath"] = m.fullPath;
  j["id"] = m.id;
  j["url"] = m.url;
  j["originalUrl"] = m.originalUrl;
  j["suggestedFileName"] = m.suggestedFileName;
  j["contentDisposition"] = m.contentDisposition;
  j["mimeType"] = m.mimeType;
}

inline void to_json(json& j, const Browser_CanDownload& m) {
  j = json::object();
  j["url"] = m.url;
  j["requestMethod"] = m.requestMethod;
}

struct Browser_OnBeforeDownload {
  DownloadItem downloadItem;
};

inline void to_json(json& j, const Browser_OnBeforeDownload& m) {
  j = json::object();
  j["downloadItem"] = m.downloadItem;
}

struct Browser_OnDownloadUpdated {
  DownloadItem downloadItem;
};

inline void to_json(json& j, const Browser_OnDownloadUpdated& m) {
  j = json::object();
  j["downloadItem"] = m.downloadItem;
}

struct DownloadConfiguration {
  bool shouldContinue;
  std::string downloadPath;
  bool showDialog;
};


inline void from_json(const json& j, DownloadConfiguration& m) {
  j.at("shouldContinue").get_to(m.shouldContinue);
  j.at("downloadPath").get_to(m.downloadPath);
  j.at("showDialog").get_to(m.showDialog);
}

enum DownloadAction { DOWNLOAD_CANCEL, DOWNLOAD_PAUSE, DOWNLOAD_RESUME };

struct Client_CreateNetworkRequest {
  std::string url;
  std::string method;
  std::optional<std::map<std::string, std::string>> headers;
  std::optional<std::vector<uint8_t>> body;
};

inline void from_json(const json& j, Client_CreateNetworkRequest& m) {
  j.at("url").get_to(m.url);
  j.at("method").get_to(m.method);
  if (j.contains("headers") && !j.at("headers").is_null()) {
    m.headers = j.at("headers").get<std::map<std::string, std::string>>();
  }
  if (j.contains("body") && !j.at("body").is_null()) {
    m.body = j.at("body").get<std::vector<uint8_t>>();
  }
}

struct Client_CreateHttpRequest {
  std::string url;
  std::string method;
  std::optional<std::map<std::string, std::string>> headers;
  std::optional<json> body;
};

inline void from_json(const json& j, Client_CreateHttpRequest& m) {
  j.at("url").get_to(m.url);
  j.at("method").get_to(m.method);
  if (j.contains("headers") && !j.at("headers").is_null()) {
    m.headers = j.at("headers").get<std::map<std::string, std::string>>();
  }
  if (j.contains("body") && !j.at("body").is_null()) {
    m.body = j.at("body").get<json>();
  }
}

struct HttpClient_OnResponse {
  int statusCode;
  std::string statusText;
  std::map<std::string, std::string> headers;
};

inline void to_json(json& j, const HttpClient_OnResponse& m) {
  j = json::object();
  j["statusCode"] = m.statusCode;
  j["statusText"] = m.statusText;
  j["headers"] = m.headers;
}

struct HttpClient_OnData {
  std::vector<uint8_t> data;
};

inline void to_json(json& j, const HttpClient_OnData& m) {
  j = json::object();
  j["data"] = m.data;
}

struct HttpClient_OnError {
  int requestStatus;
  int errorCode;
};

inline void to_json(json& j, const HttpClient_OnError& m) {
  j = json::object();
  j["requestStatus"] = m.requestStatus;
  j["errorCode"] = m.errorCode;
}


#include "browser_rpc_commands.h"

#include <include/base/cef_bind.h>
#include <include/base/cef_callback.h>
#include <include/wrapper/cef_closure_task.h>

#include "browser_handler.h"
#include "browser_process_handler.h"

class GetSourceStringVisitor : public CefStringVisitor {
 public:
  GetSourceStringVisitor(BrowserProcessHandler* handler,
                         const CefString& requestId)
      : handler(handler), requestId(requestId) {}

  void Visit(const CefString& string) override {
    CefRefPtr<CefValue> returnValue = CefValue::Create();
    returnValue->SetString(string);
    handler->SendResponse(requestId, returnValue);
  }

 private:
  BrowserProcessHandler* handler;
  CefString requestId;

  IMPLEMENT_REFCOUNTING(GetSourceStringVisitor);
};

class DownloadImageCallback : public CefDownloadImageCallback {
 public:
  DownloadImageCallback(BrowserProcessHandler* handler,
                        const CefString& requestId,
                        const std::string& imageUrl)
      : handler(handler), requestId(requestId), imageUrl(imageUrl) {}

  void OnDownloadImageFinished(const CefString& image_url,
                               int http_status_code,
                               CefRefPtr<CefImage> image_) override {
    CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
    arguments->SetString("imageUrl", imageUrl);
    arguments->SetInt("httpStatusCode", http_status_code);
    CefRefPtr<CefListValue> images = CefListValue::Create();
    if (image_ && !image_->IsEmpty()) {
      float scale_factor = 1.0f;
      int pixel_width = 0;
      int pixel_height = 0;

      CefRefPtr<CefBinaryValue> binary =
          image_->GetAsPNG(scale_factor, true, pixel_width, pixel_height);
      if (binary && binary->GetSize() > 0) {
        CefRefPtr<CefDictionaryValue> image = CefDictionaryValue::Create();
        image->SetBinary("data", binary);
        image->SetInt("width", pixel_width);
        image->SetInt("height", pixel_height);
        images->SetDictionary(images->GetSize(), image);
      }
    }
    arguments->SetList("images", images);
    CefRefPtr<CefValue> returnValue = CefValue::Create();
    returnValue->SetDictionary(arguments);
    handler->SendResponse(requestId, returnValue);
  }

 private:
  BrowserProcessHandler* handler;
  CefString requestId;
  std::string imageUrl;

  IMPLEMENT_REFCOUNTING(DownloadImageCallback);
};

void LoadUrlCommand::ReadArguments() {
  Require("url", url);
}

void LoadUrlCommand::Execute(BrowserProcessHandler* handler,
                             CefRefPtr<CefBrowser> browser,
                             CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetMainFrame()->LoadURL(url);
}

void LoadRequestCommand::ReadArguments() {
  Require("url", url);
  Require("method", method);
  Require("headers", headers);
  Require("postData", postData);
}

void LoadRequestCommand::Execute(BrowserProcessHandler* handler,
                                CefRefPtr<CefBrowser> browser,
                                CefRefPtr<BrowserHandler> browserHandler) {
  auto frame = browser->GetMainFrame();
  auto request = CefRequest::Create();
  request->SetURL(url);
  request->SetMethod(method);

  if (!postData.empty()) {
    auto cefPostData = CefPostData::Create();
    for (const auto& element : postData) {
      cefPostData->AddElement(element);
    }
    request->SetPostData(cefPostData);
  }

  for (const auto& [key, value] : headers) {
    request->SetHeaderByName(key, value, true);
  }

  frame->LoadRequest(request);
}

void ReloadCommand::Execute(BrowserProcessHandler* handler,
                            CefRefPtr<CefBrowser> browser, 
                            CefRefPtr<BrowserHandler> browserHandler) {
  browser->Reload();
}

void EvaluateJavaScriptCommand::ReadArguments() {
  Require("code", code);
  Optional("scriptUrl", scriptUrl);
  Optional("startLine", startLine);
}

void EvaluateJavaScriptCommand::Execute(
    BrowserProcessHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<BrowserHandler> browserHandler) {
  CefRefPtr<CefProcessMessage> message =
      CefProcessMessage::Create("Eval");
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, id);
  args->SetString(1, code);
  args->SetString(2, scriptUrl.value_or(CefString("unknown")));
  args->SetInt(3, startLine.value_or(1));
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
}

void OnMouseClickCommand::ReadArguments() {
  Require("event", event);
  Require("button", button);
  Require("mouseUp", mouseUp);
  Require("clickCount", clickCount);
}

void OnMouseClickCommand::Execute(
    BrowserProcessHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetHost()->SendMouseClickEvent(
      event, static_cast<CefBrowserHost::MouseButtonType>(button), mouseUp,
      clickCount);
}

void OnMouseMoveCommand::ReadArguments() {
  Require("event", event);
  Require("mouseLeave", mouseLeave);
}

void OnMouseMoveCommand::Execute(
    BrowserProcessHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetHost()->SendMouseMoveEvent(event, mouseLeave);
}

void OnMouseWheelCommand::ReadArguments() {
  Require("event", event);
  Require("deltaX", deltaX);
  Require("deltaY", deltaY);
}

void OnMouseWheelCommand::Execute(
    BrowserProcessHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetHost()->SendMouseWheelEvent(event, deltaX, deltaY);
}

void OnKeyboardEventCommand::ReadArguments() {
  Require("type", type);
  Require("modifiers", modifiers);
  Require("windowsKeyCode", windowsKeyCode);
  Require("nativeKeyCode", nativeKeyCode);
  Require("isSystemKey", isSystemKey);
  Optional("character", character);
  Optional("unmodifiedCharacter", unmodifiedCharacter);
  Require("focusOnEditableField", focusOnEditableField);
}

void OnKeyboardEventCommand::Execute(
    BrowserProcessHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<BrowserHandler> browserHandler) {
  CefKeyEvent keyEvent;
  keyEvent.size = sizeof(CefKeyEvent);
  keyEvent.type = static_cast<cef_key_event_type_t>(type);
  keyEvent.modifiers = modifiers;
  keyEvent.windows_key_code = windowsKeyCode;
  keyEvent.native_key_code = nativeKeyCode;
  keyEvent.is_system_key = isSystemKey;
  keyEvent.character = (!character.has_value() || character->empty())
                           ? 0
                           : character->c_str()[0];
  keyEvent.unmodified_character =
      (!unmodifiedCharacter.has_value() || unmodifiedCharacter->empty())
          ? 0
          : unmodifiedCharacter->c_str()[0];
  keyEvent.focus_on_editable_field = focusOnEditableField;
  browser->GetHost()->SendKeyEvent(keyEvent);
}

void FocusCommand::ReadArguments() {
  Require("focus", focus);
}

void FocusCommand::Execute(BrowserProcessHandler* handler,
                                  CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetHost()->SetFocus(focus);
}

void WasHiddenCommand::ReadArguments() {
  Require("hidden", hidden);
}

void WasHiddenCommand::Execute(
    BrowserProcessHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetHost()->WasHidden(hidden);
}

void WasResizedCommand::ReadArguments() {
  Require("rectangle", rectangle);
}

void WasResizedCommand::Execute(
    BrowserProcessHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<BrowserHandler> browserHandler) {
  browserHandler->SetPageRectangle(rectangle);
  browser->GetHost()->WasResized();
}

void SetWindowOriginCommand::ReadArguments() {
  Require("origin", origin);
}

void SetWindowOriginCommand::Execute(
    BrowserProcessHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<BrowserHandler> browserHandler) {
  browserHandler->SetWindowOrigin(origin);
}

void GetFrameRateCommand::Execute(
    BrowserProcessHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<BrowserHandler> browserHandler) {
  CefPostTask(TID_UI, base::BindOnce(&BrowserProcessHandler::Browser_GetFrameRateRpc,
                             handler, browser, id));
}

void SetFrameRateCommand::ReadArguments() {
  Require("frameRate", frameRate);
}

void SetFrameRateCommand::Execute(
    BrowserProcessHandler* handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetHost()->SetWindowlessFrameRate(frameRate);
}

void CloseCommand::ReadArguments() {
  Require("forceClose", forceClose);
}

void CloseCommand::Execute(BrowserProcessHandler* handler,
                                  CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<BrowserHandler> browserHandler) {
  CefPostTask(TID_UI, base::BindOnce(&BrowserProcessHandler::Browser_CloseRpc,
                                     handler, browser, forceClose));
}

void TryCloseCommand::Execute(BrowserProcessHandler* handler,
                                     CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<BrowserHandler> browserHandler) {
  CefPostTask(TID_UI,
              base::BindOnce(&BrowserProcessHandler::Browser_TryCloseRpc,
                             handler, browser, id));
}

void CutCommand::Execute(BrowserProcessHandler* handler,
                                 CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetFocusedFrame()->Cut();
}

void CopyCommand::Execute(BrowserProcessHandler* handler,
                                  CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetFocusedFrame()->Copy();
}

void PasteCommand::Execute(BrowserProcessHandler* handler,
                                   CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetFocusedFrame()->Paste();
}

void DeleteCommand::Execute(BrowserProcessHandler* handler,
                                    CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetFocusedFrame()->Delete();
}

void UndoCommand::Execute(BrowserProcessHandler* handler,
                                    CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetFocusedFrame()->Undo();
}

void RedoCommand::Execute(BrowserProcessHandler* handler,
                                    CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetFocusedFrame()->Redo();
}

void SelectAllCommand::Execute(BrowserProcessHandler* handler,
                                    CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetFocusedFrame()->SelectAll();
}

void GetSourceCommand::Execute(BrowserProcessHandler* handler,
                                    CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<BrowserHandler> browserHandler) {
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  CefRefPtr<GetSourceStringVisitor> visitor =
      new GetSourceStringVisitor(handler, id);
  frame->GetSource(visitor);
}

void GetTextCommand::Execute(BrowserProcessHandler* handler,
                                  CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<BrowserHandler> browserHandler) {
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  CefRefPtr<GetSourceStringVisitor> visitor =
      new GetSourceStringVisitor(handler, id);
  frame->GetText(visitor);
}

void StartDownloadCommand::ReadArguments() {
  Require("url", url);
}

void StartDownloadCommand::Execute(BrowserProcessHandler* handler,
                                  CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<BrowserHandler> browserHandler) {
  browser->GetHost()->StartDownload(url);
}

void DownloadImageCommand::ReadArguments() {
  Require("url", url);
  Require("isFavicon", isFavicon);
  Require("maxImageSize", maxImageSize);
  Require("bypassCache", bypassCache);
}

void DownloadImageCommand::Execute(BrowserProcessHandler* handler,
                                    CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<BrowserHandler> browserHandler) {
  CefRefPtr<DownloadImageCallback> callback =
      new DownloadImageCallback(handler, id, url);
  browser->GetHost()->DownloadImage(
      url, isFavicon, static_cast<uint32_t>(maxImageSize), bypassCache,
      callback);
}
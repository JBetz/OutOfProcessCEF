#include "client_rpc_commands.h"

#include <include/base/cef_bind.h>
#include <include/base/cef_callback.h>
#include <include/wrapper/cef_closure_task.h>

#include "browser_process_handler.h"

void CreateBrowserCommand::ReadArguments() {
  Require("url", url);
  Require("windowOrigin", windowOrigin);
  Require("rectangle", rectangle);
  Optional("html", html);
  Require("parentWindowHandle", parentWindowHandle);
  Require("windowless", windowless);
  Require("hardwareAccelerated", hardwareAccelerated);
  Require("private", private_);
}

void CreateBrowserCommand::Execute(CefRefPtr<BrowserProcessHandler> handler) {
  HWND parentHwnd = reinterpret_cast<HWND>(parentWindowHandle);
  CefPostTask(
      TID_UI,
      base::BindOnce(&BrowserProcessHandler::Client_CreateBrowserRpc, handler,
                     id, url, windowOrigin, rectangle, parentHwnd,
                     windowless, hardwareAccelerated, private_));
}

void CreateHttpRequestCommand::ReadArguments() {
  Require("url", url);
  Require("method", method);
  Optional("headers", headers);
  Optional("body", body);
}

void CreateHttpRequestCommand::Execute(CefRefPtr<BrowserProcessHandler> handler) {
  handler->CreateHttpRequest(id, url, method, headers, body);
}

void ShutdownCommand::Execute(CefRefPtr<BrowserProcessHandler> handler) {
  CefPostTask(TID_UI, base::BindOnce(&BrowserProcessHandler::Client_ShutdownRpc,
                                     handler));
}

void GetPendingCallbacksCommand::Execute(CefRefPtr<BrowserProcessHandler> handler) {
  CefRefPtr<CefListValue> pendingCallbackUuids = CefListValue::Create();
  for (auto const& imap : handler->GetPendingCallbacks()) {
    pendingCallbackUuids->SetString(pendingCallbackUuids->GetSize(), imap.first);
  }
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetList(pendingCallbackUuids);
  handler->SendResponse(id, value);
}

void CancelPendingCallbackCommand::ReadArguments() {
  Require("requestId", requestId);
}


void CancelPendingCallbackCommand::Execute(CefRefPtr<BrowserProcessHandler> handler) {
  handler->CancelPendingCallback(requestId);
}
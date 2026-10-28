#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "rpc_command.h"

class BrowserProcessHandler;

class ClientCommand : public RpcCommand {
 public:
  virtual void Execute(BrowserProcessHandler* handler) = 0;
};

class CreateBrowserCommand : public ClientCommand {
 private:
  CefString url;
  CefPoint windowOrigin;
  CefRect rectangle;
  std::optional<CefString> html;
  uintptr_t parentWindowHandle = 0;
  bool windowless = true;
  bool hardwareAccelerated = true;
  bool private_ = false;

 protected:
  void ReadArguments() override;

 public:
  void Execute(BrowserProcessHandler* handler) override;
};
REGISTER_RPC_COMMAND(ClientCommand, "CreateBrowser", CreateBrowserCommand);

class CreateHttpRequestCommand : public ClientCommand {
 private:
  CefString url;
  CefString method;
  std::optional<CefRefPtr<CefDictionaryValue>> headers;
  std::optional<CefString> body;

 protected:
  void ReadArguments() override;

 public:
  void Execute(BrowserProcessHandler* handler) override;
};
REGISTER_RPC_COMMAND(ClientCommand,
                     "CreateHttpRequest",
                     CreateHttpRequestCommand);

class ShutdownCommand : public ClientCommand {
 protected:
  void ReadArguments() override {}

 public:
  void Execute(BrowserProcessHandler* handler) override;
};
REGISTER_RPC_COMMAND(ClientCommand, "Shutdown", ShutdownCommand);

class GetPendingCallbacksCommand : public ClientCommand {
  protected:
   void ReadArguments() override {}

  public:
   void Execute(BrowserProcessHandler* handler) override;
};
REGISTER_RPC_COMMAND(ClientCommand,
                     "GetPendingCallbacks",
                     GetPendingCallbacksCommand);

class CancelPendingCallbackCommand : public ClientCommand {
 private:
  CefString requestId;

 protected:
  void ReadArguments() override;

 public:
  void Execute(BrowserProcessHandler* handler) override;
};
REGISTER_RPC_COMMAND(ClientCommand,
                     "CancelPendingCallback",
                     CancelPendingCallbackCommand);
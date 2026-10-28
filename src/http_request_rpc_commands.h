#pragma once

#include "rpc_command.h"

class BrowserProcessHandler;

class HttpRequestCommand : public RpcCommand {
  public:
    virtual void Execute(BrowserProcessHandler* handler,
                       CefRefPtr<CefURLRequest> request) = 0;
};

class CancelCommand : public HttpRequestCommand {
 public:
  void ReadArguments() override {}
  void Execute(BrowserProcessHandler* handler,
               CefRefPtr<CefURLRequest> request) override;
};
REGISTER_RPC_COMMAND(HttpRequestCommand, "Cancel", CancelCommand);

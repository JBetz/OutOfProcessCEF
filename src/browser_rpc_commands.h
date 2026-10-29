#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "rpc_command.h"
#include "browser_handler.h"

class BrowserProcessHandler;
class BrowserHandler;

class BrowserCommand : public RpcCommand {
 public:
  virtual void Execute(CefRefPtr<BrowserProcessHandler> handler,
                       CefRefPtr<CefBrowser> browser,
                       CefRefPtr<BrowserHandler> browserHandler) = 0;
};

class LoadUrlCommand : public BrowserCommand {
 public:
  CefString url;

  void ReadArguments() override;
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "LoadUrl", LoadUrlCommand);

class LoadRequestCommand : public BrowserCommand {
  CefString url;
  CefString method;
  std::map<CefString, CefString> headers;
  std::vector<CefRefPtr<CefPostDataElement>> postData;

  void ReadArguments() override;
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "LoadRequest", LoadRequestCommand);
 

class ReloadCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "Reload", ReloadCommand);

class EvaluateJavaScriptCommand : public BrowserCommand {
 public:
  CefString code;
  std::optional<CefString> scriptUrl;
  std::optional<int> startLine;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand,
                     "EvaluateJavaScript",
                     EvaluateJavaScriptCommand);

class OnMouseClickCommand : public BrowserCommand {
 public:
  CefMouseEvent event;
  int button = 0;
  bool mouseUp = false;
  int clickCount = 1;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "OnMouseClick", OnMouseClickCommand);

class OnMouseMoveCommand : public BrowserCommand {
 public:
  CefMouseEvent event;
  bool mouseLeave = false;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "OnMouseMove", OnMouseMoveCommand);

class OnMouseWheelCommand : public BrowserCommand {
 public:
  CefMouseEvent event;
  int deltaX = 0;
  int deltaY = 0;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "OnMouseWheel", OnMouseWheelCommand);

class OnKeyboardEventCommand : public BrowserCommand {
 public:
  int type = 0;
  uint32_t modifiers = 0;
  int windowsKeyCode = 0;
  int nativeKeyCode = 0;
  bool isSystemKey = false;
  std::optional<CefString> character;
  std::optional<CefString> unmodifiedCharacter;
  bool focusOnEditableField = false;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "OnKeyboardEvent", OnKeyboardEventCommand);

class FocusCommand : public BrowserCommand {
 public:
  bool focus = true;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "Focus", FocusCommand);

class WasHiddenCommand : public BrowserCommand {
 public:
  bool hidden = false;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "WasHidden", WasHiddenCommand);

class WasResizedCommand : public BrowserCommand {
 public:
  CefRect rectangle;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "WasResized", WasResizedCommand);

class SetWindowOriginCommand : public BrowserCommand {
 public:
  CefPoint origin;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "SetWindowOrigin", SetWindowOriginCommand);

class GetFrameRateCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}
 
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "GetFrameRate", GetFrameRateCommand);

class SetFrameRateCommand : public BrowserCommand {
 public:
  int frameRate = 30;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "SetFrameRate", SetFrameRateCommand);

class CloseCommand : public BrowserCommand {
 public:
  bool forceClose = false;

 protected:
  void ReadArguments() override;

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "Close", CloseCommand);


class TryCloseCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}

 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "TryClose", TryCloseCommand);

class CutCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "Cut", CutCommand);

class CopyCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "Copy", CopyCommand);

class PasteCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "Paste", PasteCommand);

class DeleteCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "Delete", DeleteCommand);

class UndoCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "Undo", UndoCommand);

class RedoCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "Redo", RedoCommand);

class SelectAllCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "SelectAll", SelectAllCommand);

class GetSourceCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "GetSource", GetSourceCommand);

class GetTextCommand : public BrowserCommand {
 protected:
  void ReadArguments() override {}
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "GetText", GetTextCommand);

class StartDownloadCommand : public BrowserCommand {
 public:
  CefString url;
 protected:
  void ReadArguments() override;
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "StartDownload", StartDownloadCommand);

class DownloadImageCommand : public BrowserCommand {
 public:
  CefString url;
  bool isFavicon = false;
  int maxImageSize = 0;
  bool bypassCache = false;
 protected:
  void ReadArguments() override;
 public:
  void Execute(CefRefPtr<BrowserProcessHandler> handler,
               CefRefPtr<CefBrowser> browser,
               CefRefPtr<BrowserHandler> browserHandler) override;
};
REGISTER_RPC_COMMAND(BrowserCommand, "DownloadImage", DownloadImageCommand);
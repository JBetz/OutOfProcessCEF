// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include <optional>

#include "render_process_handler.h"

#include <SDL3/sdl.h>
#include <rpc.h>
#include <map>
#include <string>
#include "include/base/cef_logging.h"
#include "json.hpp"
#include "rpc.hpp"

using json = nlohmann::json;

const char kOnFocusMessage[] = "RenderProcessHandler.OnFocus";
const char kOnFocusOutMessage[] = "RenderProcessHandler.OnFocusOut";
const char kOnMouseOverMessage[] = "RenderProcessHandler.OnMouseOver";
const char kOnHistoryMessage[] = "RenderProcessHandler.OnHistory";
const char kOnNavigationMessage[] = "RenderProcessHandler.OnNavigation";
const char kOnMessageMessage[] = "RenderProcessHandler.OnMessage";
const char kOnEvalMessage[] = "RenderProcessHandler.OnEval";
const char kOnConsoleMessageMessage[] = "RenderProcessHandler.OnConsoleMessage";

RenderProcessHandler::RenderProcessHandler() {}

CefRefPtr<CefRenderProcessHandler>
RenderProcessHandler::GetRenderProcessHandler() {
  return this;
}

void RenderProcessHandler::OnWebKitInitialized() {}

void RenderProcessHandler::OnBrowserCreated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDictionaryValue> extra_info) {}

void RenderProcessHandler::OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) {}

CefRefPtr<CefLoadHandler> RenderProcessHandler::GetLoadHandler() {
  return nullptr;
}

class MouseOverHandler : public CefV8Handler {
 public:
  MouseOverHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
      CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
      CefRefPtr<CefFrame> frame = context->GetFrame();
      CefRefPtr<CefV8Value> event = arguments.front();
      CefV8ValueList stopPropagationArguments;
      event->GetValue("stopPropagation")
          ->ExecuteFunction(event, stopPropagationArguments);
      CefRefPtr<CefV8Value> target = event->GetValue("target");

      RpcRequest request;
      request.id = CreateUuid();
      request.className = "Browser";
      request.methodName = "OnMouseOver";
      request.instanceId = frame->GetBrowser()->GetIdentifier();
      Browser_OnMouseOver mouseOverArguments;
      mouseOverArguments.tagName =
          target->GetValue("tagName")->GetStringValue().ToString();
      if (mouseOverArguments.tagName == "INPUT") {
        mouseOverArguments.inputType =
            target->GetValue("type")->GetStringValue().ToString();
      }
      CefV8ValueList closestArguments;
      closestArguments.push_back(CefV8Value::CreateString("a"));
      CefRefPtr<CefV8Value> closestResult =
          target->GetValue("closest")->ExecuteFunction(target, closestArguments);
      CefV8ValueList getBoundingClientRectArguments;
      CefRefPtr<CefV8Value> rectangle;
      if (closestResult->IsObject()) {
        std::string href =
            closestResult->GetValue("href")->GetStringValue().ToString();
        if (!href.empty()) {
          mouseOverArguments.href = href;
        }
        rectangle =
            closestResult->GetValue("getBoundingClientRect")
                ->ExecuteFunction(closestResult, getBoundingClientRectArguments);
      } else {
        rectangle = target->GetValue("getBoundingClientRect")
                        ->ExecuteFunction(target, getBoundingClientRectArguments);
      }
      mouseOverArguments.rectangle.x =
          rectangle->GetValue("left")->GetDoubleValue();
      mouseOverArguments.rectangle.y =
          rectangle->GetValue("top")->GetDoubleValue();
      mouseOverArguments.rectangle.width =
          rectangle->GetValue("right")->GetDoubleValue() -
          mouseOverArguments.rectangle.x;
      mouseOverArguments.rectangle.height =
          rectangle->GetValue("bottom")->GetDoubleValue() -
          mouseOverArguments.rectangle.y;

      request.arguments = mouseOverArguments;
      json jsonRequest = request;
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kOnMouseOverMessage);
      message->GetArgumentList()->SetString(0, jsonRequest.dump());
      frame->SendProcessMessage(PID_BROWSER, message);
      return true;
  }

  // Provide the reference counting implementation for this class.
  IMPLEMENT_REFCOUNTING(MouseOverHandler);
};

class MessageHandler : public CefV8Handler {
 public:
  MessageHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefV8Value> window = context->GetGlobal();
    CefRefPtr<CefFrame> frame = context->GetFrame();
    CefRefPtr<CefV8Value> event = arguments.front();

    RpcRequest request;
    request.id = CreateUuid();
    request.className = "Browser";
    request.methodName = "OnMessage";
    request.instanceId = frame->GetBrowser()->GetIdentifier();
    json eventArguments = json ::object();
    eventArguments["data"] = CefV8ValueToJson(event->GetValue("data"));
    json requestArguments = json::object();
    requestArguments["event"] = eventArguments;
    request.arguments = requestArguments;
    json jsonRequest = request;
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnMessageMessage);
    message->GetArgumentList()->SetString(0, jsonRequest.dump());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

  // Provide the reference counting implementation for this class.
  IMPLEMENT_REFCOUNTING(MessageHandler);
};

class HistoryHandler : public CefV8Handler {
 public:
  HistoryHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
      CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
      CefRefPtr<CefFrame> frame = context->GetFrame();

      RpcRequest request;
      request.id = CreateUuid();
      request.className = "Browser";
      request.instanceId = frame->GetBrowser()->GetIdentifier();
      std::string action = arguments[0]->GetStringValue().ToString();

      if (action == "pushState") {
        request.methodName = "OnPushState";  
        Browser_OnPushState args;
        args.frameId = frame->GetIdentifier().ToString();
        args.isMainFrame = frame->IsMain();
        args.state = arguments[1]->GetStringValue().ToString();
        args.url = arguments[2]->GetStringValue().ToString();
        args.isActive = arguments[3]->GetBoolValue();
        args.hasBeenActive = arguments[4]->GetBoolValue();
        request.arguments = args;
      } else if (action == "replaceState") {
        request.methodName = "OnReplaceState";
        Browser_OnReplaceState args;
        args.frameId = frame->GetIdentifier().ToString();
        args.isMainFrame = frame->IsMain();
        args.state = arguments[1]->GetStringValue().ToString();
        args.url = arguments[2]->GetStringValue().ToString();
        args.isActive = arguments[3]->GetBoolValue();
        args.hasBeenActive = arguments[4]->GetBoolValue();
        request.arguments = args;
      } else {
        return false;
      }

      json jsonRequest = request;
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kOnHistoryMessage);
      message->GetArgumentList()->SetString(0, jsonRequest.dump());
      frame->SendProcessMessage(PID_BROWSER, message);
      return true;
  }

  IMPLEMENT_REFCOUNTING(HistoryHandler);
};

class NavigationHandler : public CefV8Handler {
 public:
  NavigationHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
      CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
      CefRefPtr<CefFrame> frame = context->GetFrame();

      RpcRequest request;
      request.id = CreateUuid();
      request.className = "Browser";
      request.instanceId = frame->GetBrowser()->GetIdentifier();
      std::string action = arguments[0]->GetStringValue().ToString();

      if (action == "url") {
        request.methodName = "OnNavigateByUrl";
        Browser_OnNavigateByUrl args;
        args.url = arguments[1]->GetStringValue().ToString();
        args.state = arguments[2]->GetStringValue().ToString();
        args.info = arguments[3]->GetStringValue().ToString();
        args.history = arguments[4]->GetStringValue().ToString();
        request.arguments = args;
      } else if (action == "delta") {
        request.methodName = "OnNavigateByDelta";
        Browser_OnNavigateByDelta args;
        args.delta = arguments[1]->GetIntValue();
        args.info = arguments[2]->GetStringValue().ToString();
        request.arguments = args;
      } else if (action == "key") {
        request.methodName = "OnNavigateByKey";
        Browser_OnNavigateByKey args;
        args.key = arguments[1]->GetStringValue().ToString();
        args.info = arguments[2]->GetStringValue().ToString();
        request.arguments = args;
      } else {
        return false;
      }

      json jsonRequest = request;
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kOnNavigationMessage);
      message->GetArgumentList()->SetString(0, jsonRequest.dump());
      frame->SendProcessMessage(PID_BROWSER, message);
      return true;
  }

  IMPLEMENT_REFCOUNTING(NavigationHandler);
};

class FocusOutHandler : public CefV8Handler {
 public:
  FocusOutHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
      CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
      CefRefPtr<CefFrame> frame = context->GetFrame();
      CefRefPtr<CefV8Value> event = arguments.front();
      CefRefPtr<CefV8Value> relatedTarget = event->GetValue("relatedTarget");

      RpcRequest request;
      request.id = CreateUuid();
      request.className = "Browser";
      request.methodName = "OnFocusOut";
      request.instanceId = frame->GetBrowser()->GetIdentifier();
      Browser_FocusOut focusOutArguments;
      if (!relatedTarget->IsNull()) {
        focusOutArguments.tagName =
            relatedTarget->GetValue("tagName")->GetStringValue();
        CefRefPtr<CefV8Value> attributes = relatedTarget->GetValue("attributes");
        std::string inputType = attributes->GetValue("type")->GetStringValue();
        if (!inputType.empty()) {
          focusOutArguments.inputType = inputType;
        }
        focusOutArguments.isEditable =
            attributes->GetValue("isEditable")->GetBoolValue();
      }
      request.arguments = focusOutArguments;
      json jsonRequest = request;
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kOnFocusOutMessage);
      message->GetArgumentList()->SetString(0, jsonRequest.dump());
      frame->SendProcessMessage(PID_BROWSER, message);
      return true;
  }

  // Provide the reference counting implementation for this class.
   IMPLEMENT_REFCOUNTING(FocusOutHandler);
};


class ConsoleHandler : public CefV8Handler {
 public:
  ConsoleHandler() {}

  bool Execute(const CefString& name,
               CefRefPtr<CefV8Value> object,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& retval,
               CefString& exception) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context->GetFrame();
    CefRefPtr<CefV8Value> window = context->GetGlobal();
    CefRefPtr<CefV8Value> jsonObj = window->GetValue("JSON");
    CefRefPtr<CefV8Value> stringifyFn = jsonObj->GetValue("stringify");

    RpcRequest request;
    request.id = CreateUuid();
    request.className = "Browser";
    request.methodName = "OnConsoleMessage";
    request.instanceId = frame->GetBrowser()->GetIdentifier();
    Browser_OnConsoleMessage args;
    args.level = name == "error" ? LOGSEVERITY_ERROR
               : name == "warn" ? LOGSEVERITY_WARNING
               : name == "debug" ? LOGSEVERITY_DEBUG
               : LOGSEVERITY_INFO;
    std::string combined;
    for (size_t i = 0; i < arguments.size(); ++i) {
      if (i > 0)
        combined += " ";
      if (arguments[i]->IsString()) {
        combined += arguments[i]->GetStringValue().ToString();
      } else {
        CefV8ValueList stringifyArgs;
        stringifyArgs.push_back(arguments[i]);
        CefRefPtr<CefV8Value> result =
            stringifyFn->ExecuteFunction(jsonObj, stringifyArgs);
        if (result && result->IsString()) {
          combined += result->GetStringValue().ToString();
        }
      }
    }
    args.message = combined;
    args.source = "";
    args.line = 0;
    request.arguments = args;
    json jsonRequest = request;
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnConsoleMessageMessage);
    message->GetArgumentList()->SetString(0, jsonRequest.dump());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

  IMPLEMENT_REFCOUNTING(ConsoleHandler);
};


void RenderProcessHandler::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                            CefRefPtr<CefFrame> frame,
                                            CefRefPtr<CefV8Context> context) {
  CefRefPtr<CefV8Value> window = context->GetGlobal();

  // mouse over
  CefRefPtr<CefV8Handler> mouseOverHandler = new MouseOverHandler();
  CefV8ValueList mouseOverArguments;
  mouseOverArguments.push_back(CefV8Value::CreateString("mouseover"));
  mouseOverArguments.push_back(
      CefV8Value::CreateFunction("onMouseOver", mouseOverHandler));
  window->GetValue("addEventListener")
      ->ExecuteFunction(window, mouseOverArguments);

  // history and navigation API shims
  CefRefPtr<CefV8Handler> modifyHistoryHandler = new HistoryHandler();
  CefRefPtr<CefV8Handler> navigateHandler = new NavigationHandler();
  CefRefPtr<CefV8Value> onProgrammaticModifyHistory =
      CefV8Value::CreateFunction("modifyHistory", modifyHistoryHandler);
  CefRefPtr<CefV8Value> onProgrammaticNavigation =
      CefV8Value::CreateFunction("navigate", navigateHandler);

  std::string shimScript = R"(
    (function(modifyHistory, navigate) {
      function resolveUrl(urlString) {
        try {
          return new URL(String(urlString ?? ''), document.baseURI || location.href).href;
        } catch (e) {
          return String(urlString || '');
        }
      }

      let _state = history.state;
      Object.defineProperty(history, 'state', { 
        get: () => _state 
      });
      Object.defineProperty(history, '_setState', {
        value: function(state) { 
          _state = state; 
        },
        enumerable: false
      });
      history.pushState = function(state, unused, url) {
        _state = state;
        const isUserGesture = navigator.userActivation ? navigator.userActivation.isActive : false;
        const hasBeenActive = navigator.userActivation ? navigator.userActivation.hasBeenActive : false;
        modifyHistory('pushState', JSON.stringify(state), resolveUrl(url), isUserGesture, hasBeenActive);
      };
      history.replaceState = function(state, unused, url) {
        _state = state;
        const isUserGesture = navigator.userActivation ? navigator.userActivation.isActive : false;
        const hasBeenActive = navigator.userActivation ? navigator.userActivation.hasBeenActive : false;
        modifyHistory('replaceState', JSON.stringify(state), resolveUrl(url), isUserGesture, hasBeenActive);
      };
      history.back = function() { 
        navigate('delta', -1, null);
      };
      history.forward = function() {
        navigate('delta', 1, null); 
      };
      history.go = function(delta) { 
        navigate('delta', delta || 0, null);
      };

      const resolved = { 
        committed: Promise.resolve(),
        finished: Promise.resolve()
      };
      navigation.navigate = function(url, options) {
        var opts = options || {};
        navigate(
            'url',
            String(url || ''),
            JSON.stringify(opts.state === undefined ? null : opts.state),
            JSON.stringify(opts.info === undefined ? null : opts.info),
            opts.history || 'auto');
        return resolved;
      };
      navigation.back = function(options) {
        var opts = options || {};
        navigate('delta', -1, JSON.stringify(opts.info === undefined ? null : opts.info));
        return resolved;
      };
      navigation.forward = function(options) {
        var opts = options || {};
        navigate('delta', 1, JSON.stringify(opts.info === undefined ? null : opts.info));
        return resolved;
      };
      navigation.traverseTo = function(key, options) {
        var opts = options || {};
        navigate('key', String(key || ''), JSON.stringify(opts.info === undefined ? null : opts.info));
        return resolved;
      };
    })
)";

  CefRefPtr<CefV8Value> shimFunction;
  CefRefPtr<CefV8Exception> shimException;
  bool shimSuccess = context->Eval(CefString(shimScript), CefString(""), 0,
                                   shimFunction, shimException);
  if (shimSuccess && shimFunction->IsFunction()) {
    CefV8ValueList shimArgs;
    shimArgs.push_back(onProgrammaticModifyHistory);
    shimArgs.push_back(onProgrammaticNavigation);
    shimFunction->ExecuteFunction(nullptr, shimArgs);
  }

  // focus out
  CefRefPtr<CefV8Value> document = window->GetValue("document");
  CefRefPtr<CefV8Handler> focusOutHandler = new FocusOutHandler();
  CefV8ValueList focusOutArguments;
  focusOutArguments.push_back(CefV8Value::CreateString("focusout"));
  focusOutArguments.push_back(
      CefV8Value::CreateFunction("onFocusOut", focusOutHandler));
  document->GetValue("addEventListener")
      ->ExecuteFunction(document, focusOutArguments);

  // message
  CefRefPtr<CefV8Handler> messageHandler = new MessageHandler();
  CefV8ValueList messageArguments;
  messageArguments.push_back(CefV8Value::CreateString("message"));
  messageArguments.push_back(
      CefV8Value::CreateFunction("onMessage", messageHandler));
  window->GetValue("addEventListener")
      ->ExecuteFunction(window, messageArguments);

  // console
  CefRefPtr<CefV8Handler> consoleHandler = new ConsoleHandler();
  CefRefPtr<CefV8Value> console = window->GetValue("console");
  if (console && console->IsObject()) {
    for (const char* method : {"log", "warn", "error", "info", "debug"}) {
        console->SetValue(method,
                      CefV8Value::CreateFunction(method, consoleHandler),
                      V8_PROPERTY_ATTRIBUTE_NONE);
    }
  } else {
    SDL_Log("No console object :(");
  }
}

void RenderProcessHandler::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             CefRefPtr<CefV8Context> context) {}

void RenderProcessHandler::OnUncaughtException(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefV8Context> context,
    CefRefPtr<CefV8Exception> exception,
    CefRefPtr<CefV8StackTrace> stackTrace) {}

void RenderProcessHandler::OnFocusedNodeChanged(CefRefPtr<CefBrowser> browser,
                                                CefRefPtr<CefFrame> frame,
                                                CefRefPtr<CefDOMNode> node) {
  if (node.get()) {
    RpcRequest request;
    request.id = CreateUuid();
    request.className = "Browser";
    request.methodName = "OnFocusedNodeChanged";
    request.instanceId = browser->GetIdentifier();
    Browser_OnFocusedNodeChanged focusedNodeArguments;
    focusedNodeArguments.tagName = node->GetElementTagName();
    std::string inputType = node->GetElementAttribute("type");
    if (!inputType.empty()) {
      focusedNodeArguments.inputType = inputType;
    }
    focusedNodeArguments.isEditable = node->IsEditable();
    request.arguments = focusedNodeArguments;
    json jsonRequest = request;
    CefRefPtr<CefProcessMessage> responseMessage =
        CefProcessMessage::Create(kOnFocusMessage);
    responseMessage->GetArgumentList()->SetString(0, jsonRequest.dump());
    frame->SendProcessMessage(PID_BROWSER, responseMessage);
  }
}

// Custom handler for our promise "then" callback
class PromiseThenHandler : public CefV8Handler {
 public:
  explicit PromiseThenHandler(CefRefPtr<CefFrame> frame,
                              CefProcessId sourceProcessId,
                              const UUID messageId)
      : frame(frame), sourceProcessId(sourceProcessId), messageId(messageId) {}

  bool Execute(const CefString& name,
               CefRefPtr<CefV8Value> object,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& retval,
               CefString& exception) override {
    if (name != "onPromiseResolved" || arguments.size() == 0) {
      return false;
    }

    CefRefPtr<CefProcessMessage> responseMessage =
        CefProcessMessage::Create(kOnEvalMessage);
    CefRefPtr<CefDictionaryValue> messageArguments =
        CefDictionaryValue::Create();
    RpcResponse response;
    response.requestId = messageId;
    response.success = true;
    response.returnValue = CefV8ValueToJson(arguments[0]);
    json jsonResponse = response;
    responseMessage->GetArgumentList()->SetString(0, jsonResponse.dump());
    frame->SendProcessMessage(sourceProcessId, responseMessage);
    retval = arguments[0];
    return true;
  }

 private:
  CefRefPtr<CefFrame> frame;
  CefProcessId sourceProcessId;
  const UUID messageId;
  IMPLEMENT_REFCOUNTING(PromiseThenHandler);
};

bool RenderProcessHandler::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  DCHECK_EQ(source_process, PID_BROWSER);
  CefRefPtr<CefV8Context> context = frame->GetV8Context();
  context->Enter();
  const CefString& name = message->GetName();
  SDL_Log("RenderProcessHandler received message: %s", name.ToString().c_str());
  bool handled = false;
  if (name == "Eval") {
    const CefString& payload = message->GetArgumentList()->GetString(0);
    RpcRequest request = json::parse(payload.ToString()).get<RpcRequest>();
    Browser_EvalJavaScript arguments =
        request.arguments.get<Browser_EvalJavaScript>();
    CefRefPtr<CefV8Value> retval;
    CefRefPtr<CefV8Exception> exception;
    bool success =
        context->Eval(CefString(arguments.code), CefString(arguments.scriptUrl),
                      arguments.startLine, retval, exception);
    if (success && retval->IsPromise()) {
      CefRefPtr<CefV8Value> thenFunction = retval->GetValue("then");
      CefRefPtr<PromiseThenHandler> handler =
          new PromiseThenHandler(frame, source_process, request.id);
      CefRefPtr<CefV8Value> onResolvedFunc =
          CefV8Value::CreateFunction("onPromiseResolved", handler);
      thenFunction->ExecuteFunction(retval, {onResolvedFunc});
    } else {
      RpcResponse response;
      response.requestId = request.id;
      response.success = success;
      if (success) {
        response.returnValue = CefV8ValueToJson(retval);
      } else {
        EvalJavaScriptError error;
        error.endColumn = exception->GetEndColumn();
        error.endPosition = exception->GetEndPosition();
        error.lineNumber = exception->GetLineNumber();
        error.message = exception->GetMessage().ToString();
        error.scriptResourceName =
            exception->GetScriptResourceName().ToString();
        error.sourceLine = exception->GetSourceLine().ToString();
        error.startColumn = exception->GetStartColumn();
        error.startPosition = exception->GetStartPosition();
        json errorJson = error;
        response.error = errorJson;
      }
      CefRefPtr<CefProcessMessage> responseMessage =
          CefProcessMessage::Create(kOnEvalMessage);
      json jsonResponse = response;
      responseMessage->GetArgumentList()->SetString(0, jsonResponse.dump());
      frame->SendProcessMessage(source_process, responseMessage);
    }
    handled = true;
  }
  context->Exit();
  return handled;
}
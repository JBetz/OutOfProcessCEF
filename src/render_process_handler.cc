// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include <optional>

#include "render_process_handler.h"

#include <SDL3/sdl.h>
#include <map>
#include <string>
#include "include/base/cef_logging.h"
#include "rpc.hpp"
#include <include/cef_parser.h>

const char kOnFocusMessage[] = "RenderProcessHandler.OnFocus";
const char kOnFocusOutMessage[] = "RenderProcessHandler.OnFocusOut";
const char kOnMouseOverMessage[] = "RenderProcessHandler.OnMouseOver";
const char kOnHistoryMessage[] = "RenderProcessHandler.OnHistory";
const char kOnNavigationMessage[] = "RenderProcessHandler.OnNavigation";
const char kOnEvalMessage[] = "RenderProcessHandler.OnEval";

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
    CefRefPtr<CefDictionaryValue> request = CefDictionaryValue::Create();
    request->SetString("id", uuid_to_string(CreateUuid()));
    request->SetString("class", "Browser");
    request->SetString("method", "OnMouseOver");
    request->SetInt("instanceId", frame->GetBrowser()->GetIdentifier());
    CefRefPtr<CefDictionaryValue> requestArguments =
        CefDictionaryValue::Create();
    std::string tagName = target->GetValue("tagName")->GetStringValue();
    requestArguments->SetString("tagName", tagName);
    if (tagName == "INPUT") {
      requestArguments->SetString("inputType",
                                  target->GetValue("type")->GetStringValue());
    }
    CefV8ValueList closestArguments;
    closestArguments.push_back(CefV8Value::CreateString("a"));
    CefRefPtr<CefV8Value> closestResult =
        target->GetValue("closest")->ExecuteFunction(target, closestArguments);
    CefV8ValueList getBoundingClientRectArguments;
    CefRefPtr<CefV8Value> v8Rectangle;
    if (closestResult->IsObject()) {
      std::string href = closestResult->GetValue("href")->GetStringValue();
      if (!href.empty()) {
        request->SetString("href", href);
      }
      v8Rectangle =
          closestResult->GetValue("getBoundingClientRect")
              ->ExecuteFunction(closestResult, getBoundingClientRectArguments);
    } else {
      v8Rectangle = target->GetValue("getBoundingClientRect")
                      ->ExecuteFunction(target, getBoundingClientRectArguments);
    }
    if (v8Rectangle->IsObject()) {
      CefRefPtr<CefDictionaryValue> rectangle = CefDictionaryValue::Create();
      rectangle->SetInt("x", v8Rectangle->GetValue("x")->GetIntValue());
      rectangle->SetInt("y", v8Rectangle->GetValue("y")->GetIntValue());
      rectangle->SetInt("width", v8Rectangle->GetValue("width")->GetIntValue());
      rectangle->SetInt("height", v8Rectangle->GetValue("height")->GetIntValue());
      requestArguments->SetDictionary("rectangle", rectangle);
    }
    request->SetDictionary("arguments", requestArguments);
    CefRefPtr<CefValue> json = CefValue::Create();
    json->SetDictionary(request);
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnMouseOverMessage);
    message->GetArgumentList()->SetValue(0, json);
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

  // Provide the reference counting implementation for this class.
  IMPLEMENT_REFCOUNTING(MouseOverHandler);
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
      CefRefPtr<CefDictionaryValue> request = CefDictionaryValue::Create();
      request->SetString("id", uuid_to_string(CreateUuid()));
      request->SetString("class", "Browser");
      request->SetInt("instanceId", frame->GetBrowser()->GetIdentifier());
      CefRefPtr<CefDictionaryValue> requestArguments = CefDictionaryValue::Create();
      std::string action = arguments[0]->GetStringValue();
      if (action == "pushState") {
        request->SetString("method", "OnPushState");
        requestArguments->SetString("frameId", frame->GetIdentifier());
        requestArguments->SetBool("isMainFrame", frame->IsMain());
        requestArguments->SetString("state", arguments[1]->GetStringValue());
        requestArguments->SetString("url", arguments[2]->GetStringValue());
        requestArguments->SetBool("isActive", arguments[3]->GetBoolValue());
        requestArguments->SetBool("hasBeenActive", arguments[4]->GetBoolValue());
      } else if (action == "replaceState") {
        request->SetString("method", "OnReplaceState");
        requestArguments->SetString("frameId", frame->GetIdentifier());
        requestArguments->SetBool("isMainFrame", frame->IsMain());
        requestArguments->SetString("state", arguments[1]->GetStringValue());
        requestArguments->SetString("url", arguments[2]->GetStringValue());
        requestArguments->SetBool("isActive", arguments[3]->GetBoolValue());
        requestArguments->SetBool("hasBeenActive", arguments[4]->GetBoolValue());
      } else {
        return false;
      }
      request->SetDictionary("arguments", requestArguments);
      CefRefPtr<CefValue> json = CefValue::Create();
      json->SetDictionary(request);
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kOnHistoryMessage);
      message->GetArgumentList()->SetValue(0, json);
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
      CefRefPtr<CefDictionaryValue> request = CefDictionaryValue::Create();
      request->SetString("id", uuid_to_string(CreateUuid()));
      request->SetString("class", "Browser");
      request->SetInt("instanceId", frame->GetBrowser()->GetIdentifier());
      CefRefPtr<CefDictionaryValue> requestArguments = CefDictionaryValue::Create();
      std::string action = arguments[0]->GetStringValue();
      if (action == "url") {
        request->SetString("method", "OnNavigateByUrl");
        requestArguments->SetString("url", arguments[1]->GetStringValue());
        requestArguments->SetString("state", arguments[2]->GetStringValue());
        requestArguments->SetString("info", arguments[3]->GetStringValue());
        requestArguments->SetString("history", arguments[4]->GetStringValue());
      } else if (action == "delta") {
        request->SetString("method", "OnNavigateByDelta");
        requestArguments->SetInt("delta", arguments[1]->GetIntValue());
        requestArguments->SetString("info", arguments[2]->GetStringValue());
      } else if (action == "key") {
        request->SetString("method", "OnNavigateByKey");
        requestArguments->SetString("key", arguments[1]->GetStringValue());
        requestArguments->SetString("info", arguments[2]->GetStringValue());
      } else {
        return false;
      }
      request->SetDictionary("arguments", requestArguments);
      CefRefPtr<CefValue> json = CefValue::Create();
      json->SetDictionary(request);
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kOnNavigationMessage);
      message->GetArgumentList()->SetValue(0, json);
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
      CefRefPtr<CefDictionaryValue> request = CefDictionaryValue::Create();
      request->SetString("id", uuid_to_string(CreateUuid()));
      request->SetString("class", "Browser");
      request->SetString("method", "OnMouseOver");
      request->SetInt("instanceId", frame->GetBrowser()->GetIdentifier());
      CefRefPtr<CefDictionaryValue> requestArguments =
          CefDictionaryValue::Create();
      if (!relatedTarget->IsNull()) {
        requestArguments->SetString("tagName",
            relatedTarget->GetValue("tagName")->GetStringValue());
        CefRefPtr<CefV8Value> attributes = relatedTarget->GetValue("attributes");
        std::string inputType = attributes->GetValue("type")->GetStringValue();
        if (!inputType.empty()) {
          requestArguments->SetString("inputType", inputType);
        }
        requestArguments->SetBool("isEditable",
            attributes->GetValue("isEditable")->GetBoolValue());
      }
      request->SetDictionary("arguments", requestArguments);
      CefRefPtr<CefValue> json = CefValue::Create();
      json->SetDictionary(request);
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kOnFocusOutMessage);
      message->GetArgumentList()->SetValue(0, json);
      frame->SendProcessMessage(PID_BROWSER, message);
      return true;
  }

  // Provide the reference counting implementation for this class.
   IMPLEMENT_REFCOUNTING(FocusOutHandler);
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
      const userActivation = navigator.userActivation || {};
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
        const isUserGesture = userActivation ? userActivation.isActive : false;
        const hasBeenActive = userActivation ? userActivation.hasBeenActive : false;
        modifyHistory('pushState', JSON.stringify(state), resolveUrl(url), isUserGesture, hasBeenActive);
      };
      history.replaceState = function(state, unused, url) {
        _state = state;
        const isUserGesture = userActivation ? userActivation.isActive : false;
        const hasBeenActive = userActivation ? userActivation.hasBeenActive : false;
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
    CefRefPtr<CefDictionaryValue> request = CefDictionaryValue::Create();
    request->SetString("id", uuid_to_string(CreateUuid()));
    request->SetString("class", "Browser");
    request->SetString("method", "OnFocusedNodeChanged");
    request->SetInt("instanceId", browser->GetIdentifier());
    CefRefPtr<CefDictionaryValue> arguments = CefDictionaryValue::Create();
    arguments->SetString("tagName", node->GetElementTagName());
    std::string inputType = node->GetElementAttribute("type");
    if (!inputType.empty()) {
      arguments->SetString("inputType", inputType);
    }
    arguments->SetBool("isEditable", node->IsEditable());
    request->SetDictionary("arguments", arguments);
    CefRefPtr<CefValue> json = CefValue::Create();
    json->SetDictionary(request);
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnNavigationMessage);
    message->GetArgumentList()->SetValue(0, json);
    frame->SendProcessMessage(PID_BROWSER, message);
  }
}

// Custom handler for our promise "then" callback
class PromiseThenHandler : public CefV8Handler {
 public:
  explicit PromiseThenHandler(CefRefPtr<CefFrame> frame,
                              CefProcessId sourceProcessId,
                              const CefString& requestId)
      : frame(frame), sourceProcessId(sourceProcessId), requestId(requestId) {}

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
    CefRefPtr<CefDictionaryValue> response = CefDictionaryValue::Create();
    response->SetString("requestId", requestId);
    response->SetBool("success", true);
    response->SetValue("returnValue", CefV8ValueToCefValue(arguments[0]));
    CefRefPtr<CefValue> json = CefValue::Create();
    json->SetDictionary(response);
    responseMessage->GetArgumentList()->SetValue(0, json);
    frame->SendProcessMessage(sourceProcessId, responseMessage);
    retval = arguments[0];
    return true;
  }

 private:
  CefRefPtr<CefFrame> frame;
  CefProcessId sourceProcessId;
  CefString requestId;
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
  bool handled = false;
  if (name == "Eval") {
    auto argumentList = message->GetArgumentList();
    const CefString& requestId = argumentList->GetString(0);
    const CefString& code = argumentList->GetString(1);
    const CefString& scriptUrl = argumentList->GetString(2);
    const int startLine = argumentList->GetInt(3);
    CefRefPtr<CefV8Value> retval;
    CefRefPtr<CefV8Exception> exception;
    bool success =
        context->Eval(code, scriptUrl, startLine, retval, exception);
    if (success && retval->IsPromise()) {
      CefRefPtr<CefV8Value> thenFunction = retval->GetValue("then");
      CefRefPtr<PromiseThenHandler> handler =
          new PromiseThenHandler(frame, source_process, requestId);
      CefRefPtr<CefV8Value> onResolvedFunc =
          CefV8Value::CreateFunction("onPromiseResolved", handler);
      thenFunction->ExecuteFunction(retval, {onResolvedFunc});
    } else {
      CefRefPtr<CefDictionaryValue> response = CefDictionaryValue::Create();
      response->SetString("requestId", requestId);
      response->SetBool("success", success);
      if (success) {
        response->SetValue("returnValue", CefV8ValueToCefValue(retval));
      } else {
        CefRefPtr<CefDictionaryValue> error = CefDictionaryValue::Create();
        error->SetInt("endColumn", exception->GetEndColumn());
        error->SetInt("endPosition", exception->GetEndPosition());
        error->SetInt("lineNumber", exception->GetLineNumber());
        error->SetString("message", exception->GetMessage());
        error->SetString("scriptResourceName", exception->GetScriptResourceName());
        error->SetString("sourceLine", exception->GetSourceLine());
        error->SetInt("startColumn", exception->GetStartColumn());
        error->SetInt("startPosition", exception->GetStartPosition());
        response->SetDictionary("error", error);
      }
      CefRefPtr<CefProcessMessage> responseMessage =
          CefProcessMessage::Create(kOnEvalMessage);
      CefRefPtr<CefValue> json = CefValue::Create();
      json->SetDictionary(response);
      responseMessage->GetArgumentList()->SetValue(0, json);
      frame->SendProcessMessage(source_process, responseMessage);
    }
    handled = true;
  }
  context->Exit();
  return handled;
}
#include "http_request_rpc_commands.h"

#include <include/base/cef_bind.h>
#include <include/base/cef_callback.h>
#include <include/wrapper/cef_closure_task.h>

#include "browser_process_handler.h"

void CancelCommand::Execute(CefRefPtr<BrowserProcessHandler> handler,
                            CefRefPtr<CefURLRequest> request) {
  request->Cancel();
}
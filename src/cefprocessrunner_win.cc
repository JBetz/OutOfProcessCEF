// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include <windows.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"

#include "browser_process_handler.h"
#include "command_line_switches.h"
#include "other_process_handler.h"
#include "process_handler.h"
#include "render_process_handler.h"

// When generating projects with CMake the CEF_USE_SANDBOX value will be defined
// automatically if using the required compiler version. Pass -DUSE_SANDBOX=OFF
// to the CMake command-line to disable use of the sandbox.
// Uncomment this line to manually enable sandbox support.
// #define CEF_USE_SANDBOX 1

#if defined(CEF_USE_SANDBOX)
// The cef_sandbox.lib static library may not link successfully with all VS
// versions.
#pragma comment(lib, "cef_sandbox.lib")
#endif

// Entry point function for all processes.
int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPTSTR lpCmdLine,
                      int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  int exit_code;

#if defined(ARCH_CPU_32_BITS)
  // Run the main thread on 32-bit Windows using a fiber with the preferred 4MiB
  // stack size. This function must be called at the top of the executable entry
  // point function (`main()` or `wWinMain()`). It is used in combination with
  // the initial stack size of 0.5MiB configured via the `/STACK:0x80000` linker
  // flag on executable targets. This saves significant memory on threads (like
  // those in the Windows thread pool, and others) whose stack size can only be
  // controlled via the linker flag.
  exit_code = CefRunWinMainWithPreferredStackSize(wWinMain, hInstance,
                                                  lpCmdLine, nCmdShow);
  if (exit_code >= 0) {
    // The fiber has completed so return here.
    return exit_code;
  }
#endif

  void* sandbox_info = nullptr;

#if defined(CEF_USE_SANDBOX)
  // Manage the life span of the sandbox information object. This is necessary
  // for sandbox support on Windows. See cef_sandbox_win.h for complete details.
  CefScopedSandboxInfo scoped_sandbox;
  sandbox_info = scoped_sandbox.sandbox_info();
#endif

  // Provide CEF with command-line arguments.
  CefMainArgs main_args(hInstance);

  // Parse command-line arguments for use in this method.
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromString(::GetCommandLineW());
  std::optional<int> application_process_id;
  std::string application_process_id_value =
      command_line->GetSwitchValue(switches::kApplicationProcessId);
  if (!application_process_id_value.empty()) {
    application_process_id = std::stoi(application_process_id_value);
  }

  // Create a ProcessHandler of the correct type.
  CefRefPtr<CefApp> handler;
  ProcessHandler::ProcessType process_type =
      ProcessHandler::GetProcessType(command_line);

  LOG(INFO) << "Running process type: " << ProcessHandler::ProcessTypeToString(process_type);

  if (process_type == ProcessHandler::BrowserProcess) {
    const std::string& server_port_str =
        command_line->GetSwitchValue(switches::kServerPort);
    const std::string& application_process_id_str =
        command_line->GetSwitchValue(switches::kApplicationProcessId);
    const CefString& server_ready_event_name_str =
        command_line->GetSwitchValue(switches::kServerReadyEventName);

    if (server_port_str.empty()) {
      LOG(ERROR) << "--server-port is required ";
      return 1;
    }
    if (application_process_id_str.empty()) {
      LOG(ERROR)  << "--application-process-id is required";
      return 1;
    }
    

    int serverPort = std::stoi(server_port_str);
    int applicationProcessId = std::stoi(application_process_id_str);
    HANDLE applicationProcessHandle =
        OpenProcess(PROCESS_DUP_HANDLE, FALSE, applicationProcessId);
    if (applicationProcessHandle == NULL) {
      LOG(ERROR) << "OpenProcess failed for process id " << application_process_id_str;
      return 1;
    }
    std::optional<CefString> serverReadyEventName;
    if (server_ready_event_name_str.empty()) {
      serverReadyEventName = std::nullopt;
    } else {
      serverReadyEventName = server_ready_event_name_str;
    }
    handler = new BrowserProcessHandler(serverPort,
                                        applicationProcessHandle,
                                        serverReadyEventName);
  } else if (process_type == ProcessHandler::RendererProcess) {
    handler = new RenderProcessHandler();
  } else if (process_type == ProcessHandler::OtherProcess) {
    handler = new OtherProcessHandler();
  }

  // CEF Handlerlications have multiple sub-processes (render, GPU, etc) that
  // share the same executable. This function checks the command-line and, if
  // this is a sub-process, executes the Handlerropriate logic.
  exit_code = CefExecuteProcess(main_args, handler, sandbox_info);
  if (exit_code >= 0) {
    return exit_code;
  }

  // Specify CEF global settings here.
  CefSettings settings;
  settings.no_sandbox = true;
  settings.log_severity = LOGSEVERITY_DEFAULT;
  settings.windowless_rendering_enabled = true;
  std::filesystem::path cache_path = std::filesystem::current_path() / "cache";
  CefString(&settings.cache_path) = cache_path;
  std::filesystem::path log_file = std::filesystem::current_path() / "cef.log";
  CefString(&settings.log_file) = log_file;

  // Initialize the CEF browser process. The first browser instance will be
  // created in CefBrowserProcessHandler::OnContextInitialized() after CEF has
  // been initialized. May return false if initialization fails or if early exit
  // is desired (for example, due to process singleton relaunch behavior).
  if (!CefInitialize(main_args, settings, handler, sandbox_info)) {
    return 1;
  }

  // Run the CEF message loop. This will block until CefQuitMessageLoop() is
  // called.
  CefRunMessageLoop();

  // Shut down CEF.
  CefShutdown();

  return 0;
}

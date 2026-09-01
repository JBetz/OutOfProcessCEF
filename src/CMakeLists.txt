# Copyright (c) 2014 The Chromium Embedded Framework Authors. All rights
# reserved. Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

#
# Source files.
#

# cefprocessrunner sources.
set(CEFPROCESSRUNNER_SRCS
  browser_handler.cc
  browser_handler.h
  browser_process_handler.cc
  browser_process_handler.h
  # cached_resource_handler.cc  
  # cached_resource_handler.h
  command_line_switches.cc
  command_line_switches.h
  guid_ext.hpp
  http_request_client.cc
  http_request_client.h
  other_process_handler.cc
  other_process_handler.h
  process_handler.cc
  process_handler.h
  render_process_handler.cc
  render_process_handler.h
  # response_body_filter.cc
  # response_body_filter.h
  rpc.hpp
  thread_safe_queue.hpp)
set(CEFPROCESSRUNNER_SRCS_WINDOWS
  cefprocessrunner_win.cc)
APPEND_PLATFORM_SOURCES(CEFPROCESSRUNNER_SRCS)
source_group(cefprocessrunner FILES ${CEFPROCESSRUNNER_SRCS})

set(CEFPROCESSRUNNER_SRCS
  ${CEFPROCESSRUNNER_SRCS}
  )

#
# Shared configuration.
#

# Target executable names.
set(CEF_TARGET "CefProcessRunner")
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

# Determine the target output directory.
SET_CEF_TARGET_OUT_DIR()

#
# Windows configuration.
#

if(OS_WINDOWS)
  # Executable target.
  add_executable(${CEF_TARGET} WIN32 ${CEFPROCESSRUNNER_SRCS})
  add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
  SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})

  target_include_directories(${CEF_TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/SDL3/include
    ${CMAKE_SOURCE_DIR}/third_party/SDL3_net/include
    ${CMAKE_SOURCE_DIR}/third_party/json/include
  )

  target_link_libraries(${CEF_TARGET}
    libcef_lib
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
    ${CMAKE_SOURCE_DIR}/third_party/SDL3/lib/SDL3.lib
    ${CMAKE_SOURCE_DIR}/third_party/SDL3_net/lib/SDL3_net.lib
  )

  # Add the custom manifest files to the executable.
  ADD_WINDOWS_MANIFEST("${CMAKE_CURRENT_SOURCE_DIR}" "${CEF_TARGET}" "exe")

  set(SDL_DLLS
    "third_party/SDL3/lib/SDL3.dll"
    "third_party/SDL3_net/lib/SDL3_net.dll"
   )

  # Copy binary and resource files to the target output directory.
  COPY_FILES("${CEF_TARGET}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
  COPY_FILES("${CEF_TARGET}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
  COPY_FILES("${CEF_TARGET}" "${SDL_DLLS}" "${CMAKE_SOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

  # Copy build output to install directory if specified in local_config.cmake.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/local_config.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/local_config.cmake")
  endif()
  
  if(INSTALL_DIR)
    add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CEF_TARGET_OUT_DIR} ${INSTALL_DIR}
      COMMENT "Installing CEF build artifacts in ${INSTALL_DIR}"
    )
  endif()
endif()

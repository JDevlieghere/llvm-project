if (APPLE AND LLVM_ENABLE_LOCAL_SUBMODULE_VISIBILITY)
  # Work around an issue with the Python headers, which have a modular include
  # inside an extern "C" block.
  remove_module_flags()
endif()

if(NOT LLDB_PYTHON_RELATIVE_PATH)
  message(FATAL_ERROR "LLDB_PYTHON_RELATIVE_PATH is not set.")
endif()
add_definitions(-DLLDB_PYTHON_RELATIVE_LIBDIR="${LLDB_PYTHON_RELATIVE_PATH}")

if(NOT LLDB_PYTHON_EXE_RELATIVE_PATH)
  message(FATAL_ERROR "LLDB_PYTHON_EXE_RELATIVE_PATH is not set.")
endif()
add_definitions(-DLLDB_PYTHON_EXE_RELATIVE_PATH="${LLDB_PYTHON_EXE_RELATIVE_PATH}")


if (LLDB_ENABLE_LIBEDIT)
  list(APPEND LLDB_LIBEDIT_LIBS LibEdit::LibEdit)
endif()

if (APPLE AND LLVM_ENABLE_LOCAL_SUBMODULE_VISIBILITY)
  # Work around an issue with the Python headers, which have a modular include
  # inside an extern "C" block.
  remove_module_flags()
endif()
get_target_property(python_bindings_dir swig_wrapper_python BINARY_DIR)
set(lldb_python_wrapper ${python_bindings_dir}/LLDBWrapPython.cpp)

add_lldb_library(lldbPluginScriptInterpreterPython SHARED
  PythonDataObjects.cpp
  PythonReadline.cpp
  ScriptInterpreterPython.cpp
  Interfaces/OperatingSystemPythonInterface.cpp
  Interfaces/ScriptInterpreterPythonInterfaces.cpp
  Interfaces/ScriptedFramePythonInterface.cpp
  Interfaces/ScriptedFrameProviderPythonInterface.cpp
  Interfaces/ScriptedPlatformPythonInterface.cpp
  Interfaces/ScriptedProcessPythonInterface.cpp
  Interfaces/ScriptedPythonInterface.cpp
  Interfaces/ScriptedStopHookPythonInterface.cpp
  Interfaces/ScriptedBreakpointPythonInterface.cpp
  Interfaces/ScriptedThreadPlanPythonInterface.cpp
  Interfaces/ScriptedThreadPythonInterface.cpp

  ${lldb_python_wrapper}

  LINK_LIBS
    liblldb
    ${Python3_LIBRARIES}
    ${LLDB_LIBEDIT_LIBS}
  )

add_dependencies(lldbPluginScriptInterpreterPython swig_wrapper_python)

if (MSVC)
  set_property(SOURCE ${lldb_python_wrapper} APPEND_STRING PROPERTY COMPILE_FLAGS " /W0")
else()
  set_property(SOURCE ${lldb_python_wrapper} APPEND_STRING PROPERTY COMPILE_FLAGS " -w")
endif()

set_source_files_properties(${lldb_python_wrapper} PROPERTIES GENERATED ON)
if (CLANG_CL)
  set_property(SOURCE ${lldb_python_wrapper} APPEND_STRING
    PROPERTY COMPILE_FLAGS " -Wno-unused-function")
endif()
if (LLVM_COMPILER_IS_GCC_COMPATIBLE AND
    NOT "${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
  set_property(SOURCE ${lldb_python_wrapper} APPEND_STRING
    PROPERTY COMPILE_FLAGS " -Wno-sequence-point -Wno-cast-qual")
endif ()

if(Python3_RPATH)
  set_property(TARGET lldbPluginScriptInterpreterPython APPEND PROPERTY INSTALL_RPATH "${Python3_RPATH}")
  set_property(TARGET lldbPluginScriptInterpreterPython APPEND PROPERTY BUILD_RPATH   "${Python3_RPATH}")
endif()

# lib/pythonX.Y/dist-packages/lldb/_lldb.so is a symlink to lib/liblldb.so,
# which depends on lib/libLLVM*.so (BUILD_SHARED_LIBS) or lib/libLLVM-10git.so
# (LLVM_LINK_LLVM_DYLIB). Add an additional rpath $ORIGIN/../../../../lib so
# that _lldb.so can be loaded from Python.
if((BUILD_SHARED_LIBS OR LLVM_LINK_LLVM_DYLIB) AND UNIX AND NOT APPLE)
  set_property(TARGET lldbPluginScriptInterpreterPython APPEND PROPERTY INSTALL_RPATH "\$ORIGIN/../../../../lib${LLVM_LIBDIR_SUFFIX}")
endif()

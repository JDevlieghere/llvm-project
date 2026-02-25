if (LLDB_ENABLE_DYNAMIC_SCRIPTINTERPRETERS)
  set(plugin_kind SHARED)
  set(link_libs liblldb)
else()
  set(plugin_kind PLUGIN)
  set(link_libs
    lldbCore
    lldbInterpreter
  )
endif()

add_lldb_library(lldbPluginScriptInterpreterLua ${plugin_kind}
  LuaState.cpp
  ScriptInterpreterLua.cpp

  LINK_LIBS
    ${link_libs}
    ${LUA_LIBRARIES}
  )

target_include_directories(lldbPluginScriptInterpreterLua PUBLIC ${LUA_INCLUDE_DIR})

if (LLDB_ENABLE_DYNAMIC_SCRIPTINTERPRETERS)
  add_lua_wrapper(lldbPluginScriptInterpreterLua)
endif()

add_lldb_tool(lldb-rwi ADD_TO_FRAMEWORK
  GDBServer.cpp
  PlatformServer.cpp
  Server.cpp
  lldb-rwi.cpp

  LINK_COMPONENTS
    Support

  LINK_LIBS
    lldbUtility
    lldbHost
)

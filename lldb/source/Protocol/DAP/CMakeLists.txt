add_lldb_library(lldbProtocolDAP
  ProtocolBase.cpp
  ProtocolEvents.cpp
  ProtocolTypes.cpp
  ProtocolRequests.cpp

  LINK_COMPONENTS
    Support

  LINK_LIBS
    lldbHost
    lldbUtility
 )

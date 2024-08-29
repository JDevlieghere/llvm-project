add_lldb_library(lldbPluginStackProviderPython PLUGIN
  StackProviderPython.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbDataFormatters
    lldbHost
    lldbInterpreter
    lldbTarget

  LINK_COMPONENTS
    Support
  )


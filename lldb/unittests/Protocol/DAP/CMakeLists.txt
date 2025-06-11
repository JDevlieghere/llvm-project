add_lldb_unittest(ProtocolDAPTests
  ProtocolTypesTest.cpp

  LINK_COMPONENTS
    Support
  LINK_LIBS
    lldbProtocolDAP
    lldbUtilityHelpers
    LLVMTestingSupport
  )

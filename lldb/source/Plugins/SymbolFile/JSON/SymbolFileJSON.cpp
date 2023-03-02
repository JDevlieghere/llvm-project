//===-- SymbolFileJSON.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolFileJSON.h"

#include "Plugins/ObjectFile/JSON/ObjectFileJSON.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Timer.h"
#include "llvm/Support/MemoryBuffer.h"

#include <memory>
#include <optional>

using namespace llvm;
using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(SymbolFileJSON)

char SymbolFileJSON::ID;

SymbolFileJSON::SymbolFileJSON(lldb::ObjectFileSP objfile_sp)
    : SymbolFileCommon(std::move(objfile_sp)) {}

void SymbolFileJSON::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void SymbolFileJSON::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef SymbolFileJSON::GetPluginDescriptionStatic() {
  return "Reads debug symbols from a textual symbol table.";
}

SymbolFile *SymbolFileJSON::CreateInstance(ObjectFileSP objfile_sp) {
  return new SymbolFileJSON(std::move(objfile_sp));
}

uint32_t SymbolFileJSON::CalculateAbilities() {
  if (!m_objfile_sp || !llvm::isa<ObjectFileJSON>(*m_objfile_sp))
    return 0;

  return GlobalVariables | Functions;
}

uint32_t SymbolFileJSON::ResolveSymbolContext(const Address &so_addr,
                                              SymbolContextItem resolve_scope,
                                              SymbolContext &sc) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (m_objfile_sp->GetSymtab() == nullptr)
    return 0;

  uint32_t resolved_flags = 0;
  if (resolve_scope & eSymbolContextSymbol) {
    sc.symbol = m_objfile_sp->GetSymtab()->FindSymbolContainingFileAddress(
        so_addr.GetFileAddress());
    if (sc.symbol)
      resolved_flags |= eSymbolContextSymbol;
  }
  return resolved_flags;
}

CompUnitSP SymbolFileJSON::ParseCompileUnitAtIndex(uint32_t idx) { return {}; }

void SymbolFileJSON::GetTypes(SymbolContextScope *sc_scope, TypeClass type_mask,
                              lldb_private::TypeList &type_list) {}

void SymbolFileJSON::AddSymbols(Symtab &symtab) {
  auto json_object_file = dyn_cast_or_null<ObjectFileJSON>(m_objfile_sp.get());
  if (!json_object_file)
    return;

  Log *log = GetLog(LLDBLog::Symbols);
  Module &module = *m_objfile_sp->GetModule();
  const SectionList &list = *module.GetSectionList();

  llvm::DenseSet<addr_t> found_symbol_addresses;

  for (ObjectFileJSON::Symbol symbol : json_object_file->GetSymbols()) {
    SectionSP section_sp = list.FindSectionContainingFileAddress(symbol.addr);
    if (!section_sp) {
      LLDB_LOG(log,
               "Ignoring symbol '{0}', whose address ({1:x}) is outside of the "
               "object file. Mismatched symbol file?",
               symbol.name, symbol.addr);
      continue;
    }
    // Keep track of what addresses were already added so far and only add
    // the symbol with the first address.
    if (!found_symbol_addresses.insert(symbol.addr).second)
      continue;
    symtab.AddSymbol(Symbol(
        /*symID*/ 0, Mangled(symbol.name), eSymbolTypeCode,
        /*is_global*/ true, /*is_debug*/ false,
        /*is_trampoline*/ false, /*is_artificial*/ false,
        AddressRange(section_sp, symbol.addr - section_sp->GetFileAddress(), 0),
        false, /*contains_linker_annotations*/ false, /*flags*/ 0));
  }

  symtab.Finalize();
}

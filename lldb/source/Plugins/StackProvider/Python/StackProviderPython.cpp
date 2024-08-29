//===-- StackProvider.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "StackProviderPython.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Target/Target.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(StackProviderPython)

constexpr uint32_t g_buffer_size = 16000;

const char *__lldb_run_python_code = R"(
extern "C" {
  struct PyObject;
  PyObject *PyDict_New();
  PyObject *PyObject_Repr(PyObject *);
  PyObject *PyObject_Str(PyObject *);
  const char* PyUnicode_AsUTF8(PyObject *);
  PyObject *PyRun_String(const char *, int , PyObject *, PyObject *);
  char* strncpy(char * , const char *, uint32_t);
}

uint32_t __lldb_run_python_code(const char *input, char *output, uint32_t length) {
  PyObject *dict = PyDict_New();
  PyObject *res = PyRun_String(input, 258, dict, dict);
  if (!res)
    return 0;

  PyObject *rep = PyObject_Str(res);
  const char *str = PyUnicode_AsUTF8(rep);
  strncpy(output, str,  length);
  return 1;
}
)";

void StackProviderPython::Initialize() {}

void StackProviderPython::Terminate() {}

llvm::Expected<std::vector<StackProviderPython::Traceback>>
StackProviderPython::GetTraceback(ExecutionContext &exe_ctx) {
  llvm::Expected<std::string> traceback = RunPythonCode(
      exe_ctx, "exec(\"import traceback\") or traceback.format_stack()");
  if (!traceback)
    return traceback.takeError();

  static const RegularExpression g_tb_regex(
      llvm::StringRef("File \"([^\"]*)\", line ([0-9]+), in ([^\\]+)"));

  if (llvm::Error e = g_tb_regex.GetError())
    return std::move(e);

  llvm::StringRef lines = *traceback;
  llvm::StringRef line;
  llvm::SmallVector<llvm::StringRef, 4> matches;

  std::vector<Traceback> tb;
  while (!lines.empty()) {
    std::tie(line, lines) = lines.split("', '");
    if (!g_tb_regex.Execute(line, &matches)) {
      llvm::outs() << "Skipping: " << line << '\n';
      continue;
    }

    uint64_t line;
    llvm::to_integer(matches[2], line);
    tb.emplace_back(std::string(matches[1]), line, std::string(matches[3]));
  }

  return tb;
}

llvm::Expected<std::string>
StackProviderPython::RunPythonCode(ExecutionContext &exe_ctx, std::string str) {
  Log *log = GetLog(LLDBLog::Expressions);
  Process &process = exe_ctx.GetProcessRef();

  // Make the utility function if it doesn't already exist.
  if (!m_utility_fn) {
    auto utility_fn_or_error = exe_ctx.GetTargetRef().CreateUtilityFunction(
        __lldb_run_python_code, "__lldb_run_python_code", eLanguageTypeC,
        exe_ctx);

    if (!utility_fn_or_error)
      return utility_fn_or_error.takeError();

    m_utility_fn = std::move(*utility_fn_or_error);
  }
  assert(m_utility_fn);

  // Make the utility function caller if it doesn't already exist.
  if (!m_utility_fn_caller) {

    // Get a C type system.
    auto type_system_or_err =
        exe_ctx.GetTargetRef().GetScratchTypeSystemForLanguage(eLanguageTypeC);
    if (!type_system_or_err)
      return type_system_or_err.takeError();

    auto ts = *type_system_or_err;
    assert(ts);

    // Make some types for our arguments.
    CompilerType char_ptr_type =
        ts->GetBasicTypeFromAST(eBasicTypeChar).GetPointerType();
    CompilerType const_char_ptr_type = char_ptr_type.AddConstModifier();
    CompilerType uint32_t_type =
        ts->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 32);

    // Put together our arguments.
    ValueList arguments;
    Value value;

    value.SetValueType(Value::ValueType::Scalar);
    value.SetCompilerType(const_char_ptr_type);
    arguments.PushValue(value);

    value.SetValueType(Value::ValueType::Scalar);
    value.SetCompilerType(char_ptr_type);
    arguments.PushValue(value);

    value.SetValueType(Value::ValueType::Scalar);
    value.SetCompilerType(uint32_t_type);
    arguments.PushValue(value);

    Status error;
    FunctionCaller *caller = m_utility_fn->MakeFunctionCaller(
        uint32_t_type, arguments, exe_ctx.GetThreadSP(), error);
    if (error.Fail())
      return error.ToError();

    m_utility_fn_caller = caller;
  }
  assert(m_utility_fn_caller);

  // Allocate space for the result if we haven't done so already.
  if (m_utility_fn_buffer_addr == LLDB_INVALID_ADDRESS) {
    Status error;
    lldb::addr_t addr = process.AllocateMemory(
        g_buffer_size, ePermissionsReadable | ePermissionsWritable, error);

    if (error.Fail())
      return error.ToError();

    if (addr == LLDB_INVALID_ADDRESS)
      return llvm::createStringError(
          "failed to allocate memory in the process");

    m_utility_fn_buffer_addr = addr;
  }
  assert(m_utility_fn_buffer_addr != LLDB_INVALID_ADDRESS);

  // The input changes, so always allocate space for it.
  Status error;
  lldb::addr_t input_buffer_addr = process.AllocateMemory(
      (str.size() + 1) * 8, ePermissionsReadable | ePermissionsWritable, error);
  if (!error.Success() || input_buffer_addr == LLDB_INVALID_ADDRESS)
    return error.ToError();

  // Write the input string.
  process.WriteMemory(input_buffer_addr, str.c_str(), str.size(), error);
  if (error.Fail())
    return error.ToError();

  // Fill in the function arguments.
  ValueList arguments = m_utility_fn_caller->GetArgumentValues();
  uint32_t index = 0;
  arguments.GetValueAtIndex(index++)->GetScalar() = input_buffer_addr;
  arguments.GetValueAtIndex(index++)->GetScalar() = m_utility_fn_buffer_addr;
  arguments.GetValueAtIndex(index++)->GetScalar() = g_buffer_size;

  DiagnosticManager diagnostics;
  lldb::addr_t args_addr = LLDB_INVALID_ADDRESS;

  if (!m_utility_fn_caller->WriteFunctionArguments(exe_ctx, args_addr,
                                                   arguments, diagnostics)) {
    diagnostics.Dump(log);
    return llvm::createStringError("failed to write function arguments");
  }

  EvaluateExpressionOptions options;
  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  options.SetStopOthers(true);
  options.SetTimeout(process.GetUtilityExpressionTimeout());
  options.SetTryAllThreads(false);
  options.SetIsForUtilityExpr(true);

  Value results;
  ExpressionResults expr_result = m_utility_fn_caller->ExecuteFunction(
      exe_ctx, &args_addr, options, diagnostics, results);

  if (error.Fail())
    return error.ToError();

  if (expr_result != eExpressionCompleted)
    return llvm::createStringError("unable to run expression");

  // Read the result buffer.
  std::string output;
  process.ReadCStringFromMemory(m_utility_fn_buffer_addr, output, error);
  if (error.Fail())
    return error.ToError();

  return output;
}

SymbolContext StackProviderPython::ToSymbolContext(lldb::TargetSP target_sp,
                                                   lldb::ModuleSP module_sp,
                                                   const Traceback &tb) {
  // Create the symbol.
  const uint32_t id = 0;
  const bool is_artificial = true;
  const bool is_trampoline = false;
  const bool is_debug = false;
  const bool external = false;
  const bool size_is_valid = false;
  const bool contains_linker_annotations = false;
  const uint32_t flags = 0;
  Symbol* symbol =
      new Symbol(id, Mangled(tb.symbol), lldb::eSymbolTypeAny, external, is_debug,
             is_trampoline, is_artificial, AddressRange(SectionSP(), 0, 0),
             size_is_valid, contains_linker_annotations, flags);

  Function* function =
    new Function(nullptr, 0, 0, Mangled(tb.symbol), nullptr, AddressRange(SectionSP(), 0, 0));

  // Create the line entry.
  LineEntry line_entry;
  line_entry.file_sp = std::make_shared<SupportFile>(FileSpec(tb.file));
  line_entry.line = tb.line;

  // Create the symbol context.
  return SymbolContext(target_sp, module_sp, nullptr, function, nullptr,
                       &line_entry, symbol);
}

llvm::Expected<std::vector<SymbolContext>>
StackProviderPython::GetSyntheticFrames(ExecutionContext &exe_ctx) {
  llvm::Expected<std::vector<StackProviderPython::Traceback>> tb =
      GetTraceback(exe_ctx);
  if (!tb)
    return tb.takeError();

  TargetSP target_sp = exe_ctx.GetTargetSP();
  if (!target_sp)
    return llvm::createStringError("need target");

  std::vector<SymbolContext> symbol_ctxs;
  for (const auto &t : *tb)
    symbol_ctxs.emplace_back(ToSymbolContext(target_sp, ModuleSP(), t));
  return symbol_ctxs;
}

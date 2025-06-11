//===-- Request.h ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_HANDLER_HANDLER_H
#define LLDB_TOOLS_LLDB_DAP_HANDLER_HANDLER_H

#include "DAP.h"
#include "DAPError.h"
#include "DAPLog.h"
#include "lldb/Protocol/DAP/ProtocolBase.h"
#include "lldb/Protocol/DAP/ProtocolRequests.h"
#include "lldb/Protocol/DAP/ProtocolTypes.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

template <typename T> struct is_optional : std::false_type {};

template <typename T> struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

namespace lldb_dap {
struct DAP;

/// Base class for request handlers. Do not extend this directly: Extend
/// the RequestHandler template subclass instead.
class BaseRequestHandler {
public:
  BaseRequestHandler(DAP &dap) : dap(dap) {}

  /// BaseRequestHandler are not copyable.
  /// @{
  BaseRequestHandler(const BaseRequestHandler &) = delete;
  BaseRequestHandler &operator=(const BaseRequestHandler &) = delete;
  /// @}

  virtual ~BaseRequestHandler() = default;

  void Run(const lldb_private::protocol::dap::Request &);

  virtual void
  operator()(const lldb_private::protocol::dap::Request &request) const = 0;

  using FeatureSet = llvm::SmallDenseSet<AdapterFeature, 1>;
  virtual FeatureSet GetSupportedFeatures() const { return {}; }

protected:
  /// Helpers used by multiple request handlers.
  /// FIXME: Move these into the DAP class?
  /// @{

  /// Prints a welcome message on the editor if the preprocessor variable
  /// LLDB_DAP_WELCOME_MESSAGE is defined.
  void PrintWelcomeMessage() const;

  // Takes a LaunchRequest object and launches the process, also handling
  // runInTerminal if applicable. It doesn't do any of the additional
  // initialization and bookkeeping stuff that is needed for `request_launch`.
  // This way we can reuse the process launching logic for RestartRequest too.
  llvm::Error LaunchProcess(
      const lldb_private::protocol::dap::LaunchRequestArguments &request) const;

  // Check if the step-granularity is `instruction`.
  bool HasInstructionGranularity(const llvm::json::Object &request) const;

  /// @}

  DAP &dap;
};

/// FIXME: Migrate callers to typed RequestHandler for improved type handling.
class LegacyRequestHandler : public BaseRequestHandler {
  using BaseRequestHandler::BaseRequestHandler;
  virtual void operator()(const llvm::json::Object &request) const = 0;
  void operator()(
      const lldb_private::protocol::dap::Request &request) const override {
    auto req = toJSON(request);
    (*this)(*req.getAsObject());
  }
};

template <typename Args>
llvm::Expected<Args>
parseArgs(const lldb_private::protocol::dap::Request &request) {
  if (!is_optional_v<Args> && !request.arguments)
    return llvm::make_error<DAPError>(
        llvm::formatv("arguments required for command '{0}' "
                      "but none received",
                      request.command)
            .str());

  Args arguments;
  llvm::json::Path::Root root("arguments");
  if (request.arguments && !fromJSON(*request.arguments, arguments, root)) {
    std::string parse_failure;
    llvm::raw_string_ostream OS(parse_failure);
    OS << "invalid arguments for request '" << request.command
       << "': " << llvm::toString(root.getError()) << "\n";
    root.printErrorContext(*request.arguments, OS);
    return llvm::make_error<DAPError>(parse_failure);
  }

  return arguments;
}
template <>
inline llvm::Expected<lldb_private::protocol::dap::EmptyArguments>
parseArgs(const lldb_private::protocol::dap::Request &request) {
  return std::nullopt;
}

/// Base class for handling DAP requests. Handlers should declare their
/// arguments and response body types like:
///
/// class MyRequestHandler : public RequestHandler<Arguments, Response> {
///   ....
/// };
template <typename Args, typename Resp>
class RequestHandler : public BaseRequestHandler {
  using BaseRequestHandler::BaseRequestHandler;

  void operator()(
      const lldb_private::protocol::dap::Request &request) const override {
    lldb_private::protocol::dap::Response response;
    response.request_seq = request.seq;
    response.command = request.command;

    llvm::Expected<Args> arguments = parseArgs<Args>(request);
    if (llvm::Error err = arguments.takeError()) {
      HandleErrorResponse(std::move(err), response);
      dap.Send(response);
      return;
    }

    if constexpr (std::is_same_v<Resp, llvm::Error>) {
      if (llvm::Error err = Run(*arguments)) {
        HandleErrorResponse(std::move(err), response);
      } else {
        response.success = true;
      }
    } else {
      Resp body = Run(*arguments);
      if (llvm::Error err = body.takeError()) {
        HandleErrorResponse(std::move(err), response);
      } else {
        response.success = true;
        response.body = std::move(*body);
      }
    }

    // Mark the request as 'cancelled' if the debugger was interrupted while
    // evaluating this handler.
    if (dap.debugger.InterruptRequested()) {
      dap.debugger.CancelInterruptRequest();
      response.success = false;
      response.message = lldb_private::protocol::dap::eResponseMessageCancelled;
      response.body = std::nullopt;
    }

    dap.Send(response);

    PostRun();
  };

  virtual Resp Run(const Args &) const = 0;

  /// A hook for a request handler to run additional operations after the
  /// request response is sent but before the next request handler.
  ///
  /// *NOTE*: PostRun will be invoked even if the `Run` operation returned an
  /// error.
  virtual void PostRun() const {};

  void
  HandleErrorResponse(llvm::Error err,
                      lldb_private::protocol::dap::Response &response) const {
    response.success = false;
    llvm::handleAllErrors(
        std::move(err),
        [&](const NotStoppedError &err) {
          response.message =
              lldb_private::protocol::dap::eResponseMessageNotStopped;
        },
        [&](const DAPError &err) {
          lldb_private::protocol::dap::ErrorMessage error_message;
          error_message.sendTelemetry = false;
          error_message.format = err.getMessage();
          error_message.showUser = err.getShowUser();
          error_message.id = err.convertToErrorCode().value();
          error_message.url = err.getURL();
          error_message.urlLabel = err.getURLLabel();
          lldb_private::protocol::dap::ErrorResponseBody body;
          body.error = error_message;
          response.body = body;
        },
        [&](const llvm::ErrorInfoBase &err) {
          lldb_private::protocol::dap::ErrorMessage error_message;
          error_message.showUser = true;
          error_message.sendTelemetry = false;
          error_message.format = err.message();
          error_message.id = err.convertToErrorCode().value();
          lldb_private::protocol::dap::ErrorResponseBody body;
          body.error = error_message;
          response.body = body;
        });
  }
};

class AttachRequestHandler
    : public RequestHandler<lldb_private::protocol::dap::AttachRequestArguments,
                            lldb_private::protocol::dap::AttachResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "attach"; }
  llvm::Error Run(const lldb_private::protocol::dap::AttachRequestArguments
                      &args) const override;
  void PostRun() const override;
};

class BreakpointLocationsRequestHandler
    : public RequestHandler<
          lldb_private::protocol::dap::BreakpointLocationsArguments,
          llvm::Expected<
              lldb_private::protocol::dap::BreakpointLocationsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "breakpointLocations"; }
  FeatureSet GetSupportedFeatures() const override {
    return {
        lldb_private::protocol::dap::eAdapterFeatureBreakpointLocationsRequest};
  }
  llvm::Expected<lldb_private::protocol::dap::BreakpointLocationsResponseBody>
  Run(const lldb_private::protocol::dap::BreakpointLocationsArguments &args)
      const override;

  std::vector<std::pair<uint32_t, uint32_t>>
  GetSourceBreakpointLocations(std::string path, uint32_t start_line,
                               uint32_t start_column, uint32_t end_line,
                               uint32_t end_column) const;
  std::vector<std::pair<uint32_t, uint32_t>>
  GetAssemblyBreakpointLocations(int64_t source_reference, uint32_t start_line,
                                 uint32_t end_line) const;
};

class CompletionsRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "completions"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureCompletionsRequest};
  }
  void operator()(const llvm::json::Object &request) const override;
};

class ContinueRequestHandler
    : public RequestHandler<
          lldb_private::protocol::dap::ContinueArguments,
          llvm::Expected<lldb_private::protocol::dap::ContinueResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "continue"; }
  llvm::Expected<lldb_private::protocol::dap::ContinueResponseBody>
  Run(const lldb_private::protocol::dap::ContinueArguments &args)
      const override;
};

class ConfigurationDoneRequestHandler
    : public RequestHandler<
          lldb_private::protocol::dap::ConfigurationDoneArguments,
          lldb_private::protocol::dap::ConfigurationDoneResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "configurationDone"; }
  FeatureSet GetSupportedFeatures() const override {
    return {
        lldb_private::protocol::dap::eAdapterFeatureConfigurationDoneRequest};
  }
  lldb_private::protocol::dap::ConfigurationDoneResponse
  Run(const lldb_private::protocol::dap::ConfigurationDoneArguments &)
      const override;
};

class DisconnectRequestHandler
    : public RequestHandler<
          std::optional<lldb_private::protocol::dap::DisconnectArguments>,
          lldb_private::protocol::dap::DisconnectResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "disconnect"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureTerminateDebuggee};
  }
  llvm::Error
  Run(const std::optional<lldb_private::protocol::dap::DisconnectArguments>
          &args) const override;
};

class EvaluateRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "evaluate"; }
  void operator()(const llvm::json::Object &request) const override;
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureEvaluateForHovers};
  }
};

class ExceptionInfoRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "exceptionInfo"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureExceptionInfoRequest};
  }
  void operator()(const llvm::json::Object &request) const override;
};

class InitializeRequestHandler
    : public RequestHandler<
          lldb_private::protocol::dap::InitializeRequestArguments,
          llvm::Expected<lldb_private::protocol::dap::InitializeResponse>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "initialize"; }
  llvm::Expected<lldb_private::protocol::dap::InitializeResponse>
  Run(const lldb_private::protocol::dap::InitializeRequestArguments &args)
      const override;
};

class LaunchRequestHandler
    : public RequestHandler<lldb_private::protocol::dap::LaunchRequestArguments,
                            lldb_private::protocol::dap::LaunchResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "launch"; }
  llvm::Error Run(const lldb_private::protocol::dap::LaunchRequestArguments
                      &arguments) const override;
  void PostRun() const override;
};

class RestartRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "restart"; }
  void operator()(const llvm::json::Object &request) const override;
};

class NextRequestHandler
    : public RequestHandler<lldb_private::protocol::dap::NextArguments,
                            lldb_private::protocol::dap::NextResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "next"; }
  llvm::Error
  Run(const lldb_private::protocol::dap::NextArguments &args) const override;
};

class StepInRequestHandler
    : public RequestHandler<lldb_private::protocol::dap::StepInArguments,
                            lldb_private::protocol::dap::StepInResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "stepIn"; }
  llvm::Error
  Run(const lldb_private::protocol::dap::StepInArguments &args) const override;
};

class StepInTargetsRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "stepInTargets"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureStepInTargetsRequest};
  }
  void operator()(const llvm::json::Object &request) const override;
};

class StepOutRequestHandler
    : public RequestHandler<lldb_private::protocol::dap::StepOutArguments,
                            lldb_private::protocol::dap::StepOutResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "stepOut"; }
  llvm::Error
  Run(const lldb_private::protocol::dap::StepOutArguments &args) const override;
};

class SetBreakpointsRequestHandler
    : public RequestHandler<
          lldb_private::protocol::dap::SetBreakpointsArguments,
          llvm::Expected<
              lldb_private::protocol::dap::SetBreakpointsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "setBreakpoints"; }
  FeatureSet GetSupportedFeatures() const override {
    return {
        lldb_private::protocol::dap::eAdapterFeatureConditionalBreakpoints,
        lldb_private::protocol::dap::eAdapterFeatureHitConditionalBreakpoints};
  }
  llvm::Expected<lldb_private::protocol::dap::SetBreakpointsResponseBody>
  Run(const lldb_private::protocol::dap::SetBreakpointsArguments &args)
      const override;
};

class SetExceptionBreakpointsRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "setExceptionBreakpoints"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureExceptionOptions};
  }
  void operator()(const llvm::json::Object &request) const override;
};

class SetFunctionBreakpointsRequestHandler
    : public RequestHandler<
          lldb_private::protocol::dap::SetFunctionBreakpointsArguments,
          llvm::Expected<lldb_private::protocol::dap::
                             SetFunctionBreakpointsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "setFunctionBreakpoints"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureFunctionBreakpoints};
  }
  llvm::Expected<
      lldb_private::protocol::dap::SetFunctionBreakpointsResponseBody>
  Run(const lldb_private::protocol::dap::SetFunctionBreakpointsArguments &args)
      const override;
};

class DataBreakpointInfoRequestHandler
    : public RequestHandler<
          lldb_private::protocol::dap::DataBreakpointInfoArguments,
          llvm::Expected<
              lldb_private::protocol::dap::DataBreakpointInfoResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "dataBreakpointInfo"; }
  llvm::Expected<lldb_private::protocol::dap::DataBreakpointInfoResponseBody>
  Run(const lldb_private::protocol::dap::DataBreakpointInfoArguments &args)
      const override;
};

class SetDataBreakpointsRequestHandler
    : public RequestHandler<
          lldb_private::protocol::dap::SetDataBreakpointsArguments,
          llvm::Expected<
              lldb_private::protocol::dap::SetDataBreakpointsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "setDataBreakpoints"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureDataBreakpoints};
  }
  llvm::Expected<lldb_private::protocol::dap::SetDataBreakpointsResponseBody>
  Run(const lldb_private::protocol::dap::SetDataBreakpointsArguments &args)
      const override;
};

class SetInstructionBreakpointsRequestHandler
    : public RequestHandler<
          lldb_private::protocol::dap::SetInstructionBreakpointsArguments,
          llvm::Expected<lldb_private::protocol::dap::
                             SetInstructionBreakpointsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() {
    return "setInstructionBreakpoints";
  }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureInstructionBreakpoints};
  }
  llvm::Expected<
      lldb_private::protocol::dap::SetInstructionBreakpointsResponseBody>
  Run(const lldb_private::protocol::dap::SetInstructionBreakpointsArguments
          &args) const override;
};

class CompileUnitsRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "compileUnits"; }
  void operator()(const llvm::json::Object &request) const override;
};

class ModulesRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "modules"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureModulesRequest};
  }
  void operator()(const llvm::json::Object &request) const override;
};

class PauseRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "pause"; }
  void operator()(const llvm::json::Object &request) const override;
};

class ScopesRequestHandler final
    : public RequestHandler<
          lldb_private::protocol::dap::ScopesArguments,
          llvm::Expected<lldb_private::protocol::dap::ScopesResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "scopes"; }

  llvm::Expected<lldb_private::protocol::dap::ScopesResponseBody>
  Run(const lldb_private::protocol::dap::ScopesArguments &args) const override;
};

class SetVariableRequestHandler final
    : public RequestHandler<
          lldb_private::protocol::dap::SetVariableArguments,
          llvm::Expected<
              lldb_private::protocol::dap::SetVariableResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "setVariable"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureSetVariable};
  }
  llvm::Expected<lldb_private::protocol::dap::SetVariableResponseBody>
  Run(const lldb_private::protocol::dap::SetVariableArguments &args)
      const override;
};

class SourceRequestHandler final
    : public RequestHandler<
          lldb_private::protocol::dap::SourceArguments,
          llvm::Expected<lldb_private::protocol::dap::SourceResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "source"; }
  llvm::Expected<lldb_private::protocol::dap::SourceResponseBody>
  Run(const lldb_private::protocol::dap::SourceArguments &args) const override;
};

class StackTraceRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "stackTrace"; }
  void operator()(const llvm::json::Object &request) const override;
  FeatureSet GetSupportedFeatures() const override {
    return {
        lldb_private::protocol::dap::eAdapterFeatureDelayedStackTraceLoading};
  }
};

class ThreadsRequestHandler
    : public RequestHandler<
          lldb_private::protocol::dap::ThreadsArguments,
          llvm::Expected<lldb_private::protocol::dap::ThreadsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "threads"; }
  llvm::Expected<lldb_private::protocol::dap::ThreadsResponseBody>
  Run(const lldb_private::protocol::dap::ThreadsArguments &) const override;
};

class VariablesRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "variables"; }
  void operator()(const llvm::json::Object &request) const override;
};

class LocationsRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "locations"; }
  void operator()(const llvm::json::Object &request) const override;
};

class DisassembleRequestHandler final
    : public RequestHandler<
          lldb_private::protocol::dap::DisassembleArguments,
          llvm::Expected<
              lldb_private::protocol::dap::DisassembleResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "disassemble"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureDisassembleRequest};
  }
  llvm::Expected<lldb_private::protocol::dap::DisassembleResponseBody>
  Run(const lldb_private::protocol::dap::DisassembleArguments &args)
      const override;
};

class ReadMemoryRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() { return "readMemory"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureReadMemoryRequest};
  }
  void operator()(const llvm::json::Object &request) const override;
};

class CancelRequestHandler
    : public RequestHandler<lldb_private::protocol::dap::CancelArguments,
                            lldb_private::protocol::dap::CancelResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "cancel"; }
  FeatureSet GetSupportedFeatures() const override {
    return {lldb_private::protocol::dap::eAdapterFeatureCancelRequest};
  }
  llvm::Error
  Run(const lldb_private::protocol::dap::CancelArguments &args) const override;
};

/// A request used in testing to get the details on all breakpoints that are
/// currently set in the target. This helps us to test "setBreakpoints" and
/// "setFunctionBreakpoints" requests to verify we have the correct set of
/// breakpoints currently set in LLDB.
class TestGetTargetBreakpointsRequestHandler : public LegacyRequestHandler {
public:
  using LegacyRequestHandler::LegacyRequestHandler;
  static llvm::StringLiteral GetCommand() {
    return "_testGetTargetBreakpoints";
  }
  void operator()(const llvm::json::Object &request) const override;
};

} // namespace lldb_dap

#endif

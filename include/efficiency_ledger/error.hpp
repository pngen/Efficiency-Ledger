#pragma once

#include <cstdint>
#include <string>

namespace el {

// Typed error/status codes. These map to the documented error model.
enum class ErrorCode : std::uint16_t {
  kOk = 0,
  kInvalidInput,
  kDuplicateEntry,
  kStaleAuthority,
  kStaleEvidence,
  kUnknownClassification,
  kAccountNotFound,
  kAttemptNotFound,
  kReconciliationFailure,
  kOverflow,
  kPersistenceCorrupt,
  kProtocolError,
  kResourceExhausted,
  kOutcomeUnknown,
  kCancelled,
  kShuttingDown,
  kNotAuthoritative,
};

// A status carries an error code plus a human/audit-safe message and, when a
// computation is partial, a stable result hint.
class Status {
 public:
  Status() = default;
  explicit Status(ErrorCode code, std::string message = {})
      : code_(code), message_(std::move(message)) {}

  static Status success() { return Status{}; }

  bool ok() const noexcept { return code_ == ErrorCode::kOk; }
  explicit operator bool() const noexcept { return ok(); }

  ErrorCode code() const noexcept { return code_; }
  const std::string& message() const noexcept { return message_; }

  friend bool operator==(const Status& a, const Status& b) noexcept {
    return a.code_ == b.code_ && a.message_ == b.message_;
  }

 private:
  ErrorCode code_ = ErrorCode::kOk;
  std::string message_;
};

inline const char* to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kOk: return "OK";
    case ErrorCode::kInvalidInput: return "INVALID_INPUT";
    case ErrorCode::kDuplicateEntry: return "DUPLICATE_ENTRY";
    case ErrorCode::kStaleAuthority: return "STALE_AUTHORITY";
    case ErrorCode::kStaleEvidence: return "STALE_EVIDENCE";
    case ErrorCode::kUnknownClassification: return "UNKNOWN_CLASSIFICATION";
    case ErrorCode::kAccountNotFound: return "ACCOUNT_NOT_FOUND";
    case ErrorCode::kAttemptNotFound: return "ATTEMPT_NOT_FOUND";
    case ErrorCode::kReconciliationFailure: return "RECONCILIATION_FAILURE";
    case ErrorCode::kOverflow: return "OVERFLOW";
    case ErrorCode::kPersistenceCorrupt: return "PERSISTENCE_CORRUPT";
    case ErrorCode::kProtocolError: return "PROTOCOL_ERROR";
    case ErrorCode::kResourceExhausted: return "RESOURCE_EXHAUSTED";
    case ErrorCode::kOutcomeUnknown: return "OUTCOME_UNKNOWN";
    case ErrorCode::kCancelled: return "CANCELLED";
    case ErrorCode::kShuttingDown: return "SHUTTING_DOWN";
    case ErrorCode::kNotAuthoritative: return "NOT_AUTHORITATIVE";
  }
  return "UNKNOWN";
}

}  // namespace el

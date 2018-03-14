// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_VALIDATION_ERRORS_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_VALIDATION_ERRORS_H_

#include <sstream>
#include <string>

namespace f1dl {
namespace internal {

enum class ValidationError {
  // There is no validation error.
  NONE,
  // An object (struct or array) is not 8-byte aligned.
  MISALIGNED_OBJECT,
  // An object is not contained inside the message data, or it overlaps other
  // objects.
  ILLEGAL_MEMORY_RANGE,
  // A struct header doesn't make sense, for example:
  // - |num_bytes| is smaller than the size of the struct header.
  // - |num_bytes| and |version| don't match.
  // TODO(yzshen): Consider splitting it into two different error codes. Because
  // the former indicates someone is misbehaving badly whereas the latter could
  // be due to an inappropriately-modified .mojom file.
  UNEXPECTED_STRUCT_HEADER,
  // An array header doesn't make sense, for example:
  // - |num_bytes| is smaller than the size of the header plus the size required
  // to store |num_elements| elements.
  // - For fixed-size arrays, |num_elements| is different than the specified
  // size.
  UNEXPECTED_ARRAY_HEADER,
  // An encoded handle is illegal.
  ILLEGAL_HANDLE,
  // A non-nullable handle field is set to invalid handle.
  UNEXPECTED_INVALID_HANDLE,
  // An encoded pointer is illegal.
  ILLEGAL_POINTER,
  // A non-nullable pointer field is set to null.
  UNEXPECTED_NULL_POINTER,
  // |flags| in the message header is invalid. The flags are either
  // inconsistent with one another, inconsistent with other parts of the
  // message, or unexpected for the message receiver.  For example the
  // receiver is expecting a request message but the flags indicate that
  // the message is a response message.
  MESSAGE_HEADER_INVALID_FLAGS,
  // |flags| in the message header indicates that a request ID is required but
  // there isn't one.
  MESSAGE_HEADER_MISSING_REQUEST_ID,
  // The |name| field in a message header contains an unexpected value.
  MESSAGE_HEADER_UNKNOWN_METHOD,
  // Two parallel arrays which are supposed to represent a map have different
  // lengths.
  DIFFERENT_SIZED_ARRAYS_IN_MAP,
  // A non-nullable union is set to null. (Has size 0)
  UNEXPECTED_NULL_UNION,
};

const char* ValidationErrorToString(ValidationError error);

// TODO(vardhan): This can die, along with |ValidationErrorObserverForTesting|.
void ReportValidationError(ValidationError error,
                           std::string* description = nullptr);

// Only used by validation tests and when there is only one thread doing message
// validation.
class ValidationErrorObserverForTesting {
 public:
  ValidationErrorObserverForTesting();
  ~ValidationErrorObserverForTesting();

  ValidationErrorObserverForTesting(const ValidationErrorObserverForTesting&) = delete;
  ValidationErrorObserverForTesting& operator=(const ValidationErrorObserverForTesting&) = delete;

  ValidationError last_error() const { return last_error_; }
  void set_last_error(ValidationError error) { last_error_ = error; }

 private:
  ValidationError last_error_;
};

// This takes in a string pointer, and provides a string stream which you can
// write to. On destruction, it sets the provided string to the contents of the
// string stream.
class ValidationErrorStringStream {
 public:
  explicit ValidationErrorStringStream(std::string* err_msg);
  ~ValidationErrorStringStream();

  ValidationErrorStringStream(const ValidationErrorStringStream&) = delete;
  ValidationErrorStringStream& operator=(const ValidationErrorStringStream&) = delete;

  std::ostringstream& stream() { return stream_; }

 private:
  std::string* err_msg_;
  std::ostringstream stream_;
};

class LogMessageVoidify {
 public:
  void operator&(std::ostream&) {}
};

}  // namespace internal
}  // namespace f1dl

// In a debug build, this will use |ValidationErrorStringStream::stream()| and
// write to the supplied string if it is not null. In a non-debug + optimized
// build it should do nothing, while also discarding away operator<<() calls.
#ifdef NDEBUG

#define FIDL_INTERNAL_DLOG_SERIALIZATION_FAILURE(error, description)

// The compiler will reduce the |true ? (void)0 : ...| expression to |(void)0|,
// thereby discarding the |ValidationErrorStringStream()| while still keeping
// this macro's use semantically valid.
#define FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err_msg) \
  true ? (void)0                                   \
       : ::f1dl::internal::LogMessageVoidify() &              \
             ::f1dl::internal::ValidationErrorStringStream(err_msg).stream()

#else

// In a debug build, logs a serialization warning and aborts.
#define FIDL_INTERNAL_DLOG_SERIALIZATION_FAILURE(error, description) \
  ZX_PANIC("Could not validate serialization: %s (%s)\n", \
    ValidationErrorToString(error), description)

#define FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err_msg) \
  ::f1dl::internal::ValidationErrorStringStream(err_msg).stream()

#endif  // NDEBUG

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_VALIDATION_ERRORS_H_

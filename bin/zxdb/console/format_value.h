// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <functional>
#include <memory>
#include <vector>

#include "garnet/bin/zxdb/console/output_buffer.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Err;
class ExprValue;
class OutputBuffer;
class StructClass;
class SymbolContext;
class SymbolDataProvider;
class SymbolVariableResolver;
class Type;
class Value;
class Variable;

struct FormatValueOptions {
  enum class NumFormat { kDefault, kUnsigned, kSigned, kHex, kChar };

  // Maximum number of elements to print in an array. For strings we'll
  // speculatively fetch this much data since we don't know mow long the string
  // will be in advance. This means that increasing this will make all string
  // printing (even small strings) slower.
  //
  // If we want to support larger sizes, we may want to add a special memory
  // request option where the debug agent fetches until a null terminator is
  // reached.
  uint32_t max_array_size = 256;

  // Format to apply to numeric types.
  NumFormat num_format = NumFormat::kDefault;
};

// Manages formatting of variables and ExprValues (the results of expressions).
// Since formatting is asynchronous this can be tricky. This class manages a
// set of output operations interleaved with synchronously and asynchronously
// formatted values.
//
// When all requested formatting is complete, the callback will be issued with
// the concatenated result.
//
// When all output is done being appended, call Complete() to schedule the
// final callback.
//
// In common usage the helper can actually be owned by the callback to keep it
// alive during processing and automatically delete it when done:
//
//   auto helper = fxl::MakeRefCounted<FormatValue>();
//   helper->Append(...);
//   // IMPORTANT: Do not move helper into this call or the RefPtr can get
//   // cleared before invoking the call!
//   helper->Complete([helper](OutputBuffer out) { UseIt(out); });
class FormatValue : public fxl::RefCountedThreadSafe<FormatValue> {
 public:
  using Callback = std::function<void(OutputBuffer)>;

  // Construct with fxl::MakeRefCounted<FormatValue>().

  void AppendValue(fxl::RefPtr<SymbolDataProvider> data_provider,
                   const ExprValue value, const FormatValueOptions& options);

  // The data provider normally comes from the frame where you want to evaluate
  // the variable in.
  void AppendVariable(const SymbolContext& symbol_context,
                      fxl::RefPtr<SymbolDataProvider> data_provider,
                      const Variable* var, const FormatValueOptions& options);

  // Writes "<name> = <value>" to the buffer.
  void AppendVariableWithName(const SymbolContext& symbol_context,
                              fxl::RefPtr<SymbolDataProvider> data_provider,
                              const Variable* var,
                              const FormatValueOptions& options);

  void Append(std::string str);
  void Append(OutputBuffer out);

  // Call after all data has been appended.
  //
  // This needs to be a sepatate call since not all output is asynchronous, and
  // we don't want to call a callback before everything is complete, or not at
  // all.
  void Complete(Callback callback);

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(FormatValue);
  FRIEND_MAKE_REF_COUNTED(FormatValue);

  // Output is multilevel and each level can be asynchronous (a struct can
  // include another struct which can include an array, etc.).
  //
  // As we build up the formatted output, each composite type
  // (struct/class/array) adds a new node with its contents as children.
  // Asynchronous operations can fill in the buffers of these nodes, and when
  // all output is complete, the tree can be flattened to produce the final
  // result.
  struct OutputNode {
    OutputBuffer buffer;  // Only used when there are no children.

    // Used for sanity checking. This is set when waiting on async resolution
    // on a given node, and clearned when async resolution is complete. It
    // makes sure we don't miss or double-set anything.
    bool pending = false;

    // The children must be heap-allocated because the pointers (in the form of
    // an OutputKey) will be passed around across vector resizes.
    std::vector<std::unique_ptr<OutputNode>> child;
  };

  // Identifies an output buffer to write asynchronously to.
  //
  // This is actually an OutputNode*. The tricky thing is that the pointers
  // are all owned by the FormatValue class. If the class goes out of scope
  // the pointers will be invalidated, but they may still be referenced by
  // in-progress callbacks.
  //
  // To avoid the temptation to write to the OutputBuffer directly from these
  // callbacks, or to forget to check for completion, this type requires a
  // cast. OutputKeyComplete does the cast and checks whether all callbacks are
  // complete. By being on the object itself, it forces asynchronous callbacks
  // to resolve their weak back-pointers first.
  //
  // No code should ever case this other than the functions that manipulate the
  // keys (AppendToOutputKey, AsyncAppend, OutputKeyComplete).
  using OutputKey = intptr_t;

  FormatValue();
  ~FormatValue();

  // Formats the given expression value to the output buffer. The variant that
  // takes an Err will do an error check before printing the value, and will
  // output the appropriate error message instead if there is one. It will
  // modify the error messaage to be appropriate as a replacement for a value.
  // output the appropriate error message instead if there is one.
  void FormatExprValue(fxl::RefPtr<SymbolDataProvider> data_provider,
                       const ExprValue& value,
                       const FormatValueOptions& options, OutputKey output_key);
  void FormatExprValue(fxl::RefPtr<SymbolDataProvider> data_provider,
                       const Err& err, const ExprValue& value,
                       const FormatValueOptions& options, OutputKey output_key);

  // Asynchronously formats the given type.
  //
  // The known_elt_count can be -1 if the array size is not statically known.
  void FormatStructClass(fxl::RefPtr<SymbolDataProvider> data_provider,
                         const StructClass* sc, const ExprValue& value,
                         const FormatValueOptions& options,
                         OutputKey output_key);
  void FormatString(fxl::RefPtr<SymbolDataProvider> data_provider,
                    const ExprValue& value, const Type* array_value_type,
                    int known_elt_count, const FormatValueOptions& options,
                    OutputKey output_key);
  void FormatArray(fxl::RefPtr<SymbolDataProvider> data_provider,
                   const ExprValue& value, const Type* array_value_type,
                   int elt_count, const FormatValueOptions& options,
                   OutputKey output_key);

  // Helper for FormatArray for when the data has been retrieved from the
  // debugged process.
  void FormatArrayData(const Err& err,
                       fxl::RefPtr<SymbolDataProvider> data_provider,
                       uint64_t address, const std::vector<ExprValue>& items,
                       int known_size, const FormatValueOptions& options,
                       OutputKey output_key);

  // Simpler synchronous outputs.
  void FormatBoolean(const ExprValue& value, OutputBuffer* out);
  void FormatFloat(const ExprValue& value, OutputBuffer* out);
  void FormatSignedInt(const ExprValue& value, OutputBuffer* out);
  void FormatUnsignedInt(const ExprValue& value,
                         const FormatValueOptions& options,
                         OutputBuffer* out);
  void FormatChar(const ExprValue& value, OutputBuffer* out);
  void FormatPointer(const ExprValue& value, OutputBuffer* out);
  void FormatReference(fxl::RefPtr<SymbolDataProvider> data_provider,
                       const ExprValue& value,
                       const FormatValueOptions& options,
                       OutputKey output_key);

  OutputKey GetRootOutputKey();

  // Appends a child node to the output key without opening an async
  // transaction.
  void AppendToOutputKey(OutputKey output_key, OutputBuffer buffer);

  // An asynchronous version of AppendToOutputKey. The returned key is a
  // sub-key for use in later appending. Call OutputKeyComplete when this is
  // done.
  OutputKey AsyncAppend(OutputKey parent);

  // Marks the given output key complete. The variant that takes an output
  // buffer is a shorthand for appending the contents and marking it complete.
  // This will check for completion and issue the callback if everything has
  // been resolved.
  void OutputKeyComplete(OutputKey key);
  void OutputKeyComplete(OutputKey key, OutputBuffer contents);

  // Issues the pending callback if necessary. The callback may delete |this|
  // so the caller should immediately return after calling.
  void CheckPendingResolution();

  // Recursively walks the OutputNode tree to produce the final output in
  // the given output buffer. The sources are moved from so this is
  // destructive.
  void RecursiveCollectOutput(const OutputNode* node, OutputBuffer* out);

  Callback complete_callback_;
  std::vector<OutputBuffer> buffers_;

  std::vector<std::unique_ptr<SymbolVariableResolver>> resolvers_;

  // The root of the output.
  OutputNode root_;

  int pending_resolution_ = 0;

  fxl::WeakPtrFactory<FormatValue> weak_factory_;
};

}  // namespace zxdb

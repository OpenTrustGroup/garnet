// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/type.h"

#include "garnet/bin/zxdb/symbols/symbol_utils.h"

namespace zxdb {

// Provide storage for the constants.
const int Symbol::kTagNone;
const int Symbol::kTagArrayType;
const int Symbol::kTagClassType;
const int Symbol::kTagEntryPoint;
const int Symbol::kTagEnumerationType;
const int Symbol::kTagFormalParameter;
const int Symbol::kTagImportedDeclaration;
const int Symbol::kTagLabel;
const int Symbol::kTagLexicalBlock;
const int Symbol::kTagMember;
const int Symbol::kTagPointerType;
const int Symbol::kTagReferenceType;
const int Symbol::kTagCompileUnit;
const int Symbol::kTagStringType;
const int Symbol::kTagStructureType;
const int Symbol::kTagSubroutineType;
const int Symbol::kTagTypedef;
const int Symbol::kTagUnionType;
const int Symbol::kTagUnspecifiedParameters;
const int Symbol::kTagVariant;
const int Symbol::kTagCommonBlock;
const int Symbol::kTagCommonInclusion;
const int Symbol::kTagInheritance;
const int Symbol::kTagInlinedSubroutine;
const int Symbol::kTagModule;
const int Symbol::kTagPtrToMemberType;
const int Symbol::kTagSetType;
const int Symbol::kTagSubrangeType;
const int Symbol::kTagWithStmt;
const int Symbol::kTagAccessDeclaration;
const int Symbol::kTagBaseType;
const int Symbol::kTagCatchBlock;
const int Symbol::kTagConstType;
const int Symbol::kTagConstant;
const int Symbol::kTagEnumerator;
const int Symbol::kTagFileType;
const int Symbol::kTagFriend;
const int Symbol::kTagNamelist;
const int Symbol::kTagNamelistItem;
const int Symbol::kTagPackedType;
const int Symbol::kTagSubprogram;
const int Symbol::kTagTemplateTypeParameter;
const int Symbol::kTagTemplateValueParameter;
const int Symbol::kTagThrownType;
const int Symbol::kTagTryBlock;
const int Symbol::kTagVariantPart;
const int Symbol::kTagVariable;
const int Symbol::kTagVolatileType;
const int Symbol::kTagDwarfProcedure;
const int Symbol::kTagRestrictType;
const int Symbol::kTagInterfaceType;
const int Symbol::kTagNamespace;
const int Symbol::kTagImportedModule;
const int Symbol::kTagUnspecifiedType;
const int Symbol::kTagPartialUnit;
const int Symbol::kTagImportedUnit;
const int Symbol::kTagCondition;
const int Symbol::kTagSharedType;
const int Symbol::kTagTypeUnit;
const int Symbol::kTagRvalueReferenceType;
const int Symbol::kTagTemplateAlias;
const int Symbol::kTagLoUser;
const int Symbol::kTagHiUser;

Symbol::Symbol() = default;
Symbol::Symbol(int tag) : tag_(tag) {}
Symbol::~Symbol() = default;

const std::string& Symbol::GetAssignedName() const {
  const static std::string empty;
  return empty;
}

const std::string& Symbol::GetFullName() const {
  if (!computed_full_name_) {
    computed_full_name_ = true;
    full_name_ = ComputeFullName();
  }
  return full_name_;
}

const ArrayType* Symbol::AsArrayType() const { return nullptr; }
const BaseType* Symbol::AsBaseType() const { return nullptr; }
const CodeBlock* Symbol::AsCodeBlock() const { return nullptr; }
const DataMember* Symbol::AsDataMember() const { return nullptr; }
const Function* Symbol::AsFunction() const { return nullptr; }
const ModifiedType* Symbol::AsModifiedType() const { return nullptr; }
const Namespace* Symbol::AsNamespace() const { return nullptr; }
const StructClass* Symbol::AsStructClass() const { return nullptr; }
const Type* Symbol::AsType() const { return nullptr; }
const Value* Symbol::AsValue() const { return nullptr; }
const Variable* Symbol::AsVariable() const { return nullptr; }

std::string Symbol::ComputeFullName() const {
  const std::string& assigned_name = GetAssignedName();
  if (assigned_name.empty()) {
    // When a thing doesn't have a name, don't try to qualify it, since
    // returning "foo::" for the name of something like a lexical block is
    // actively confusing.
    return std::string();
  }

  // This base type class just uses the qualified name for the full name.
  // Derived classes will override this function to apply modifiers.
  return GetSymbolScopePrefix(this) + assigned_name;
}

}  // namespace zxdb

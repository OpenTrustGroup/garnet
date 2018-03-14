#!/bin/bash

set -e -u

# Go into the //garnet/public/lib/fidl directory.
cd $(dirname $0)/../..

# Assume we're using an x86-64 debug build.
OUT_DIR=$PWD/../../out/debug-x64
GEN_DIR=$OUT_DIR/gen

# Rebuild the bindings
../../buildtools/ninja -C $OUT_DIR "../../lib/fidl/compiler/interfaces/fidl_files.fidl^" "../../lib/fidl/compiler/interfaces/fidl_types.fidl^"

# Copy generated Python bindings
cp $GEN_DIR/lib/fidl/compiler/interfaces/fidl*.py $PWD/compiler/legacy_generators/pylib/mojom/generate/generated/

# Copy and modify fidl_types.core.go
# (Todo: the fidl compiler generates "Eventpair". Should we follow it?)
sed -e 's/HandleType_Kind_Eventpair/HandleType_Kind_EventPair/' $GEN_DIR/go/src/lib/fidl/compiler/interfaces/fidl_types/fidl_types.core.go > go/src/fidl/compiler/generated/fidl_types/fidl_types.core.go

# Copy and modify fidl_files.core.go
sed -e 's,lib/fidl/compiler/interfaces/fidl_types,fidl/compiler/generated/fidl_types,' $GEN_DIR/go/src/lib/fidl/compiler/interfaces/fidl_files/fidl_files.core.go > go/src/fidl/compiler/generated/fidl_files/fidl_files.core.go


// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
	"fmt"
	"log"
	"strings"
)

const (
	OutOfLineSuffix = "OutOfLine"
	InLineSuffix    = "InLine"
	RequestSuffix   = "Request"
	ResponseSuffix  = "Response"
	EventSuffix     = "Event"
	HandlesSuffix   = "Handles"
)

// Type represents a syzkaller type including type-options.
type Type string

// Enum represents a set of syzkaller flags
type Enum struct {
	Name    string
	Type    string
	Members []string
}

// Struct represents a syzkaller struct.
type Struct struct {
	Name    string
	Members []StructMember
}

// StructMember represents a member of a syzkaller struct.
type StructMember struct {
	Name string
	Type Type
}

// Union represents a syzkaller union.
type Union struct {
	Name    string
	Members []StructMember
	VarLen  bool
}

// Interface represents a FIDL interface in terms of syzkaller structures.
type Interface struct {
	Name string

	// ServiceNameString is the string service name for this FIDL interface.
	ServiceNameString string

	// Methods is a list of methods for this FIDL interface.
	Methods []Method
}

// Method represents a method of a FIDL interface in terms of syzkaller syscalls.
type Method struct {
	// Ordinal is the ordinal for this method.
	Ordinal types.Ordinal

	// Name is the name of the Method, including the interface name as a prefix.
	Name string

	// Request represents a struct containing the request parameters.
	Request *Struct

	// RequestHandles represents a struct containing the handles in the request parameters.
	RequestHandles *Struct

	// Response represents an optional struct containing the response parameters.
	Response *Struct

	// ResponseHandles represents a struct containing the handles in the response parameters.
	ResponseHandles *Struct

	// Structs contain all the structs generated during depth-first traversal of Request/Response.
	Structs []Struct

	// Unions contain all the unions generated during depth-first traversal of Request/Response.
	Unions []Union
}

// Root is the root of the syzkaller backend IR structure.
type Root struct {
	// Name is the name of the library.
	Name string

	// C header file path to be included in syscall description.
	HeaderPath string

	// Interfaces represent the list of FIDL interfaces represented as a collection of syskaller syscall descriptions.
	Interfaces []Interface

	// Structs correspond to syzkaller structs.
	Structs []Struct

	// Unions correspond to syzkaller unions.
	Unions []Union

	// Enums correspond to syzkaller flags.
	Enums []Enum
}

type StructMap map[types.EncodedCompoundIdentifier]types.Struct
type UnionMap map[types.EncodedCompoundIdentifier]types.Union
type EnumMap map[types.EncodedCompoundIdentifier]types.Enum

type compiler struct {
	// decls contains all top-level declarations for the FIDL source.
	decls types.DeclMap

	// structs contain all top-level struct definitions for the FIDL source.
	structs StructMap

	// unions contain all top-level union definitions for the FIDL source.
	unions UnionMap

	// enums contain all top-level enum definitions for the FIDL source.
	enums EnumMap

	// library is the identifier for the current library.
	library types.LibraryIdentifier
}

var reservedWords = map[string]bool{
	"array":     true,
	"buffer":    true,
	"int8":      true,
	"int16":     true,
	"int32":     true,
	"int64":     true,
	"intptr":    true,
	"ptr":       true,
	"type":      true,
	"len":       true,
	"string":    true,
	"stringnoz": true,
	"const":     true,
	"in":        true,
	"out":       true,
	"flags":     true,
	"bytesize":  true,
	"bitsize":   true,
	"text":      true,
	"void":      true,
}

var primitiveTypes = map[types.PrimitiveSubtype]string{
	types.Bool:    "int8",
	types.Int8:    "int8",
	types.Int16:   "int16",
	types.Int32:   "int32",
	types.Int64:   "int64",
	types.Uint8:   "int8",
	types.Uint16:  "int16",
	types.Uint32:  "int32",
	types.Uint64:  "int64",
	types.Float32: "int32",
	types.Float64: "int64",
}

var handleSubtypes = map[types.HandleSubtype]string{
	types.Handle:    "zx_handle",
	types.Process:   "zx_process",
	types.Thread:    "zx_thread",
	types.Vmo:       "zx_vmo",
	types.Channel:   "zx_chan",
	types.Event:     "zx_event",
	types.Port:      "zx_port",
	types.Interrupt: "zx_interrupt",
	types.Log:       "zx_log",
	types.Socket:    "zx_socket",
	types.Resource:  "zx_resource",
	types.Eventpair: "zx_eventpair",
	types.Job:       "zx_job",
	types.Vmar:      "zx_vmar",
	types.Fifo:      "zx_fifo",
	types.Guest:     "zx_guest",
	types.Time:      "zx_timer",
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

func changeIfReserved(val types.Identifier, ext string) string {
	str := string(val) + ext
	if isReservedWord(str) {
		return str + "_"
	}
	return str
}

func formatLibrary(library types.LibraryIdentifier, sep string) string {
	parts := []string{}
	for _, part := range library {
		parts = append(parts, string(part))
	}
	return changeIfReserved(types.Identifier(strings.Join(parts, sep)), "")
}

func formatLibraryPath(library types.LibraryIdentifier) string {
	return formatLibrary(library, "/")
}

func (c *compiler) compileIdentifier(id types.Identifier, ext string) string {
	str := string(id)
	str = common.ToSnakeCase(str)
	return changeIfReserved(types.Identifier(str), ext)
}

func (c *compiler) compileCompoundIdentifier(eci types.EncodedCompoundIdentifier, ext string) string {
	val := types.ParseCompoundIdentifier(eci)
	strs := []string{}
	strs = append(strs, formatLibrary(val.Library, "_"))
	strs = append(strs, changeIfReserved(val.Name, ext))
	return strings.Join(strs, "_")
}

func (c *compiler) compilePrimitiveSubtype(val types.PrimitiveSubtype) Type {
	t, ok := primitiveTypes[val]
	if !ok {
		log.Fatal("Unknown primitive type: ", val)
	}
	return Type(t)
}

func (c *compiler) compilePrimitiveSubtypeRange(val types.PrimitiveSubtype, valRange string) Type {
	return Type(fmt.Sprintf("%s[%s]", c.compilePrimitiveSubtype(val), valRange))
}

func (c *compiler) compileHandleSubtype(val types.HandleSubtype) Type {
	if t, ok := handleSubtypes[val]; ok {
		return Type(t)
	}
	log.Fatal("Unknown handle type: ", val)
	return Type("")
}

func (c *compiler) compileEnum(val types.Enum) Enum {
	e := Enum{
		c.compileCompoundIdentifier(val.Name, ""),
		string(c.compilePrimitiveSubtype(val.Type)),
		[]string{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, fmt.Sprintf("%s_%s", e.Name, v.Name))
	}
	return e
}

func (c *compiler) compileStructMember(p types.StructMember) (StructMember, *StructMember, *StructMember) {
	var i StructMember
	var o *StructMember
	var h *StructMember

	switch p.Type.Kind {
	case types.PrimitiveType:
		i = StructMember{
			Type: c.compilePrimitiveSubtype(p.Type.PrimitiveSubtype),
			Name: c.compileIdentifier(p.Name, ""),
		}
	case types.HandleType:
		i = StructMember{
			Type: Type("flags[fidl_handle_presence, int32]"),
			Name: c.compileIdentifier(p.Name, ""),
		}

		// Out-of-line handles
		h = &StructMember{
			Type: c.compileHandleSubtype(p.Type.HandleSubtype),
			Name: c.compileIdentifier(p.Name, ""),
		}
	case types.RequestType:
		i = StructMember{
			Type: Type("flags[fidl_handle_presence, int32]"),
			Name: c.compileIdentifier(p.Name, ""),
		}

		// Out-of-line handles
		h = &StructMember{
			Type: Type(fmt.Sprintf("zx_chan_%s_server", c.compileCompoundIdentifier(p.Type.RequestSubtype, ""))),
			Name: c.compileIdentifier(p.Name, ""),
		}
	case types.ArrayType:
		inLine, outOfLine, handle := c.compileStructMember(types.StructMember{
			Name: types.Identifier(c.compileIdentifier(p.Name, OutOfLineSuffix)),
			Type: (*p.Type.ElementType),
		})

		i = StructMember{
			Type: Type(fmt.Sprintf("array[%s, %v]", inLine.Type, *p.Type.ElementCount)),
			Name: c.compileIdentifier(p.Name, InLineSuffix),
		}

		// Variable-size, out-of-line data
		if outOfLine != nil {
			o = &StructMember{
				Type: Type(fmt.Sprintf("array[%s, %v]", outOfLine.Type, *p.Type.ElementCount)),
				Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
			}
		}

		// Out-of-line handles
		if handle != nil {
			h = &StructMember{
				Type: Type(fmt.Sprintf("array[%s, %v]", handle.Type, *p.Type.ElementCount)),
				Name: c.compileIdentifier(p.Name, HandlesSuffix),
			}
		}
	case types.StringType:
		// Constant-size, in-line data
		i = StructMember{
			Type: Type("fidl_string"),
			Name: c.compileIdentifier(p.Name, InLineSuffix),
		}

		// Variable-size, out-of-line data
		o = &StructMember{
			Type: Type("fidl_aligned[stringnoz]"),
			Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
		}
	case types.VectorType:
		// Constant-size, in-line data
		i = StructMember{
			Type: Type("fidl_vector"),
			Name: c.compileIdentifier(p.Name, InLineSuffix),
		}

		// Variable-size, out-of-line data
		inLine, outOfLine, handle := c.compileStructMember(types.StructMember{
			Name: types.Identifier(c.compileIdentifier(p.Name, OutOfLineSuffix)),
			Type: (*p.Type.ElementType),
		})
		o = &StructMember{
			Type: Type(fmt.Sprintf("array[%s]", inLine.Type)),
			Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
		}

		if outOfLine != nil {
			o = &StructMember{
				Type: Type(fmt.Sprintf("parallel_array[%s, %s]", inLine.Type, outOfLine.Type)),
				Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
			}
		}

		// Out-of-line handles
		if handle != nil {
			h = &StructMember{
				Type: Type(fmt.Sprintf("array[%s]", handle.Type)),
				Name: c.compileIdentifier(p.Name, ""),
			}
		}
	case types.IdentifierType:
		declType, ok := c.decls[p.Type.Identifier]
		if !ok {
			log.Fatal("Unknown identifier: ", p.Type.Identifier)
		}

		switch declType {
		case types.EnumDeclType:
			i = StructMember{
				Type: Type(fmt.Sprintf("flags[%s, %s]", c.compileCompoundIdentifier(p.Type.Identifier, ""), c.compilePrimitiveSubtype(c.enums[p.Type.Identifier].Type))),
				Name: c.compileIdentifier(p.Name, ""),
			}
		case types.InterfaceDeclType:
			i = StructMember{
				Type: Type("flags[fidl_handle_presence, int32]"),
				Name: c.compileIdentifier(p.Name, ""),
			}

			// Out-of-line handles
			h = &StructMember{
				Type: Type(fmt.Sprintf("zx_chan_%s_client", c.compileCompoundIdentifier(p.Type.Identifier, ""))),
				Name: c.compileIdentifier(p.Name, ""),
			}
		case types.UnionDeclType:
			_, outOfLine, handles := c.compileUnion(c.unions[p.Type.Identifier])

			// Constant-size, in-line data
			t := c.compileCompoundIdentifier(p.Type.Identifier, InLineSuffix)
			i = StructMember{
				Type: Type(t),
				Name: c.compileIdentifier(p.Name, InLineSuffix),
			}

			// Variable-size, out-of-line data
			if outOfLine != nil {
				t := c.compileCompoundIdentifier(p.Type.Identifier, OutOfLineSuffix)
				o = &StructMember{
					Type: Type(t),
					Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
				}
			}

			// Out-of-line handles
			if handles != nil {
				t := c.compileCompoundIdentifier(p.Type.Identifier, HandlesSuffix)
				h = &StructMember{
					Type: Type(t),
					Name: c.compileIdentifier(p.Name, ""),
				}
			}
		case types.StructDeclType:
			_, outOfLine, handles := c.compileStruct(c.structs[p.Type.Identifier])

			// Constant-size, in-line data
			t := c.compileCompoundIdentifier(p.Type.Identifier, InLineSuffix)
			i = StructMember{
				Type: Type(t),
				Name: c.compileIdentifier(p.Name, InLineSuffix),
			}

			// Variable-size, out-of-line data
			if outOfLine != nil {
				t := c.compileCompoundIdentifier(p.Type.Identifier, OutOfLineSuffix)
				o = &StructMember{
					Type: Type(t),
					Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
				}
			}

			// Out-of-line handles
			if handles != nil {
				t := c.compileCompoundIdentifier(p.Type.Identifier, HandlesSuffix)
				h = &StructMember{
					Type: Type(t),
					Name: c.compileIdentifier(p.Name, ""),
				}
			}
		}
	}

	return i, o, h
}

func (c *compiler) compileMessageHeader(Ordinal types.Ordinal) StructMember {
	return StructMember{
		Type: Type(fmt.Sprintf("fidl_message_header[%d]", Ordinal)),
		Name: "hdr",
	}
}

func (c *compiler) compileStruct(p types.Struct) ([]StructMember, []StructMember, []StructMember) {
	var i, o, h []StructMember

	for _, m := range p.Members {
		inLine, outOfLine, handles := c.compileStructMember(m)

		i = append(i, inLine)

		if outOfLine != nil {
			o = append(o, *outOfLine)
		}

		if handles != nil {
			h = append(h, *handles)
		}
	}

	if len(o) == 0 {
		o = append(o, StructMember{
			Name: "void",
			Type: "void",
		})
	}

	if len(h) == 0 {
		h = append(h, StructMember{
			Name: "void",
			Type: "void",
		})
	}

	return i, o, h
}

func (c *compiler) compileUnion(p types.Union) ([]StructMember, []StructMember, []StructMember) {
	var i, o, h []StructMember

	for _, m := range p.Members {
		inLine, outOfLine, handles := c.compileStructMember(types.StructMember{
			Type:   m.Type,
			Name:   m.Name,
			Offset: m.Offset,
		})

		i = append(i, StructMember{
			Type: Type(fmt.Sprintf("fidl_union_member[%sTag%s, %s]", c.compileCompoundIdentifier(p.Name, ""), m.Name, inLine.Type)),
			Name: inLine.Name,
		})

		if outOfLine != nil {
			o = append(o, *outOfLine)
		}

		if handles != nil {
			h = append(h, *handles)
		}
	}

	return i, o, h
}

func (c *compiler) compileParameters(name string, ordinal types.Ordinal, params []types.Parameter) (Struct, Struct) {
	var args types.Struct
	for _, p := range params {
		args.Members = append(args.Members, types.StructMember{
			Type:   p.Type,
			Name:   p.Name,
			Offset: p.Offset,
		})
	}

	i, o, h := c.compileStruct(args)

	if len(o) == 1 && o[0].Type == "void" {
		o = []StructMember{}
	}

	msg := Struct{
		Name:    name,
		Members: append(append([]StructMember{c.compileMessageHeader(ordinal)}, i...), o...),
	}

	handles := Struct{
		Name:    name + HandlesSuffix,
		Members: h,
	}

	return msg, handles
}

func (c *compiler) compileMethod(ifaceName types.EncodedCompoundIdentifier, val types.Method) Method {
	methodName := c.compileCompoundIdentifier(ifaceName, string(val.Name))
	r := Method{
		Name:    methodName,
		Ordinal: val.Ordinal,
	}

	if val.HasRequest {
		request, requestHandles := c.compileParameters(r.Name+RequestSuffix, r.Ordinal, val.Request)
		r.Request = &request
		r.RequestHandles = &requestHandles
	}

	// For response, we only extract handles for now.
	if val.HasResponse {
		suffix := ResponseSuffix
		if !val.HasRequest {
			suffix = EventSuffix
		}
		response, responseHandles := c.compileParameters(r.Name+suffix, r.Ordinal, val.Response)
		r.Response = &response
		r.ResponseHandles = &responseHandles
	}

	return r
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	r := Interface{
		Name:              c.compileCompoundIdentifier(val.Name, ""),
		ServiceNameString: strings.Trim(val.GetServiceName(), "\""),
	}
	for _, v := range val.Methods {
		r.Methods = append(r.Methods, c.compileMethod(val.Name, v))
	}
	return r
}

func Compile(fidlData types.Root) Root {
	root := Root{}
	libraryName := types.ParseLibraryName(fidlData.Name)
	c := compiler{
		decls:   fidlData.Decls,
		structs: make(StructMap),
		unions:  make(UnionMap),
		enums:   make(EnumMap),
		library: libraryName,
	}

	root.HeaderPath = fmt.Sprintf("%s/c/fidl.h", formatLibraryPath(libraryName))

	for _, v := range fidlData.Enums {
		c.enums[v.Name] = v

		root.Enums = append(root.Enums, c.compileEnum(v))
	}

	for _, v := range fidlData.Structs {
		c.structs[v.Name] = v

		i, o, h := c.compileStruct(v)
		root.Structs = append(root.Structs, Struct{
			Name:    c.compileCompoundIdentifier(v.Name, InLineSuffix),
			Members: i,
		})

		root.Structs = append(root.Structs, Struct{
			Name:    c.compileCompoundIdentifier(v.Name, OutOfLineSuffix),
			Members: o,
		})

		root.Structs = append(root.Structs, Struct{
			Name:    c.compileCompoundIdentifier(v.Name, HandlesSuffix),
			Members: h,
		})
	}

	for _, v := range fidlData.Unions {
		c.unions[v.Name] = v

		i, o, h := c.compileUnion(v)
		root.Unions = append(root.Unions, Union{
			Name:    c.compileCompoundIdentifier(v.Name, InLineSuffix),
			Members: i,
		})

		if len(o) == 0 {
			o = append(o, StructMember{
				Name: "void",
				Type: "void",
			})
		}

		if len(h) == 0 {
			h = append(h, StructMember{
				Name: "void",
				Type: "void",
			})
		}

		root.Unions = append(root.Unions, Union{
			Name:    c.compileCompoundIdentifier(v.Name, OutOfLineSuffix),
			Members: o,
			VarLen:  true,
		})

		root.Unions = append(root.Unions, Union{
			Name:    c.compileCompoundIdentifier(v.Name, HandlesSuffix),
			Members: h,
			VarLen:  true,
		})
	}

	for _, v := range fidlData.Interfaces {
		root.Interfaces = append(root.Interfaces, c.compileInterface(v))
	}

	exists := make(map[string]bool)
	for _, i := range root.Interfaces {
		for _, m := range i.Methods {
			for _, s := range m.Structs {
				if _, ok := exists[s.Name]; !ok {
					root.Structs = append(root.Structs, s)
					exists[s.Name] = true
				}
			}
			for _, s := range m.Unions {
				if _, ok := exists[s.Name]; !ok {
					root.Unions = append(root.Unions, s)
					exists[s.Name] = true
				}
			}
		}
	}

	return root
}

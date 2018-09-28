// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"runtime/trace"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/cmd/pm/archive"
	buildcmd "fuchsia.googlesource.com/pm/cmd/pm/build"
	"fuchsia.googlesource.com/pm/cmd/pm/delta"
	"fuchsia.googlesource.com/pm/cmd/pm/expand"
	"fuchsia.googlesource.com/pm/cmd/pm/genkey"
	initcmd "fuchsia.googlesource.com/pm/cmd/pm/init"
	"fuchsia.googlesource.com/pm/cmd/pm/install"
	"fuchsia.googlesource.com/pm/cmd/pm/publish"
	"fuchsia.googlesource.com/pm/cmd/pm/seal"
	"fuchsia.googlesource.com/pm/cmd/pm/serve"
	"fuchsia.googlesource.com/pm/cmd/pm/sign"
	"fuchsia.googlesource.com/pm/cmd/pm/snapshot"
	"fuchsia.googlesource.com/pm/cmd/pm/update"
	"fuchsia.googlesource.com/pm/cmd/pm/verify"
)

const usage = `Usage: %s [-k key] [-m manifest] [-o output dir] [-t tempdir] <command>
Commands
    init     - initialize a package meta directory in the standard form
    genkey   - generate a new private key

    build    - perform update, sign and seal in order
    update   - update the merkle roots in meta/contents
    sign     - sign a package with the given key
    seal     - seal package metadata into a signed meta.far
    verify   - verify metadata signature against the embedded public key
    archive  - construct a single .far representation of the package
    expand   - expand a single .far representation of a package into a repository
    publish  - publish the package to a local TUF directory
    serve    - serve a TUF directory of packages
    snapshot - capture metadata from multiple packages in a single file
    delta    - compare two snapshot files

Dev Only:
    install  - install a single .far representation of the package
`

var tracePath = flag.String("trace", "", "write runtime trace to `file`")

func doMain() int {
	cfg := build.NewConfig()
	cfg.InitFlags(flag.CommandLine)

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		flag.PrintDefaults()
	}

	flag.Parse()

	if *tracePath != "" {
		tracef, err := os.Create(*tracePath)
		if err != nil {
			log.Fatal(err)
		}
		defer func() {
			if err := tracef.Sync(); err != nil {
				log.Fatal(err)
			}
			if err := tracef.Close(); err != nil {
				log.Fatal(err)
			}
		}()
		if err := trace.Start(tracef); err != nil {
			log.Fatal(err)
		}
		defer trace.Stop()
	}

	var err error
	switch flag.Arg(0) {
	case "archive":
		err = archive.Run(cfg, flag.Args()[1:])

	case "build":
		err = buildcmd.Run(cfg, flag.Args()[1:])

	case "delta":
		err = delta.Run(cfg, flag.Args()[1:])

	case "expand":
		err = expand.Run(cfg, flag.Args()[1:])

	case "genkey":
		err = genkey.Run(cfg, flag.Args()[1:])

	case "init":
		err = initcmd.Run(cfg, flag.Args()[1:])

	case "install":
		err = install.Run(cfg, flag.Args()[1:])

	case "publish":
		err = publish.Run(cfg, flag.Args()[1:])

	case "seal":
		err = seal.Run(cfg, flag.Args()[1:])

	case "sign":
		err = sign.Run(cfg, flag.Args()[1:])

	case "serve":
		err = serve.Run(cfg, flag.Args()[1:])

	case "snapshot":
		err = snapshot.Run(cfg, flag.Args()[1:])

	case "update":
		err = update.Run(cfg, flag.Args()[1:])

	case "verify":
		err = verify.Run(cfg, flag.Args()[1:])

	default:
		flag.Usage()
		return 1
	}

	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err)
		return 1
	}

	return 0
}

func main() {
	// we want to use defer in main, but os.Exit doesn't run defers, so...
	os.Exit(doMain())
}

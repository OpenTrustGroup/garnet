// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"

	"app/context"
	"syslog/logger"
	"system_update_package"
)

func main() {
	ctx := context.CreateFromStartupInfo()
	logger.InitDefaultLoggerWithTags(ctx.Connector(), "system_updater")

	dataPath := filepath.Join("/pkgfs", "packages", "update", "0")

	pFile, err := os.Open(filepath.Join(dataPath, "packages"))
	if err != nil {
		logger.Fatalf("error opening packages data file! %s", err)
	}
	defer pFile.Close()

	iFile, err := os.Open(filepath.Join("/pkg", "data", "images"))
	if err != nil {
		logger.Fatalf("error opening images data file! %s", err)
		return
	}
	defer iFile.Close()

	pkgs, imgs, err := system_update_package.ParseRequirements(pFile, iFile)
	if err != nil {
		logger.Fatalf("could not parse requirements: %s", err)
	}

	amber, err := system_update_package.ConnectToUpdateSrvc()
	if err != nil {
		logger.Fatalf("unable to connect to update service: %s", err)
	}

	if err := system_update_package.FetchPackages(pkgs, amber); err != nil {
		logger.Fatalf("failed getting packages: %s", err)
	}

	if err := system_update_package.WriteImgs(imgs, dataPath); err != nil {
		logger.Fatalf("error writing image file: %s", err)
	}

	logger.Infof("system update complete, rebooting...")

	dmctl, err := os.OpenFile("/dev/misc/dmctl", os.O_RDWR, os.ModePerm)
	if err != nil {
		logger.Errorf("error forcing restart: %s", err)
	}
	defer dmctl.Close()
	cmd := []byte("reboot")
	if _, err := dmctl.Write(cmd); err != nil {
		logger.Errorf("error writing to control socket: %s", err)
	}
}

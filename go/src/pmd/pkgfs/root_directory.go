// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"strings"
	"sync"
	"thinfs/fs"
	"time"
)

type rootDirectory struct {
	unsupportedDirectory
	fs   *Filesystem
	mu   sync.RWMutex
	dirs map[string]fs.Directory
}

func (d *rootDirectory) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *rootDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	parts := strings.SplitN(name, "/", 2)

	d.mu.RLock()
	subdir, ok := d.dirs[parts[0]]
	d.mu.RUnlock()
	if !ok {
		return nil, nil, nil, fs.ErrNotFound
	}

	if len(parts) == 1 {
		return nil, subdir, nil, nil
	}

	return subdir.Open(parts[1], flags)
}

func (d *rootDirectory) Read() ([]fs.Dirent, error) {
	debugLog("pkgfs:root:read")

	d.mu.RLock()
	dirs := make([]fs.Dirent, 0, len(d.dirs))
	for n := range d.dirs {
		dirs = append(dirs, dirDirEnt(n))
	}
	d.mu.RUnlock()
	return dirs, nil
}

func (d *rootDirectory) Close() error {
	debugLog("pkgfs:root:close")
	return nil
}

func (d *rootDirectory) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:root:stat")
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

// setDir sets the given path within the root directory to be served by the given fs.Directory
func (d *rootDirectory) setDir(path string, newDir fs.Directory) {
	d.mu.Lock()
	defer d.mu.Unlock()

	d.dirs[path] = newDir
}

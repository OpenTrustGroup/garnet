// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package pkgfs hosts a filesystem for interacting with packages that are
// stored on a host. It presents a tree of packages that are locally available
// and a tree that enables a user to add new packages and/or package content to
// the host.
package pkgfs

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"time"

	"application/lib/app/context"
	"fidl/bindings"
	"garnet/amber/api/amber"
	"thinfs/fs"
	"thinfs/zircon/rpc"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/pkg"
	"fuchsia.googlesource.com/pmd/blobstore"
	"fuchsia.googlesource.com/pmd/index"
)

// Filesystem is the top level container for a pkgfs server
type Filesystem struct {
	root      fs.Directory
	static    index.StaticIndex
	index     *index.DynamicIndex
	blobstore *blobstore.Manager
	mountInfo mountInfo
	mountTime time.Time
	amberPxy  *amber.Control_Proxy
}

// New initializes a new pkgfs filesystem server
func New(staticIndex, indexDir, blobstoreDir string) (*Filesystem, error) {
	var static index.StaticIndex
	if _, err := os.Stat(staticIndex); !os.IsNotExist(err) {
		static, err = index.LoadStaticIndex(staticIndex)
		if err != nil {
			// TODO(raggi): avoid crashing the process in cases like this
			return nil, err
		}
	}

	index, err := index.NewDynamic(indexDir)
	if err != nil {
		return nil, err
	}

	bm, err := blobstore.New(blobstoreDir, "")
	if err != nil {
		return nil, err
	}

	f := &Filesystem{
		static:    static,
		index:     index,
		blobstore: bm,
	}

	f.root = &rootDirectory{
		unsupportedDirectory: unsupportedDirectory("/"),
		fs:                   f,

		dirs: map[string]fs.Directory{
			"incoming": &inDirectory{
				unsupportedDirectory: unsupportedDirectory("/incoming"),
				fs:                   f,
			},
			"needs": &needsRoot{
				unsupportedDirectory: unsupportedDirectory("/needs"),
				fs:                   f,
			},
			"packages": &packagesRoot{
				unsupportedDirectory: unsupportedDirectory("/packages"),
				fs:                   f,
			},
			"metadata": unsupportedDirectory("/metadata"),
		},
	}

	var pxy *amber.Control_Proxy
	req, pxy := pxy.NewRequest(bindings.GetAsyncWaiter())
	context.CreateFromStartupInfo().ConnectToEnvService(req)
	f.amberPxy = pxy

	return f, nil
}

// NewSinglePackage initializes a new pkgfs filesystem that hosts only a single
// package.
func NewSinglePackage(blob, blobstoreDir string) (*Filesystem, error) {
	bm, err := blobstore.New(blobstoreDir, "")
	if err != nil {
		return nil, err
	}

	f := &Filesystem{
		static:    nil,
		index:     nil,
		blobstore: bm,
	}

	pd, err := newPackageDirFromBlob(blob, f)
	if err != nil {
		return nil, err
	}

	f.root = pd

	return f, nil
}

func (f *Filesystem) Blockcount() int64 {
	// TODO(raggi): sum up all packages?
	// TODO(raggi): delegate to blobstore?
	debugLog("fs blockcount")
	return 0
}

func (f *Filesystem) Blocksize() int64 {
	// TODO(raggi): sum up all packages?
	// TODO(raggi): delegate to blobstore?
	debugLog("fs blocksize")
	return 0
}

func (f *Filesystem) Size() int64 {
	debugLog("fs size")
	// TODO(raggi): delegate to blobstore?
	return 0
}

func (f *Filesystem) Close() error {
	debugLog("fs close")
	return fs.ErrNotSupported
}

func (f *Filesystem) RootDirectory() fs.Directory {
	return f.root
}

func (f *Filesystem) Type() string {
	return "pkgfs"
}

func (f *Filesystem) FreeSize() int64 {
	return 0
}

func (f *Filesystem) DevicePath() string {
	return ""
}

var _ fs.FileSystem = (*Filesystem)(nil)

type rootDirectory struct {
	unsupportedDirectory
	fs   *Filesystem
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

	subdir, ok := d.dirs[parts[0]]
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

	dirs := make([]fs.Dirent, 0, len(d.dirs))
	for n := range d.dirs {
		dirs = append(dirs, dirDirEnt(n))
	}
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

// inDirectory is located at /in. It accepts newly created files, and writes them to blobstore.
type inDirectory struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *inDirectory) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *inDirectory) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:in:stat")
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *inDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	debugLog("pkgfs:in:open %q", name)
	if name == "" {
		return nil, d, nil, nil
	}

	if !(flags.Create() && flags.File()) {
		return nil, nil, nil, fs.ErrNotFound
	}
	// TODO(raggi): validate/reject other flags

	// TODO(raggi): create separate incoming directories for blobs and packages
	return &inFile{unsupportedFile: unsupportedFile("/pkgfs/incoming/" + name), fs: d.fs, oname: name, name: ""}, nil, nil, nil
}

func (d *inDirectory) Close() error {
	debugLog("pkgfs:in:close")
	return nil
}

func (d *inDirectory) Unlink(target string) error {
	debugLog("pkgfs:in:unlink %s", target)
	return fs.ErrNotFound
}

type inFile struct {
	unsupportedFile
	fs *Filesystem

	mu sync.Mutex

	oname string
	name  string
	sz    int64
	w     io.WriteCloser
}

func (f *inFile) Write(p []byte, off int64, whence int) (int, error) {
	debugLog("pkgfs:infile:write %q [%d @ %d]", f.oname, len(p), off)
	f.mu.Lock()
	defer f.mu.Unlock()

	if f.w == nil {
		var err error
		f.w, err = f.fs.blobstore.Create(f.name, f.sz)
		if err != nil {
			log.Printf("pkgfs: incoming file %q blobstore creation failed %q: %s", f.oname, f.name, err)
			return 0, goErrToFSErr(err)
		}
	}

	if whence != fs.WhenceFromCurrent || off != 0 {
		return 0, fs.ErrNotSupported
	}

	n, err := f.w.Write(p)
	return n, goErrToFSErr(err)
}

func (f *inFile) Close() error {
	debugLog("pkgfs:infile:close %q", f.oname)
	f.mu.Lock()
	defer f.mu.Unlock()

	if f.w == nil {
		var err error
		f.w, err = f.fs.blobstore.Create(f.name, f.sz)
		if err != nil {
			log.Printf("pkgfs: incoming file %q blobstore creation failed %q: %s", f.oname, f.name, err)
			return goErrToFSErr(err)
		}
	}

	err := f.w.Close()

	root := f.name
	if rooter, ok := f.w.(blobstore.Rooter); ok {
		root, err = rooter.Root()
		if err != nil {
			log.Printf("pkgfs: root digest computation failed after successful blobstore write: %s", err)
			return goErrToFSErr(err)
		}
	}

	if err == nil || f.fs.blobstore.HasBlob(root) {
		log.Printf("pkgfs: wrote %q to blob %q", f.oname, root)

		if f.oname == root {
			// TODO(raggi): removing the blob need after the blob is written should signal to package activation that the need is fulfilled.
			os.Remove(f.fs.index.NeedsBlob(root))

			// XXX(raggi): TODO(raggi): temporary hack for synchronous needs fulfillment:
			checkNeeds(f.fs, root)
		} else {
			// remove the needs declaration that has just been fulfilled
			os.Remove(f.fs.index.NeedsFile(f.oname))

			// TODO(raggi): make this asynchronous? (makes the tests slightly harder)
			importPackage(f.fs, root)
		}
	}
	if err != nil {
		log.Printf("pkgfs: failed incoming file write to blobstore: %s", err)
	}
	return goErrToFSErr(err)
}

func (f *inFile) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:infile:stat %q", f.oname)
	return 0, time.Time{}, time.Time{}, nil
}

func (f *inFile) Truncate(sz uint64) error {
	debugLog("pkgfs:infile:truncate %q", f.oname)
	if f.w != nil {
		return fs.ErrInvalidArgs
	}
	// XXX(raggi): the usual Go mess with io size operations, this truncation is potentially bad.
	f.sz = int64(sz)
	return nil
}

// importPackage reads a package far from blobstore, given a content key, and imports it into the package index
func importPackage(fs *Filesystem, root string) {
	log.Printf("pkgfs: importing package from %q", root)

	f, err := fs.blobstore.Open(root)
	if err != nil {
		log.Printf("error importing package: %s", err)
		return
	}
	defer f.Close()

	// TODO(raggi): this is a bit messy, the system could instead force people to
	// write to specific paths in the incoming directory
	if !far.IsFAR(f) {
		log.Printf("pkgfs:importPackage: %q is not a package, ignoring import", root)
		return
	}
	f.Seek(0, io.SeekStart)

	r, err := far.NewReader(f)
	if err != nil {
		log.Printf("error reading package archive package: %s", err)
		return
	}

	// TODO(raggi): this can also be replaced if we enforce writes into specific places in the incoming tree
	var isPkg bool
	for _, f := range r.List() {
		if strings.HasPrefix(f, "meta/") {
			isPkg = true
		}
	}
	if !isPkg {
		log.Printf("pkgfs: %q does not contain a meta directory, assuming it is not a package", root)
		return
	}

	pf, err := r.ReadFile("meta/package.json")
	if err != nil {
		log.Printf("error reading package metadata: %s", err)
		return
	}

	var p pkg.Package
	err = json.Unmarshal(pf, &p)
	if err != nil {
		log.Printf("error parsing package metadata: %s", err)
		return
	}

	if err := p.Validate(); err != nil {
		log.Printf("pkgfs: package is invalid: %s", err)
		return
	}

	contents, err := r.ReadFile("meta/contents")
	if err != nil {
		log.Printf("error parsing package contents file: %s", err)
		return
	}

	pkgWaitingDir := fs.index.WaitingPackageVersionPath(p.Name, p.Version)

	files := bytes.Split(contents, []byte{'\n'})
	var needsCount int
	for i := range files {
		parts := bytes.SplitN(files[i], []byte{'='}, 2)
		if len(parts) != 2 {
			// TODO(raggi): log illegal contents format?
			continue
		}
		root := string(parts[1])

		if fs.blobstore.HasBlob(root) {
			log.Printf("pkgfs: blob already present %q", root)
			continue
		}

		log.Printf("pkgfs: blob needed %q", root)

		needsCount++
		err = ioutil.WriteFile(fs.index.NeedsBlob(root), []byte{}, os.ModePerm)
		if err != nil {
			// XXX(raggi): there are potential deadlock conditions here, we should fail the package write (???)
			log.Printf("pkgfs: import error, can't create needs index: %s", err)
		}

		if needsCount == 1 {
			os.MkdirAll(pkgWaitingDir, os.ModePerm)
		}

		err = ioutil.WriteFile(filepath.Join(pkgWaitingDir, root), []byte{}, os.ModePerm)
		if err != nil {
			log.Printf("pkgfs: import error, can't create needs index: %s", err)
		}

		// TODO(jmatt) limit concurrency, send this to a worker routine?
		go fs.amberPxy.GetBlob(root)
	}

	if needsCount == 0 {
		pkgIndexDir := fs.index.PackagePath(p.Name)
		os.MkdirAll(pkgIndexDir, os.ModePerm)

		if err := ioutil.WriteFile(filepath.Join(pkgIndexDir, p.Version), []byte(root), os.ModePerm); err != nil {
			// XXX(raggi): is this a really bad state?
			log.Printf("pkgfs: error writing package installed index for %s/%s: %s", p.Name, p.Version, err)
		}
	}

	// XXX(raggi): there's a potential race here where needs could be fulfilled
	// before this is written, so this should get re-orgnized to execute before the
	// needs files are written, and then the move should be done as if checkNeeds
	// completed.
	pkgInstalling := fs.index.InstallingPackageVersionPath(p.Name, p.Version)
	os.MkdirAll(filepath.Dir(pkgInstalling), os.ModePerm)

	if err := ioutil.WriteFile(pkgInstalling, []byte(root), os.ModePerm); err != nil {
		log.Printf("error writing package installing index for %s/%s: %s", p.Name, p.Version, err)
	}
}

func checkNeeds(fs *Filesystem, root string) {
	fulfillments, err := filepath.Glob(filepath.Join(fs.index.WaitingPackageVersionPath("*", "*"), root))
	if err != nil {
		log.Printf("pkgfs: error checking fulfillment of %s: %s", root, err)
		return
	}
	for _, path := range fulfillments {
		if err := os.Remove(path); err != nil {
			log.Printf("pkgfs: error removing %q: %s", path, err)
		}

		pkgWaitingDir := filepath.Dir(path)
		dir, err := os.Open(pkgWaitingDir)
		if err != nil {
			log.Printf("pkgfs: error opening waiting dir: %s: %s", pkgWaitingDir, err)
			continue
		}
		names, err := dir.Readdirnames(0)
		dir.Close()
		if err != nil {
			log.Printf("pkgfs: failed to check waiting dir %s: %s", pkgWaitingDir, err)
			continue
		}
		// if all the needs are fulfilled, move the package from installing to packages.
		if len(names) == 0 {
			pkgNameVersion, err := filepath.Rel(fs.index.WaitingDir(), pkgWaitingDir)
			if err != nil {
				log.Printf("pkgfs: error extracting package name from %s: %s", pkgWaitingDir, err)
				continue
			}

			from := filepath.Join(fs.index.InstallingDir(), pkgNameVersion)
			to := filepath.Join(fs.index.PackagesDir(), pkgNameVersion)
			os.MkdirAll(filepath.Dir(to), os.ModePerm)
			debugLog("package %s ready, moving %s to %s", pkgNameVersion, from, to)
			if err := os.Rename(from, to); err != nil {
				// TODO(raggi): this kind of state will need to be cleaned up by a general garbage collector at a later time.
				log.Printf("pkgfs: error moving package from installing to packages: %s", err)
			}
		}
	}
}

type packagesRoot struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *packagesRoot) Dup() (fs.Directory, error) {
	return d, nil
}

func (pr *packagesRoot) Close() error { return nil }

func (pr *packagesRoot) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	debugLog("pkgfs:packagesroot:open %q", name)
	if name == "" {
		return nil, pr, nil, nil
	}

	parts := strings.Split(name, "/")

	pld, err := newPackageListDir(parts[0], pr.fs)
	if err != nil {
		log.Printf("pkgfs:packagesroot:open error reading package list dir for %q: %s", name, err)
		return nil, nil, nil, err
	}
	if len(parts) > 1 {
		debugLog("pkgfs:packagesroot:open forwarding %v to %q", parts[1:], name)
		return pld.Open(filepath.Join(parts[1:]...), flags)
	}
	return nil, pld, nil, nil
}

func (pr *packagesRoot) Read() ([]fs.Dirent, error) {
	debugLog("pkgfs:packagesroot:read")

	var names []string
	if pr.fs.static != nil {
		pkgs, err := pr.fs.static.List()
		if err != nil {
			return nil, err
		}
		names = make([]string, len(pkgs))
		for i, p := range pkgs {
			names[i] = p.Name
		}
	}

	dnames, err := filepath.Glob(pr.fs.index.PackagePath("*"))
	if err != nil {
		return nil, goErrToFSErr(err)
	}

	names = append(names, dnames...)

	dirents := make([]fs.Dirent, len(names))
	for i := range names {
		dirents[i] = fileDirEnt(filepath.Base(names[i]))
	}
	return dirents, nil
}

func (pr *packagesRoot) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:packagesRoot:stat")
	// TODO(raggi): stat the index directory and pass on info
	return 0, pr.fs.mountTime, pr.fs.mountTime, nil
}

// packageListDir is a directory in the pkgfs packages directory for an
// individual package that lists all versions of packages
type packageListDir struct {
	unsupportedDirectory
	fs          *Filesystem
	packageName string
}

func newPackageListDir(name string, f *Filesystem) (*packageListDir, error) {
	debugLog("pkgfs:newPackageListDir: %q", name)
	if !f.static.HasName(name) {
		_, err := os.Stat(f.index.PackagePath(name))
		if os.IsNotExist(err) {
			debugLog("pkgfs:newPackageListDir: %q not found", name)
			return nil, fs.ErrNotFound
		}
		if err != nil {
			log.Printf("pkgfs: error opening package: %q: %s", name, err)
			return nil, err
		}
	}

	pld := packageListDir{
		unsupportedDirectory: unsupportedDirectory(filepath.Join("/packages", name)),
		fs:                   f,
		packageName:          name,
	}
	return &pld, nil
}

func (d *packageListDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (pld *packageListDir) Close() error {
	debugLog("pkgfs:packageListDir:close %q", pld.packageName)
	return nil
}

func (pld *packageListDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	debugLog("pkgfs:packageListDir:open %q %s", pld.packageName, name)

	parts := strings.Split(name, "/")

	d, err := newPackageDir(pld.packageName, parts[0], pld.fs)
	if err != nil {
		return nil, nil, nil, err
	}

	if len(parts) > 1 {
		return d.Open(filepath.Join(parts[1:]...), flags)
	}
	return nil, d, nil, nil
}

func (pld *packageListDir) Read() ([]fs.Dirent, error) {
	debugLog("pkgfs:packageListdir:read %q", pld.packageName)

	if pld.fs.static != nil && pld.fs.static.HasName(pld.packageName) {
		versions := pld.fs.static.ListVersions(pld.packageName)
		dirents := make([]fs.Dirent, len(versions))
		for i := range versions {
			dirents[i] = fileDirEnt(versions[i])
		}
		return dirents, nil
	}

	names, err := filepath.Glob(pld.fs.index.PackageVersionPath(pld.packageName, "*"))
	if err != nil {
		return nil, goErrToFSErr(err)
	}
	dirents := make([]fs.Dirent, len(names))
	for i := range names {
		dirents[i] = fileDirEnt(filepath.Base(names[i]))
	}
	return dirents, nil
}

func (pld *packageListDir) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:packageListDir:stat %q", pld.packageName)
	// TODO(raggi): stat the index directory and pass on info
	return 0, pld.fs.mountTime, pld.fs.mountTime, nil
}

type packageDir struct {
	unsupportedDirectory
	fs            *Filesystem
	name, version string
	contents      map[string]string

	// if this packagedir is a subdirectory, then this is the prefix name
	subdir *string
}

func newPackageDir(name, version string, filesystem *Filesystem) (*packageDir, error) {
	merkleroot := ""
	if filesystem.static != nil {
		merkleroot = filesystem.static.GetPackage(name, version)
	}

	if merkleroot == "" {
		bmerkle, err := ioutil.ReadFile(filesystem.index.PackageVersionPath(name, version))
		if err != nil {
			return nil, goErrToFSErr(err)
		}
		merkleroot = string(bmerkle)
	}

	pd, err := newPackageDirFromBlob(merkleroot, filesystem)
	if err != nil {
		return nil, err
	}

	// update the name related fields for easier debugging:
	pd.unsupportedDirectory = unsupportedDirectory(filepath.Join("/packages", name, version))
	pd.name = name
	pd.version = version

	return pd, nil
}

// Initialize a package directory server interface from a package meta.far
func newPackageDirFromBlob(blob string, filesystem *Filesystem) (*packageDir, error) {
	blobPath := filepath.Join(filesystem.blobstore.Root, blob)
	f, err := os.Open(blobPath)
	if err != nil {
		log.Printf("pkgfs: failed to open package contents at %q: %s", blob, err)
		return nil, goErrToFSErr(err)
	}
	defer f.Close()

	fr, err := far.NewReader(f)
	if err != nil {
		log.Printf("pkgfs: failed to read meta.far at %q: %s", blob, err)
		return nil, goErrToFSErr(err)
	}

	buf, err := fr.ReadFile("meta/contents")
	if err != nil {
		log.Printf("pkgfs: failed to read meta/contents from %q: %s", blob, err)
		return nil, goErrToFSErr(err)
	}

	pd, err := newPackageDirFromReader(bytes.NewReader(buf), filesystem)
	if err != nil {
		return nil, goErrToFSErr(err)
	}
	pd.unsupportedDirectory = unsupportedDirectory(blob)
	pd.name = blob
	pd.version = ""

	pd.contents["meta"] = blob

	return pd, nil
}

func newPackageDirFromReader(r io.Reader, filesystem *Filesystem) (*packageDir, error) {
	pd := packageDir{
		unsupportedDirectory: unsupportedDirectory("packageDir"),
		fs:                   filesystem,
		contents:             map[string]string{},
	}

	b := bufio.NewReader(r)

	for {
		line, err := b.ReadString('\n')
		if err == io.EOF {
			if len(line) == 0 {
				break
			}
			err = nil
		}

		if err != nil {
			log.Printf("pkgfs: failed to read package contents from %v: %s", r, err)
			// TODO(raggi): better error?
			return nil, fs.ErrFailedPrecondition
		}
		line = strings.TrimSpace(line)
		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			log.Printf("pkgfs: bad contents line: %v", line)
			continue
		}
		pd.contents[parts[0]] = parts[1]
	}

	return &pd, nil
}

func (d *packageDir) Close() error {
	debugLog("pkgfs:packageDir:close %q/%q", d.name, d.version)
	return nil
}

func (d *packageDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *packageDir) Reopen(flags fs.OpenFlags) (fs.Directory, error) {
	return d, nil
}

func (d *packageDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	debugLog("pkgfs:packagedir:open %q", name)

	if d.subdir != nil {
		name = filepath.Join(*d.subdir, name)
	}

	if name == "" {
		return nil, d, nil, nil
	}

	if flags.Create() || flags.Truncate() || flags.Write() || flags.Append() {
		debugLog("pkgfs:packagedir:open %q unsupported flags", name)
		return nil, nil, nil, fs.ErrNotSupported
	}

	if name == "meta" || strings.HasPrefix(name, "meta/") {
		if blob, ok := d.contents["meta"]; ok {
			d, err := newMetaFarDir(d.name, d.version, blob, d.fs)
			if err != nil {
				return nil, nil, nil, err
			}
			return d.Open(strings.TrimPrefix(name, "meta"), flags)
		}
		return nil, nil, nil, fs.ErrNotFound
	}

	if root, ok := d.contents[name]; ok {
		return nil, nil, &fs.Remote{Channel: d.fs.blobstore.Channel(), Path: root, Flags: flags}, nil
	}

	dirname := name + "/"
	for k := range d.contents {
		if strings.HasPrefix(k, dirname) {
			// subdir is a copy of d, but with subdir set
			subdir := *d
			subdir.subdir = &dirname
			return nil, &subdir, nil, nil
		}
	}

	debugLog("pkgfs:packagedir:open %q not found", name)
	return nil, nil, nil, fs.ErrNotFound
}

func (d *packageDir) Read() ([]fs.Dirent, error) {
	// TODO(raggi): improve efficiency
	dirs := map[string]struct{}{}
	dents := []fs.Dirent{}
	dents = append(dents, dirDirEnt("."))
	for name := range d.contents {
		if d.subdir != nil {
			if !strings.HasPrefix(name, *d.subdir) {
				continue
			}
			name = strings.TrimPrefix(name, *d.subdir)
		}

		parts := strings.SplitN(name, "/", 2)
		if len(parts) == 2 {
			if _, ok := dirs[parts[0]]; !ok {
				dirs[parts[0]] = struct{}{}
				dents = append(dents, dirDirEnt(parts[0]))
			}

		} else {
			dents = append(dents, fileDirEnt(parts[0]))
		}
	}
	return dents, nil
}

func (d *packageDir) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:packagedir:stat %q/%q", d.name, d.version)
	// TODO(raggi): forward stat values from the index
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

type dirDirEnt string

func (d dirDirEnt) GetType() fs.FileType {
	return fs.FileTypeDirectory
}

func (d dirDirEnt) GetName() string {
	return string(d)
}

type fileDirEnt string

func (d fileDirEnt) GetType() fs.FileType {
	return fs.FileTypeRegularFile
}

func (d fileDirEnt) GetName() string {
	return string(d)
}

type needsRoot struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *needsRoot) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *needsRoot) Close() error {
	return nil
}

func (d *needsRoot) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	parts := strings.SplitN(name, "/", 2)

	switch parts[0] {
	case "blobs":
		nbd := &needsBlobsDir{unsupportedDirectory: unsupportedDirectory("/needs/blobs"), fs: d.fs}
		if len(parts) > 1 {
			return nbd.Open(parts[1], flags)
		}
		return nil, nbd, nil, nil
	default:
		if len(parts) != 1 {
			return nil, nil, nil, fs.ErrNotSupported
		}

		idxPath := d.fs.index.NeedsFile(parts[0])

		var f *os.File
		var err error
		if flags.Create() {
			f, err = os.Create(idxPath)
			go d.fs.amberPxy.GetUpdate(parts[0], nil)
		} else {
			f, err = os.Open(idxPath)
		}
		if err != nil {
			return nil, nil, nil, goErrToFSErr(err)
		}
		if err := f.Close(); err != nil {
			return nil, nil, nil, goErrToFSErr(err)
		}
		return &needsFile{unsupportedFile: unsupportedFile(filepath.Join("/needs", name)), fs: d.fs}, nil, nil, nil
	}
}

func (d *needsRoot) Read() ([]fs.Dirent, error) {
	infos, err := ioutil.ReadDir(d.fs.index.NeedsDir())
	if err != nil {
		return nil, goErrToFSErr(err)
	}

	var dents = make([]fs.Dirent, len(infos))

	for i, info := range infos {
		if info.IsDir() {
			dents[i] = dirDirEnt(filepath.Base(info.Name()))
		} else {
			dents[i] = fileDirEnt(filepath.Base(info.Name()))
		}
	}

	return dents, nil
}

func (d *needsRoot) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): provide more useful values
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

type needsFile struct {
	unsupportedFile

	fs *Filesystem
}

func (f *needsFile) Close() error {
	return nil
}

func (f *needsFile) Stat() (int64, time.Time, time.Time, error) {
	return 0, time.Time{}, time.Time{}, nil
}

type needsBlobsDir struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *needsBlobsDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *needsBlobsDir) Close() error {
	return nil
}

func (d *needsBlobsDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	if strings.Contains(name, "/") {
		return nil, nil, nil, fs.ErrNotFound
	}

	if _, err := os.Stat(d.fs.index.NeedsBlob(name)); err != nil {
		return nil, nil, nil, goErrToFSErr(err)
	}

	debugLog("pkgfs:needsblob:%q open", name)
	return &inFile{unsupportedFile: unsupportedFile("/needs/blobs/" + name), fs: d.fs, oname: name, name: name}, nil, nil, nil
}

func (d *needsBlobsDir) Read() ([]fs.Dirent, error) {
	names, err := filepath.Glob(d.fs.index.NeedsBlob("*"))
	if err != nil {
		return nil, goErrToFSErr(err)
	}
	dirents := make([]fs.Dirent, len(names))
	for i := range names {
		dirents[i] = fileDirEnt(filepath.Base(names[i]))
	}
	return dirents, nil
}

func (d *needsBlobsDir) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): provide more useful values
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

// clean canonicalizes a path and returns a path that is relative to an assumed root.
// as a result of this cleaning operation, an open of '/' or '.' or '' all return ''.
// TODO(raggi): speed this up/reduce allocation overhead.
func clean(path string) string {
	return filepath.Clean("/" + path)[1:]
}

// mountInfo is a platform specific type that carries platform specific mounting
// data, such as the file descriptor or handle of the mount.
type mountInfo struct {
	unmountOnce  sync.Once
	serveChannel *zx.Channel
	parentFd     int
}

// Mount attaches the filesystem host to the given path. If an error occurs
// during setup, this method returns that error. If an error occurs after
// serving has started, the error causes a log.Fatal. If the given path does not
// exist, it is created before mounting.
func (f *Filesystem) Mount(path string) error {
	err := os.MkdirAll(path, os.ModePerm)
	if err != nil {
		return err
	}

	f.mountInfo.parentFd, err = syscall.Open(path, syscall.O_ADMIN|syscall.O_DIRECTORY, 0777)
	if err != nil {
		return err
	}

	var rpcChan *zx.Channel
	rpcChan, f.mountInfo.serveChannel, err = zx.NewChannel(0)
	if err != nil {
		syscall.Close(f.mountInfo.parentFd)
		f.mountInfo.parentFd = -1
		return fmt.Errorf("channel creation: %s", err)
	}

	if err := syscall.FDIOForFD(f.mountInfo.parentFd).IoctlSetHandle(fdio.IoctlVFSMountFS, f.mountInfo.serveChannel.Handle); err != nil {
		f.mountInfo.serveChannel.Close()
		f.mountInfo.serveChannel = nil
		syscall.Close(f.mountInfo.parentFd)
		f.mountInfo.parentFd = -1
		return fmt.Errorf("mount failure: %s", err)
	}

	vfs, err := rpc.NewServer(f, rpcChan.Handle)
	if err != nil {
		f.mountInfo.serveChannel.Close()
		f.mountInfo.serveChannel = nil
		syscall.Close(f.mountInfo.parentFd)
		f.mountInfo.parentFd = -1
		return fmt.Errorf("vfs server creation: %s", err)
	}

	// TODO(raggi): handle the exit case more cleanly.
	go func() {
		defer f.Unmount()
		vfs.Serve()
	}()
	return nil
}

// Unmount detaches the filesystem from a previously mounted path. If mount was not previously called or successful, this will panic.
func (f *Filesystem) Unmount() {
	f.mountInfo.unmountOnce.Do(func() {
		// TODO(raggi): log errors?
		syscall.FDIOForFD(f.mountInfo.parentFd).Ioctl(fdio.IoctlVFSUnmountNode, nil, nil)
		f.mountInfo.serveChannel.Close()
		syscall.Close(f.mountInfo.parentFd)
		f.mountInfo.serveChannel = nil
		f.mountInfo.parentFd = -1
	})
}

func goErrToFSErr(err error) error {
	switch e := err.(type) {
	case nil:
		return nil
	case *os.PathError:
		return goErrToFSErr(e.Err)
	case zx.Error:
		switch e.Status {
		case zx.ErrNotFound:
			return fs.ErrNotFound

		default:
			debugLog("pkgfs: unmapped os err to fs err: %T %v", err, err)
			return err

		}
	}
	switch err {
	case os.ErrInvalid:
		return fs.ErrInvalidArgs
	case os.ErrPermission:
		return fs.ErrPermission
	case os.ErrExist:
		return fs.ErrAlreadyExists
	case os.ErrNotExist:
		return fs.ErrNotFound
	case os.ErrClosed:
		return fs.ErrNotOpen
	case io.EOF:
		return fs.ErrEOF
	default:
		debugLog("pkgfs: unmapped os err to fs err: %T %v", err, err)
		return err
	}
}

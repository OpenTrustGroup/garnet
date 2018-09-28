// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"sync"
	"time"

	"amber/lg"
	"amber/pkg"

	"fidl/fuchsia/amber"

	tuf "github.com/flynn/go-tuf/client"
	tuf_data "github.com/flynn/go-tuf/data"
	"github.com/rjw57/oauth2device"
	"golang.org/x/oauth2"

	"github.com/flynn/go-tuf/verify"
)

const (
	configFileName = "config.json"
)

// ErrTufSrcNoHash is returned if the TUF entry doesn't have a SHA512 hash
var ErrTufSrcNoHash = errors.New("tufsource: hash missing or wrong type")

type tufSourceConfig struct {
	Config *amber.SourceConfig

	Oauth2Token *oauth2.Token

	Status *SourceStatus
}

type SourceStatus struct {
	Enabled *bool
}

// LoadSourceConfigs loads source configs from a directory.  The directory
// structure looks like:
//
//     $dir/source1/config.json
//     $dir/source2/config.json
//     ...
//
// If an error is encountered loading any config, none are returned.
func LoadSourceConfigs(dir string) ([]*amber.SourceConfig, error) {
	files, err := ioutil.ReadDir(dir)
	if err != nil {
		return nil, err
	}

	configs := make([]*amber.SourceConfig, 0, len(files))
	for _, file := range files {
		p := filepath.Join(dir, file.Name(), configFileName)
		log.Printf("loading source config %s", p)

		cfg, err := LoadConfigFromDir(p)
		if err != nil {
			return nil, err
		}
		configs = append(configs, cfg)
	}

	return configs, nil
}

func LoadConfigFromDir(path string) (*amber.SourceConfig, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var cfg amber.SourceConfig
	if err := json.NewDecoder(f).Decode(&cfg); err != nil {
		return nil, err
	}

	// it is possible we encounter a config on disk that does not have
	// this value set, set the defaults
	if cfg.StatusConfig == nil {
		cfg.StatusConfig = &amber.StatusConfig{Enabled: true}
	}

	return &cfg, nil
}

func newTUFSourceConfig(cfg *amber.SourceConfig) (tufSourceConfig, error) {
	if cfg.Id == "" {
		return tufSourceConfig{}, fmt.Errorf("tuf source id cannot be empty")
	}

	if _, err := url.ParseRequestURI(cfg.RepoUrl); err != nil {
		return tufSourceConfig{}, err
	}

	if len(cfg.RootKeys) == 0 {
		return tufSourceConfig{}, fmt.Errorf("no root keys provided")
	}

	status := false
	srcStatus := &SourceStatus{&status}
	if cfg.StatusConfig != nil && cfg.StatusConfig.Enabled {
		*srcStatus.Enabled = true
	}

	return tufSourceConfig{
		Config: cfg,
		Status: srcStatus,
	}, nil
}

// TUFSource wraps a TUF Client into the Source interface
type TUFSource struct {
	// mu guards all following fields
	mu sync.Mutex

	cfg tufSourceConfig

	dir string

	// We save a reference to the tuf local store so that when we update
	// the tuf client, we can reuse the store. This avoids us from having
	// multiple database connections to the same file, which could corrupt
	// the TUF database.
	localStore tuf.LocalStore

	tokenSource oauth2.TokenSource
	httpClient  *http.Client

	keys      []*tuf_data.Key
	tufClient *tuf.Client
}

type merkle struct {
	Root string `json:"merkle"`
}

type RemoteStoreError struct {
	error
}

type IOError struct {
	error
}

func NewTUFSource(dir string, c *amber.SourceConfig) (*TUFSource, error) {
	if dir == "" {
		return nil, fmt.Errorf("tuf source directory cannot be an empty string")
	}

	cfg, err := newTUFSourceConfig(c)
	if err != nil {
		return nil, err
	}

	src := TUFSource{
		cfg: cfg,
		dir: dir,
	}

	if err := src.initSource(); err != nil {
		return nil, err
	}

	return &src, nil
}

// setEnabledStatus examines the config to see if the Status field exists and
// if enabled is set. If either is not present the field is added and set to
// enabled. If the sourceConfig is changed true is returned, otherwise false.
func setEnabledStatus(cfg *tufSourceConfig) bool {
	dirty := false
	if cfg.Status == nil {
		enabled := true
		cfg.Status = &SourceStatus{&enabled}
		dirty = true
	} else if cfg.Status.Enabled == nil {
		enabled := true
		cfg.Status.Enabled = &enabled
		dirty = true
	}

	// it is possible we encounter a config on disk that does not have
	// this value set, set the defaults
	if cfg.Config.StatusConfig == nil {
		cfg.Config.StatusConfig = &amber.StatusConfig{Enabled: true}
		dirty = true
	}

	return dirty
}

func LoadTUFSourceFromPath(dir string) (*TUFSource, error) {
	log.Printf("loading source from %s", dir)

	f, err := os.Open(filepath.Join(dir, configFileName))
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var cfg tufSourceConfig
	if err := json.NewDecoder(f).Decode(&cfg); err != nil {
		return nil, err
	}

	dirty := setEnabledStatus(&cfg)

	src := TUFSource{
		cfg: cfg,
		dir: dir,
	}

	if dirty {
		src.Save()
	}

	if err := src.initSource(); err != nil {
		return nil, err
	}

	return &src, nil
}

func (f *TUFSource) Enabled() bool {
	return *f.cfg.Status.Enabled
}

// This function finishes initializing the TUFSource by parsing out the config
// data to create the derived fields, like the TUFClient.
func (f *TUFSource) initSource() error {
	f.mu.Lock()
	defer f.mu.Unlock()

	keys, err := newTUFKeys(f.cfg.Config.RootKeys)
	if err != nil {
		return err
	}
	f.keys = keys

	// We might have multiple things in the store directory, so put tuf in
	// it's own directory.
	localStore, err := NewFileStore(filepath.Join(f.dir, "tuf.json"))
	if err != nil {
		return IOError{fmt.Errorf("couldn't open datastore: %s", err)}
	}
	f.localStore = localStore

	if err := f.updateTUFClientLocked(); err != nil {
		return err
	}

	return nil
}

func oauth2deviceConfig(c *amber.OAuth2Config) *oauth2device.Config {
	if c == nil {
		return nil
	}

	return &oauth2device.Config{
		Config: &oauth2.Config{
			ClientID:     c.ClientId,
			ClientSecret: c.ClientSecret,
			Endpoint: oauth2.Endpoint{
				AuthURL:  c.AuthUrl,
				TokenURL: c.TokenUrl,
			},
			Scopes: c.Scopes,
		},
		DeviceEndpoint: oauth2device.DeviceEndpoint{
			CodeURL: c.DeviceCodeUrl,
		},
	}
}

// Initialize (or reinitialize) the TUFClient. This is especially useful when
// logging in, or modifying any of the http settings since there's no way to
// change settings in place, so we need to replace it with a new tuf.Client.
//
// NOTE: It's the responsibility of the caller to hold the mutex before calling
// this function.
func (f *TUFSource) updateTUFClientLocked() error {
	httpClient, err := newHTTPClient(f.cfg.Config.TransportConfig)
	if err != nil {
		return err
	}

	// If we have oauth2 configured, we need to wrap the client in order to
	// inject the authentication header.
	var tokenSource oauth2.TokenSource

	if c := oauth2deviceConfig(f.cfg.Config.Oauth2Config); c != nil {
		// Store the client in the context so oauth2 can use it to
		// fetch the token. This client's transport will also be used
		// as the base of the client oauth2 returns to us, except for
		// the request timeout, which we manually have to copy over.
		ctx := context.WithValue(context.Background(), oauth2.HTTPClient, httpClient)

		timeout := httpClient.Timeout

		tokenSource = c.TokenSource(ctx, f.cfg.Oauth2Token)
		httpClient = oauth2.NewClient(ctx, tokenSource)

		httpClient.Timeout = timeout
	}

	// Create a new tuf client that uses the new http client.
	remoteStore, err := tuf.HTTPRemoteStore(f.cfg.Config.RepoUrl, nil, httpClient)
	if err != nil {
		return RemoteStoreError{fmt.Errorf("server address not understood: %s", err)}
	}
	tufClient := tuf.NewClient(f.localStore, remoteStore)

	// We got our tuf client ready to go. Before we store the client in our
	// source, make sure to close the old client's transport's idle
	// connections so we don't leave a bunch of sockets open.
	f.closeIdleConnections()

	// We're done! Save the clients for the next time we update our source.
	f.tokenSource = tokenSource
	f.httpClient = httpClient
	f.tufClient = tufClient

	return nil
}

func newHTTPClient(cfg *amber.TransportConfig) (*http.Client, error) {
	if cfg == nil {
		return http.DefaultClient, nil
	}

	tlsClientConfig, err := newTLSClientConfig(cfg.TlsClientConfig)
	if err != nil {
		return nil, err
	}

	t := &http.Transport{
		Proxy: http.ProxyFromEnvironment,
		DialContext: (&net.Dialer{
			Timeout:   time.Duration(cfg.ConnectTimeout) * time.Millisecond,
			KeepAlive: time.Duration(cfg.KeepAlive) * time.Millisecond,
		}).DialContext,
		MaxIdleConns:          int(cfg.MaxIdleConns),
		MaxIdleConnsPerHost:   int(cfg.MaxIdleConnsPerHost),
		IdleConnTimeout:       time.Duration(cfg.IdleConnTimeout) * time.Millisecond,
		ResponseHeaderTimeout: time.Duration(cfg.ResponseHeaderTimeout) * time.Millisecond,
		ExpectContinueTimeout: time.Duration(cfg.ExpectContinueTimeout) * time.Millisecond,
		TLSClientConfig:       tlsClientConfig,
	}

	c := &http.Client{
		Transport: t,
		Timeout:   time.Duration(cfg.RequestTimeout) * time.Millisecond,
	}

	return c, nil
}

func newTLSClientConfig(cfg *amber.TlsClientConfig) (*tls.Config, error) {
	if cfg == nil {
		return nil, nil
	}

	t := &tls.Config{
		InsecureSkipVerify: cfg.InsecureSkipVerify,
	}

	if len(cfg.RootCAs) != 0 {
		t.RootCAs = x509.NewCertPool()
		for _, ca := range cfg.RootCAs {
			if !t.RootCAs.AppendCertsFromPEM([]byte(ca)) {
				log.Printf("failed to add cert")
				return nil, fmt.Errorf("failed to add certificate")
			}
		}
	}

	return t, nil
}

// Note, the mutex should be held when this is called.
func (f *TUFSource) initLocalStoreLocked() error {
	if needsInit(f.localStore) {
		log.Print("initializing local TUF store")
		err := f.tufClient.Init(f.keys, len(f.keys))
		if err != nil {
			return fmt.Errorf("TUF init failed: %s", err)
		}
	}

	return nil
}

func newTUFKeys(cfg []amber.KeyConfig) ([]*tuf_data.Key, error) {
	keys := make([]*tuf_data.Key, len(cfg))

	for i, key := range cfg {
		if key.Type != "ed25519" {
			return nil, fmt.Errorf("unsupported key type %s", key.Type)
		}

		keyHex, err := hex.DecodeString(key.Value)
		if err != nil {
			return nil, fmt.Errorf("invalid key value: %s", err)
		}

		keys[i] = &tuf_data.Key{
			Type:  key.Type,
			Value: tuf_data.KeyValue{Public: tuf_data.HexBytes(keyHex)},
		}
	}

	return keys, nil
}

func (f *TUFSource) GetId() string {
	return f.cfg.Config.Id
}

func (f *TUFSource) GetConfig() *amber.SourceConfig {
	return f.cfg.Config
}

func (f *TUFSource) SetEnabled(enabled bool) {
	f.cfg.Status.Enabled = &enabled
}

func (f *TUFSource) GetHttpClient() (*http.Client, error) {
	f.mu.Lock()
	defer f.mu.Unlock()

	if err := f.refreshOauth2TokenLocked(); err != nil {
		return nil, fmt.Errorf("failed to refresh oauth2 token: %s", err)
	}

	// http.Client itself is thread safe, but the member alias is not, so is
	// guarded here.
	return f.httpClient, nil
}

// Check if the token has refreshed. If so, save a new token
func (f *TUFSource) refreshOauth2TokenLocked() error {
	if f.cfg.Oauth2Token == nil {
		return nil
	}

	// Grab the latest token from the token source. If the token has
	// expired, it will automatically refresh it in the background and give
	// us a new access token.
	newToken, err := f.tokenSource.Token()
	if err != nil {
		return err
	}

	if newToken.AccessToken != f.cfg.Oauth2Token.AccessToken {
		log.Printf("refreshed oauth2 token for: %s", f.cfg.Config.Id)
		f.cfg.Oauth2Token = newToken
		f.saveLocked()
	}

	return nil
}

func (f *TUFSource) Login() (*amber.DeviceCode, error) {
	log.Printf("logging into tuf source: %s", f.cfg.Config.Id)

	c := oauth2deviceConfig(f.cfg.Config.Oauth2Config)
	if c == nil {
		log.Printf("source is not configured for oauth2")
		return nil, fmt.Errorf("source is not configured for oauth2")
	}

	code, err := oauth2device.RequestDeviceCode(http.DefaultClient, c)
	if err != nil {
		log.Printf("failed to get device code: %s", err)
		return nil, fmt.Errorf("failed to get device code: %s", err)
	}

	// Wait for the device authorization in a separate thread so we don't
	// block the response. This thread will eventually time out if the user
	// does not enter in the code.
	go f.waitForDeviceAuthorization(c, code)

	return &amber.DeviceCode{
		VerificationUrl: code.VerificationURL,
		UserCode:        code.UserCode,
		ExpiresIn:       code.ExpiresIn,
	}, nil
}

// Wait for the oauth2 server to authorize us to access the TUF repository. If
// we are denied access, we will log the error, but otherwise do nothing.
func (f *TUFSource) waitForDeviceAuthorization(config *oauth2device.Config, code *oauth2device.DeviceCode) {
	log.Printf("waiting for device authorization: %s", f.cfg.Config.Id)

	token, err := oauth2device.WaitForDeviceAuthorization(http.DefaultClient, config, code)
	if err != nil {
		log.Printf("failed to get device authorization token: %s", err)
		return
	}

	log.Printf("token received for remote store: %s", f.cfg.Config.Id)

	// Now that we have a token, grab the lock, and update our client.
	f.mu.Lock()
	defer f.mu.Unlock()

	f.cfg.Oauth2Token = token

	if err := f.updateTUFClientLocked(); err != nil {
		log.Printf("failed to update tuf client: %s", err)
		return
	}

	if err := f.saveLocked(); err != nil {
		log.Printf("failed to save config: %s", err)
		return
	}

	log.Printf("remote store updated: %s", f.cfg.Config.Id)
}

// AvailableUpdates takes a list of Packages and returns a map from those Packages
// to any available update Package
func (f *TUFSource) AvailableUpdates(pkgs []*pkg.Package) (map[pkg.Package]pkg.Package, error) {
	f.mu.Lock()
	defer f.mu.Unlock()

	if err := f.initLocalStoreLocked(); err != nil {
		return nil, fmt.Errorf("tuf_source: source could not be initialized: %s", err)
	}

	if err := f.refreshOauth2TokenLocked(); err != nil {
		return nil, fmt.Errorf("tuf_source: failed to refresh oauth2 token: %s", err)
	}

	_, err := f.tufClient.Update()
	if err != nil && !tuf.IsLatestSnapshot(err) {
		if _, ok := err.(tuf.ErrDecodeFailed); ok {
			e := err.(tuf.ErrDecodeFailed)
			if _, ok := e.Err.(verify.ErrLowVersion); ok {
				err = fmt.Errorf("tuf_source: verify update repository is current or reset "+
					"device storage %s", err)
			}
		}
		return nil, err
	}

	// TODO(jmatt) seems like 'm' should be the same as returned from
	// Client.Update, but empirically this seems untrue, investigate
	m, err := f.tufClient.Targets()

	if err != nil {
		return nil, err
	}

	updates := make(map[pkg.Package]pkg.Package)

	for _, p := range pkgs {
		meta, ok := m[p.Name]
		if !ok {
			continue
		}
		hash, ok := meta.Hashes["sha512"]
		if !ok {
			continue
		}
		hashStr := hash.String()

		m := merkle{}
		if meta.Custom != nil {
			json.Unmarshal(*meta.Custom, &m)
		}

		if (len(p.Version) == 0 || p.Version == hashStr) &&
			(len(p.Merkle) == 0 || p.Merkle == m.Root) {
			lg.Log.Printf("tuf_source: available update %s version %s\n",
				p.Name, hashStr[:8])
			updates[*p] = pkg.Package{
				Name:    p.Name,
				Version: hashStr,
				Merkle:  m.Root,
			}
		}
	}

	return updates, nil
}

// create a wrapper for File so it conforms to interface Client.Download expects
type delFile struct {
	*os.File
}

// Delete removes the file from the filesystem
func (f *delFile) Delete() error {
	f.Close()
	return os.Remove(f.Name())
}

// FetchPkg gets the content for the requested Package
func (f *TUFSource) FetchPkg(pkg *pkg.Package) (*os.File, error) {
	f.mu.Lock()
	defer f.mu.Unlock()

	if err := f.initLocalStoreLocked(); err != nil {
		return nil, fmt.Errorf("tuf_source: source could not be initialized: %s", err)
	}
	lg.Log.Printf("tuf_source: requesting download for: %s\n", pkg.Name)

	if err := f.refreshOauth2TokenLocked(); err != nil {
		return nil, fmt.Errorf("failed to refresh oauth2 token: %s", err)
	}

	tmp, err := ioutil.TempFile("", pkg.Version)
	if err != nil {
		return nil, err
	}

	err = f.tufClient.Download(pkg.Name, &delFile{tmp})
	if err != nil {
		tmp.Close()
		os.Remove(tmp.Name())
		return nil, ErrNoUpdateContent
	}

	_, err = tmp.Seek(0, os.SEEK_SET)
	if err != nil {
		tmp.Close()
		os.Remove(tmp.Name())
		return nil, err
	}
	return tmp, nil
}

// CheckInterval returns the time between which checks should be spaced.
func (f *TUFSource) CheckInterval() time.Duration {
	// TODO(jmatt) figure out how to establish a real value from the
	// Client we wrap
	return time.Millisecond * time.Duration(f.cfg.Config.RatePeriod)
}

func (f *TUFSource) CheckLimit() uint64 {
	return f.cfg.Config.RateLimit
}

// Equals returns true if the Source passed in is a pointer to this instance
func (f *TUFSource) Equals(o Source) bool {
	switch o.(type) {
	case *TUFSource:
		return f == o.(*TUFSource)
	default:
		return false
	}
}

func (f *TUFSource) Save() error {
	f.mu.Lock()
	defer f.mu.Unlock()

	return f.saveLocked()
}

func (f *TUFSource) DeleteConfig() error {
	f.mu.Lock()
	defer f.mu.Unlock()

	return os.Remove(filepath.Join(f.dir, configFileName))
}

func (f *TUFSource) Delete() error {
	f.mu.Lock()
	defer f.mu.Unlock()

	return os.RemoveAll(f.dir)
}

func (f *TUFSource) Close() {
	f.closeIdleConnections()
}

func (f *TUFSource) closeIdleConnections() {
	if f.httpClient != nil {
		transport := f.httpClient.Transport
		if transport == nil {
			transport = http.DefaultTransport
		}
		if transport, ok := transport.(*http.Transport); ok {
			transport.CloseIdleConnections()
		}
	}
}

// Actually save the config.
//
// NOTE: It's the responsibility of the caller to hold the mutex before calling
// this function.
func (f *TUFSource) saveLocked() error {
	err := os.MkdirAll(f.dir, os.ModePerm)
	if err != nil {
		return err
	}

	p := filepath.Join(f.dir, configFileName)

	// We want to atomically write the config, so we'll first write it to a
	// temp file, then do an atomic rename to overwrite the target.

	file, err := ioutil.TempFile(f.dir, configFileName)
	if err != nil {
		return err
	}
	defer file.Close()

	// Make sure to clean up the temp file if there's an error.
	defer func() {
		if err != nil {
			os.Remove(file.Name())
		}
	}()

	// Encode the cfg as a pretty printed json.
	encoder := json.NewEncoder(file)
	encoder.SetIndent("", "    ")

	if err = encoder.Encode(f.cfg); err != nil {
		return err
	}

	if err = file.Close(); err != nil {
		return err
	}

	if err = os.Rename(file.Name(), p); err != nil {
		return err
	}

	return nil
}

func needsInit(s tuf.LocalStore) bool {
	meta, err := s.GetMeta()
	if err != nil {
		return true
	}

	_, found := meta["root.json"]
	return !found
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"encoding/json"
	"io/ioutil"
)

type APConfig struct {
	SSID         string
	BeaconPeriod int
	DTIMPeriod   int
	Active       bool
	Channel      uint8
}

func NewEmptyAPConfig() *APConfig {
	return &APConfig{}
}

func NewAPConfig(ssid string) *APConfig {
	return &APConfig{ssid, 100, 1, true, 6}
}

func ReadAPConfigFromFile(path string) (*APConfig, error) {
	cfgBytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, err
	}

	cfg := NewEmptyAPConfig()
	err = json.Unmarshal(cfgBytes, cfg)
	return cfg, err
}

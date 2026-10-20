// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

void dump_partition_table(const void *entries, 
                         uint32_t num_entries, 
                         uint32_t entry_size);

void dump_gpt_header(const void *header);

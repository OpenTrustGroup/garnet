/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <nbl_app_manifest.h>
#include <stddef.h>

/* App UUID:   {bef76d78-d0f1-465d-9a24-83b0f4546c1e} */
#define UUID  \
	{ 0xbef76d78, 0xd0f1, 0x465d, \
	{ 0x9a, 0x24, 0x83, 0xb0, 0xf4, 0x54, 0x6c, 0x1e } }

NBL_APP_MANIFEST(
	UUID,

	/* 16 pages for heap */
	TRUSTY_APP_CONFIG_MIN_HEAP_SIZE(16 * 4096),

	/* 2 pages for stack */
	TRUSTY_APP_CONFIG_MIN_STACK_SIZE(2 * 4096)
);

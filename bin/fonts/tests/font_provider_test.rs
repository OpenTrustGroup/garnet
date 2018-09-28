// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use failure::{format_err, Error, ResultExt};
use fidl_fuchsia_fonts as fonts;
use fuchsia_app::client::{LaunchOptions, Launcher};
use fuchsia_async::Executor;
use fuchsia_zircon as zx;
use fuchsia_zircon::AsHandleRef;

#[derive(Debug, Eq, PartialEq)]
struct FontInfo {
    vmo_koid: zx::Koid,
    buffer_id: u32,
    size: u64,
    index: u32,
}

async fn get_font_info_with_lang(
    font_provider: &fonts::ProviderProxy, name: String, language: Vec<String>,
) -> Result<FontInfo, Error> {
    let font = await!(font_provider.get_font(&mut fonts::Request {
        family: name.clone(),
        weight: 400,
        width: 5,
        slant: fonts::Slant::Upright,
        character: 0,
        language: language,
    }))?;
    let font = *font.ok_or_else(|| format_err!("Received empty response for {}", name))?;

    assert!(font.buffer.size > 0);
    assert!(font.buffer.size <= font.buffer.vmo.get_size()?);

    let vmo_koid = font.buffer.vmo.as_handle_ref().get_koid()?;
    Ok(FontInfo {
        vmo_koid,
        buffer_id: font.buffer_id,
        size: font.buffer.size,
        index: font.font_index,
    })
}

async fn get_font_info(
    font_provider: &fonts::ProviderProxy, name: String,
) -> Result<FontInfo, Error> {
    await!(get_font_info_with_lang(font_provider, name, vec![]))
}

async fn test_basic() -> Result<(), Error> {
    let launcher = Launcher::new().context("Failed to open launcher service")?;
    let app = launcher
        .launch("fonts".to_string(), None)
        .context("Failed to launch fonts::Provider")?;

    let font_provider = app
        .connect_to_service(fonts::ProviderMarker)
        .context("Failed to connect to fonts::Provider")?;

    let default = await!(get_font_info(&font_provider, "".to_string()))
        .context("Failed to load default font")?;
    let roboto = await!(get_font_info(&font_provider, "Roboto".to_string()))
        .context("Failed to load Roboto")?;
    let roboto_slab = await!(get_font_info(&font_provider, "RobotoSlab".to_string()))
        .context("Failed to load RobotoSlab")?;

    // Roboto should be returned by default.
    assert!(default == roboto);

    // RobotoSlab request should return a different font.
    assert!(default.vmo_koid != roboto_slab.vmo_koid);
    assert!(default.buffer_id != roboto_slab.buffer_id);

    Ok(())
}

async fn test_font_collections() -> Result<(), Error> {
    let mut launch_options = LaunchOptions::new();
    launch_options.add_dir_to_namespace(
        "/test_fonts".to_string(),
        std::fs::File::open("/pkgfs/packages/font_provider_tests/0/data/test_fonts")?,
    )?;

    let launcher = Launcher::new().context("Failed to open launcher service")?;
    let app = launcher
        .launch_with_options(
            "fonts".to_string(),
            Some(vec![
                "--font-manifest".to_string(),
                "/test_fonts/manifest.json".to_string(),
            ]),
            launch_options,
        ).context("Failed to launch fonts::Provider")?;

    let font_provider = app
        .connect_to_service(fonts::ProviderMarker)
        .context("Failed to connect to fonts::Provider")?;

    // Request Japanese and Simplified Chinese versions of Noto Sans CJK. Both
    // fonts are part of the same TTC file, so font provider is expected to
    // return the same buffer with different font index values.
    let noto_sans_cjk_ja = await!(get_font_info_with_lang(
        &font_provider,
        "NotoSansCJK".to_string(),
        vec!["ja".to_string()]
    )).context("Failed to load NotoSansCJK font")?;
    let noto_sans_cjk_sc = await!(get_font_info_with_lang(
        &font_provider,
        "NotoSansCJK".to_string(),
        vec!["zh-Hans".to_string()]
    )).context("Failed to load NotoSansCJK font")?;

    assert!(noto_sans_cjk_ja.vmo_koid == noto_sans_cjk_sc.vmo_koid);
    assert!(noto_sans_cjk_ja.index != noto_sans_cjk_sc.index);

    Ok(())
}

async fn run_tests() -> Result<(), Error> {
    await!(test_basic())?;
    await!(test_font_collections())?;

    Ok(())
}

fn main() -> Result<(), Error> {
    let mut executor = Executor::new().context("Error creating executor")?;

    executor.run_singlethreaded(run_tests())
}

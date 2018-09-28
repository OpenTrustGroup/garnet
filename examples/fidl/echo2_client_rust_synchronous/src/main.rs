// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use failure::{Error, ResultExt};
use fidl_fidl_examples_echo::{EchoMarker, EchoSynchronousProxy};
use fuchsia_app::client::Launcher;
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, prelude::*};
use structopt::StructOpt;

fn main() -> Result<(), Error> {
    // Needed only so that creation of async (never-queried) channels succeeds
    // in fuchsia_app
    let _executor = fasync::Executor::new().context("Error creating executor")?;

    #[derive(StructOpt, Debug)]
    #[structopt(name = "echo_client_rust")]
    struct Opt {
        #[structopt(long = "server", help = "URL of echo server",
                    default_value = "fuchsia-pkg://fuchsia.com/echo2_server_rust#meta/echo2_server_rust.cmx")]
        server_url: String,
    }

    // Launch the server and connect to the echo service.
    let Opt { server_url } = Opt::from_args();

    let launcher = Launcher::new().context("Failed to open launcher service")?;
    let app = launcher.launch(server_url, None)
                      .context("Failed to launch echo service")?;

    let (server_end, client_end) = zx::Channel::create()?;
    app.pass_to_service(EchoMarker, server_end)
       .context("Failed to connect to echo service")?;

    let mut proxy = EchoSynchronousProxy::new(client_end);
    let res = proxy.echo_string(Some("hello world!"), 1.second().after_now())?;
    println!("response: {:?}", res);
    Ok(())
}

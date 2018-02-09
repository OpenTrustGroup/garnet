// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app;
extern crate futures;
extern crate garnet_public_lib_network_fidl;
extern crate tokio_core;
extern crate tokio_fuchsia;
extern crate tokio_io;

use failure::Error;
use fidl::FidlService;
use futures::{Future, IntoFuture};
use garnet_public_lib_network_fidl as netsvc;
use std::io;
use tokio_core::reactor;
use tokio_io::io::{AllowStdIo, copy};

fn print_headers(resp: &netsvc::URLResponse) {
    println!(">>> Headers <<<");
    if let Some(ref status) = resp.status_line {
        println!("  {}", status);
    }
    if let Some(ref hdrs) = resp.headers {
        for hdr in hdrs {
            println!("  {}={}", hdr.name, hdr.value);
        }
    }
}

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

/// Connects to the network service, sends a url request, and prints the response.
fn main_res() -> Result<(), Error> {
    let url = match std::env::args().nth(1) {
        Some(url) => {
            if url.find("://").is_none() {
                ["http://", &url].concat()
            } else {
                url
            }
        }
        None => {
            println!("usage: {} <url>", std::env::args().nth(0).unwrap());
            return Ok(());
        }
    };

    // Set up tokio reactor
    let mut core = reactor::Core::new()?;
    let handle = core.handle();

    // Connect to the network service
    let net = fuchsia_app::client::connect_to_service::<netsvc::NetworkService::Service>(&handle)?;

    // Create a URLLoader instance
    let (loader_proxy, loader_server) = netsvc::URLLoader::Service::new_pair(&handle)?;
    net.create_url_loader(loader_server)?;

    // Send the URLRequest to fetch the webpage
    let req = netsvc::URLRequest {
        url: url,
        method: String::from("GET"),
        headers: None,
        body: None,
        response_body_buffer_size: 0,
        auto_follow_redirects: true,
        cache_mode: netsvc::URLRequestCacheMode::Default,
        response_body_mode: netsvc::URLRequestResponseBodyMode::Stream,
    };

    let fut = loader_proxy.start(req).map_err(Error::from).and_then(|resp| {
        if let Some(e) = resp.error {
            let code = e.code;
            println!("Got error: {} ({})",
                    code,
                    e.description.unwrap_or("".into()));
            return None;
        }
        print_headers(&resp);

        match resp.body.map(|x| *x) {
            Some(netsvc::URLBody::Stream(s)) => {
                Some(tokio_fuchsia::Socket::from_socket(s, &handle)
                        .map_err(Error::from)
                        .into_future())
            }
            Some(netsvc::URLBody::Buffer(_)) |
            Some(netsvc::URLBody::SizedBuffer(_)) |
            None =>  None,
        }
    }).and_then(|socket_opt| {
        socket_opt.map(|socket| {
            // stdout is blocking, but we'll pretend it's okay
            println!(">>> Body <<<");

            // Copy the bytes from the socket to stdout
            copy(socket, AllowStdIo::new(io::stdout()))
                .map(|_| ())
                .or_else(|e| if e.kind() == io::ErrorKind::ConnectionAborted {
                    println!("\n>>> EOF <<<");
                    Ok(())
                } else {
                    Err(e)
                })
                .map_err(Error::from)
        })
    }).map(|_| ());

    //// Run the future to completion
    core.run(fut)
}

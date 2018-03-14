// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateEndpoint = `
{{- /* . (dot) refers to a string which is the endpoint's name */ -}}
{{- define "GenerateEndpoint" -}}
{{- $endpoint := .Name -}}
{{- $interface := .Interface -}}

#[derive(Debug)]
pub struct {{$endpoint}}(::zircon::Channel);

impl ::zircon::HandleRef for {{$endpoint}} {
    #[inline]
    fn as_handle_ref(&self) -> ::zircon::HandleRef {
        self.0.get_ref()
    }
}

impl From<{{$endpoint}}> for ::zircon::Handle {
    #[inline]
    fn from(x: $endpoint) -> ::zircon::Handle {
        x.0.into()
    }
}

impl From<::zircon::Handle> for {{$endpoint}} {
    #[inline]
    fn from(handle: ::zircon::Handle) -> Self {
        {{$endpoint}}(handle.into())
    }
}

impl ::zircon::HandleBased for {{$endpoint}}

impl_codable_handle!({{$endpoint}});

{{- end -}}
`

const GenerateInterface = `
{{- /* . (dot) refers to the Go type |rustgen.InterfaceTemplate| */ -}}
{{- define "GenerateInterface" -}}
{{- $interface := . -}}
// --- {{$interface.Name}} ---

pub mod {{$interface.Name}} {

use fidl::{self, DecodeBuf, DecodablePtr, EncodeBuf, EncodablePtr, FidlService, Stub};
use futures::{Async, Poll, Future, FutureExt, future, task};
use zircon;
use super::*;

pub trait Server: Send {
    {{range $message := $interface.Messages}}
        type {{$message.TyName}}: fidl::ServerFuture<
            {{- if ne $message.ResponseStruct.Name "" }}
                {{template "GenerateResponseType" $message.ResponseStruct}}
            {{- else }}
                ()
            {{end}}
        >;
        fn {{$message.Name}}(&mut self
            {{- range $index, $field := $message.RequestStruct.Fields -}}
                , {{$field.Name}}: {{$field.Type}}
            {{- end -}}
        ) -> Self::{{$message.TyName}};
    {{end}}
}

pub trait Client {
    {{range $message := $interface.Messages}}
        fn {{$message.Name}}(&self
            {{- range $index, $field := $message.RequestStruct.Fields -}}
                , {{$field.Name}}: {{$field.Type}}
            {{- end -}}
        )
        {{- if ne $message.ResponseStruct.Name "" }}
            -> fidl::BoxFuture<{{template "GenerateResponseType" $message.ResponseStruct}}>
        {{- else}}
            -> fidl::Result<()>
        {{end}};
    {{end}}
}

/// A struct which implements Server by delegating methods to its fields.
///
/// This can be used to implement the Server trait without having named return
/// types from the methods.
#[derive(Debug, Copy, Clone)]
pub struct Impl<State: Send,
	 {{range $message := $interface.Messages}}
		{{$message.TyName}}: Send + FnMut(&mut State
				{{- range $index, $field := $message.RequestStruct.Fields -}}
                , {{$field.Type}}
            {{- end -}}
			 ) -> {{$message.TyName}}Fut,
		{{$message.TyName}}Fut: Send + fidl::ServerFuture<
		  {{- if ne $message.ResponseStruct.Name "" }}
				{{template "GenerateResponseType" $message.ResponseStruct}}
		  {{- else }}
				()
		  {{end}}
		>,
	{{end}}
> {
    pub state: State,
	 {{range $message := $interface.Messages}}
		pub {{$message.Name}}: {{$message.TyName}},
	 {{end}}
}

impl<State: Send,
	 {{range $message := $interface.Messages}}
		{{$message.TyName}}: Send + FnMut(&mut State
		  {{- range $index, $field := $message.RequestStruct.Fields -}}
				, {{$field.Type}}
		  {{- end -}}
		) -> {{$message.TyName}}Fut,
		{{$message.TyName}}Fut: fidl::ServerFuture<
		  {{- if ne $message.ResponseStruct.Name "" }}
				{{template "GenerateResponseType" $message.ResponseStruct}}
		  {{- else }}
				()
		  {{end}}
		>,
	{{end}}
> Server for Impl<State,
	{{range $message := $interface.Messages}}
		{{$message.TyName}},
		{{$message.TyName}}Fut,
	{{end}}
>
{
	 {{range $message := $interface.Messages}}
        type {{$message.TyName}} = {{$message.TyName}}Fut;
		  fn {{$message.Name}}(&mut self
            {{- range $index, $field := $message.RequestStruct.Fields -}}
                , {{$field.Name}}: {{$field.Type}}
            {{- end -}}
        ) -> Self::{{$message.TyName}} {
			 (self.{{$message.Name}})(&mut self.state
            {{- range $index, $field := $message.RequestStruct.Fields -}}
                , {{$field.Name}}
            {{- end -}}
			 )
		  }
    {{end}}
}

#[derive(Debug)]
pub struct Dispatcher<T: Server>(pub T);

#[derive(Debug)]
pub enum DispatchResponseFuture <
    {{range $message := $interface.Messages}}
        {{if ne $message.ResponseStruct.Name ""}}
            {{- $message.TyName}},
        {{end}}
    {{end}}
> {
    {{range $message := $interface.Messages}}
        {{if ne $message.ResponseStruct.Name ""}}
            {{- $message.TyName}}({{- $message.TyName}}),
        {{end}}
    {{end}}
    FidlError(fidl::ErrorOrClose),
}

impl<
    {{range $message := $interface.Messages}}
        {{if ne $message.ResponseStruct.Name ""}}
            {{- $message.TyName}}: fidl::ServerFuture<
                {{template "GenerateResponseType" $message.ResponseStruct}}
            >,
        {{end}}
    {{end}}
>
Future for DispatchResponseFuture<
    {{range $message := $interface.Messages}}
        {{if ne $message.ResponseStruct.Name ""}}
            {{- $message.TyName}},
        {{end}}
    {{end}}
>
{
    type Item = EncodeBuf;
    type Error = fidl::ErrorOrClose;
	 fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        match *self {
            {{range $message := $interface.Messages}}
                {{if ne $message.ResponseStruct.Name ""}}
                    DispatchResponseFuture::{{- $message.TyName}}(ref mut inner) => {
                        let res = try_ready!(inner.poll(cx));
                        // Split apart response tuple into multiple response fields
                        let ({{- if ne 1 (len $message.ResponseStruct.Fields) -}} ( {{- end -}}
                            {{- range $index, $field := $message.ResponseStruct.Fields -}}
                                {{- if $index}}, {{end -}}
                                {{- $field.Name -}}
                            {{- end -}}
                            {{- if ne 1 (len $message.ResponseStruct.Fields) -}} ) {{- end -}}
                            ,)= (res,);

                        let response = {{$message.ResponseStruct.Name}} {
                            {{range $field := $message.ResponseStruct.Fields}}
                                {{$field.Name}}: {{$field.Name}},
                            {{end}}
                        };
                        let mut encode_buf = ::fidl::EncodeBuf::new_response({{$message.MessageOrdinal}});
                        ::fidl::EncodablePtr::encode_obj(response, &mut encode_buf);
                        Ok(Async::Ready(encode_buf))
                    }
                {{end}}
            {{end}}
            DispatchResponseFuture::FidlError(ref mut err) =>
                Err(::std::mem::replace(err, fidl::Error::PollAfterCompletion.into())),
        }
    }
}

#[derive(Debug)]
pub enum DispatchFuture <
    {{range $message := $interface.Messages}}
        {{if eq $message.ResponseStruct.Name ""}}
            {{- $message.TyName}},
        {{end}}
    {{end}}
> {
    {{range $message := $interface.Messages}}
        {{if eq $message.ResponseStruct.Name ""}}
            {{- $message.TyName}}({{- $message.TyName}}),
        {{end}}
    {{end}}
    FidlError(fidl::ErrorOrClose),
}

impl<
    {{range $message := $interface.Messages}}
        {{if eq $message.ResponseStruct.Name ""}}
            {{- $message.TyName}}: fidl::ServerFuture<()>,
        {{end}}
    {{end}}
> Future for DispatchFuture<
    {{range $message := $interface.Messages}}
        {{if eq $message.ResponseStruct.Name ""}}
            {{- $message.TyName}},
        {{end}}
    {{end}}
> {
    type Item = ();
    type Error = fidl::ErrorOrClose;
	 fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        match *self {
            {{range $message := $interface.Messages}}
                {{if eq $message.ResponseStruct.Name ""}}
                    DispatchFuture::{{- $message.TyName}}(ref mut inner) => {
                        inner.poll(cx).map_err(fidl::CloseChannel::into)
                    }
                {{end}}
            {{end}}
            DispatchFuture::FidlError(ref mut err) =>
                Err(::std::mem::replace(err, fidl::Error::PollAfterCompletion.into())),
        }
    }
}

impl<T: Server> Stub for Dispatcher<T> {
    type Service = Service;

    type DispatchResponseFuture = DispatchResponseFuture<
        {{range $message := $interface.Messages}}
            {{if ne $message.ResponseStruct.Name ""}}
                T::{{- $message.TyName}},
            {{end}}
        {{end}}
    >;

    type DispatchFuture = DispatchFuture<
        {{range $message := $interface.Messages}}
            {{if eq $message.ResponseStruct.Name ""}}
                T::{{- $message.TyName}},
            {{end}}
        {{end}}
    >;

    #[inline]
    fn dispatch_with_response(&mut self, request: &mut DecodeBuf) -> Self::DispatchResponseFuture {
        let name: u32 = match fidl::Decodable::decode(request, 0, 8) {
            Ok(x) => x,
            Err(e) => return DispatchResponseFuture::FidlError(e.into()),
        };

        match name {
            {{range $message := $interface.Messages}}
                {{if ne $message.ResponseStruct.Name ""}}
                    {{$message.MessageOrdinal}} => {
                        let request: {{$message.RequestStruct.Name}} = match fidl::DecodablePtr::decode_obj(request, 24) {
                            Ok(request) => request,
                            Err(e) => return DispatchResponseFuture::FidlError(e.into()),
                        };
                        DispatchResponseFuture::{{- $message.TyName}}(
                            Server::{{$message.Name}}(&mut self.0
                                {{- range $index, $field := $message.RequestStruct.Fields -}}
                                    , request.{{- $field.Name -}}
                                {{- end -}}
                            )
                        )
                    }
                {{end}}
            {{end}}
            ordinal => DispatchResponseFuture::FidlError(fidl::Error::UnknownOrdinal {
              ordinal,
              service_name: NAME
            }.into()),
        }
    }

    #[inline]
    fn dispatch(&mut self, request: &mut ::fidl::DecodeBuf) -> Self::DispatchFuture {
        let name: u32 = match fidl::Decodable::decode(request, 0, 8) {
            Ok(x) => x,
            Err(e) => return DispatchFuture::FidlError(e.into()),
        };
        match name {
            {{range $message := $interface.Messages}}
                {{if eq $message.ResponseStruct.Name ""}}
                    {{$message.MessageOrdinal}} => {
                        let request: {{$message.RequestStruct.Name}} = match fidl::DecodablePtr::decode_obj(request, 16) {
                            Ok(request) => request,
                            Err(e) => return DispatchFuture::FidlError(e.into()),
                        };
                        DispatchFuture::{{- $message.TyName}}(
                            Server::{{$message.Name}}(&mut self.0
                                {{- range $index, $field := $message.RequestStruct.Fields -}}
                                    , request.{{- $field.Name -}}
                                {{- end -}}
                            )
                        )
                    }
                {{end}}
            {{end}}
            ordinal => DispatchFuture::FidlError(::fidl::Error::UnknownOrdinal {
              ordinal,
              service_name: NAME,
            }.into())
        }
    }
}

#[derive(Debug)]
pub struct Proxy(fidl::Client);

impl Client for Proxy {
    {{- range $message := $interface.Messages}}
        #[inline]
        fn {{$message.Name}}(&self
            {{- range $field := $message.RequestStruct.Fields}}
                , {{$field.Name}}: {{$field.Type}}
            {{- end -}} )
            {{- if ne $message.ResponseStruct.Name "" }}
                -> fidl::BoxFuture<{{template "GenerateResponseType" $message.ResponseStruct}}>
            {{- else}}
                -> fidl::Result<()>
            {{end}}
        {
            let request = {{$message.RequestStruct.Name}} {
                {{range $field := $message.RequestStruct.Fields}}
                    {{$field.Name}}: {{$field.Name}},
                {{end}}
            };
            let mut encode_buf =
                ::fidl::EncodeBuf::
                new_request{{if ne $message.ResponseStruct.Name "" -}}_expecting_response{{- end}}
                ({{$message.MessageOrdinal}});

            ::fidl::EncodablePtr::encode_obj(request, &mut encode_buf);
            {{if eq $message.ResponseStruct.Name ""}}
                self.0.send_msg(&mut encode_buf)
            {{else}}
                Box::new(self.0.send_msg_expect_response(&mut encode_buf).and_then(|mut decode_buf| {
                    let r: {{$message.ResponseStruct.Name}} = try!(::fidl::DecodablePtr::decode_obj(&mut decode_buf, 24));
                    Ok(
                        {{- if ne 1 (len $message.ResponseStruct.Fields) -}} ( {{- end -}}
                        {{- range $index, $field := $message.ResponseStruct.Fields -}}
                            {{- if $index}},{{end -}}
                            r.{{- $field.Name -}}
                        {{- end -}}
                        {{- if ne 1 (len $message.ResponseStruct.Fields) -}}
                    ) {{- end -}}
                )
                }))
            {{end}}
        }
    {{end}}
}

// Duplicate client methods as inherent impls so users don't have to import the trait
impl Proxy {
    {{- range $message := $interface.Messages}}
        #[inline]
        pub fn {{$message.Name}}(&self
            {{- range $field := $message.RequestStruct.Fields}}
                , {{$field.Name}}: {{$field.Type}}
            {{- end -}} )
            {{- if ne $message.ResponseStruct.Name "" }}
                -> fidl::BoxFuture<{{template "GenerateResponseType" $message.ResponseStruct}}>
            {{- else}}
                -> fidl::Result<()>
            {{- end}}
        {
            <Proxy as Client>::{{$message.Name}}(self
                {{- range $field := $message.RequestStruct.Fields}}
                    , {{$field.Name}}
                {{- end -}}
            )
        }
    {{end}}
}

#[derive(Debug, Default, Eq, PartialEq, Copy, Clone, Hash)]
pub struct Service;
impl fidl::FidlService for Service {
    type Proxy = Proxy;

    #[inline]
    fn new_proxy(client_end: fidl::ClientEnd<Self>) -> Result<Self::Proxy, fidl::Error> {
        let channel = ::fuchsia_async::Channel::from_channel(client_end.into_channel())
                        .map_err(fidl::Error::AsyncChannel)?;
        Ok(Proxy(fidl::Client::new(channel)))
    }

    #[inline]
    fn new_pair() -> Result<(Self::Proxy, fidl::ServerEnd<Self>), fidl::Error> {
        let (s1, s2) = zircon::Channel::create().map_err(fidl::Error::ChannelPairCreate)?;
        let client_end = fidl::ClientEnd::new(s1);
        let server_end = fidl::ServerEnd::new(s2);
        Ok((Self::new_proxy(client_end)?, server_end))
    }

    const NAME: &'static str = NAME;
    const VERSION: u32 = VERSION;
}

// Duplicate SERVICE trait members at the module level

#[inline]
pub fn new_proxy(client_end: fidl::ClientEnd<Service>) -> Result<Proxy, fidl::Error> {
    Service::new_proxy(client_end)
}

#[inline]
pub fn new_pair() -> Result<(Proxy, fidl::ServerEnd<Service>), fidl::Error> {
    Service::new_pair()
}

pub const NAME: &'static str = "{{$interface.ServiceName}}";
pub const VERSION: u32 = {{$interface.Version}};

} // End of mod

// Enums
{{range $enum := $interface.Enums -}}
{{template "GenerateEnum" $enum}}
{{end}}

// Constants
{{range $const := $interface.Constants -}}
pub const {{$const.Name}}: {{$const.Type}} = {{$const.Value}};
{{end}}

{{range $message := $interface.Messages -}}
/// Message: {{$message.Name}}
{{template "GenerateStruct" $message.RequestStruct}}

{{if ne $message.ResponseStruct.Name "" -}}
{{template "GenerateStruct" $message.ResponseStruct}}

{{end -}}
{{- end -}}
{{- end -}}

{{- /* . (dot) refers to the Go type |rustgen.StructTemplate| */ -}}
{{- define "GenerateResponseType" -}}
{{- $struct := . -}}
{{- if ne 1 (len $struct.Fields) -}} ( {{- end -}}
{{- range $index, $field := $struct.Fields -}}
{{- if $index}}, {{end -}}
{{- $field.Type -}}
{{- end -}}
{{- if ne 1 (len $struct.Fields) -}} ) {{- end -}}
{{- end -}}
`

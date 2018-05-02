// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.Decl }} {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "RequestMethodSignature" -}}
  {{- if .HasResponse -}}
{{ .Name }}({{ template "Params" .Request }}{{ if .Request }}, {{ end }}void callback({{ template "Params" .Response }}))
  {{- else -}}
{{ .Name }}({{ template "Params" .Request }})
  {{- end -}}
{{ end -}}

{{- define "ResponseMethodSignature" -}}
{{ .Name }}({{ template "Params" .Response }})
{{ end -}}

{{- define "InterfaceDeclaration" -}}
abstract class {{ .Name }} {
  static const String $serviceName = {{ .ServiceName }};

{{- range .Methods }}
  {{- if .HasRequest }}
  void {{ template "RequestMethodSignature" . }};
  {{- end }}
{{- end }}
}

{{ range .Methods }}
// {{ .Name }}: {{ if .HasRequest }}({{ template "Params" .Request }}){{ end }}{{ if .HasResponse }} -> ({{ template "Params" .Response }}){{ end }}
const int {{ .OrdinalName }} = {{ .Ordinal }};
const $fidl.MethodType {{ .TypeSymbol }} = {{ .TypeExpr }};
{{- end }}

{{ range .Methods }}
  {{- if not .HasRequest }}
    {{- if .HasResponse }}
typedef void {{ .CallbackType }}({{ template "Params" .Response }});
    {{- end }}
  {{- end }}
{{- end }}

class {{ .ProxyName }} extends $fidl.Proxy<{{ .Name }}>
    implements {{ .Name }} {

  {{ .ProxyName }}() : super(new $fidl.ProxyController<{{ .Name }}>($serviceName: {{ .ServiceName }})) {
    ctrl.onResponse = _handleResponse;
  }

  void _handleEvent($fidl.Message $message) {
    final $fidl.Decoder $decoder = new $fidl.Decoder($message);
    switch ($message.ordinal) {
{{- range .Methods }}
{{- if not .HasRequest }}
  {{- if .HasResponse }}
      case {{ .OrdinalName }}:
        final Function $callback = {{ .Name }};
        if ($callback == null) {
          $message.closeHandles();
          return;
        }
        final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response;
        $decoder.claimMemory({{ .ResponseSize }});
        $callback(
    {{- range $index, $response := .Response }}
          $types[{{ $index }}].decode($decoder, 0),
    {{- end }}
        );
        break;
  {{- end }}
{{- end }}
{{- end }}
      default:
        ctrl.proxyError('Unexpected message ordinal: ${$message.ordinal}');
        ctrl.close();
        break;
    }
  }

  void _handleResponse($fidl.Message $message) {
    final int $txid = $message.txid;
    if ($txid == 0) {
      _handleEvent($message);
      return;
    }
    final Function $callback = ctrl.getCallback($txid);
    if ($callback == null) {
      $message.closeHandles();
      return;
    }
    final $fidl.Decoder $decoder = new $fidl.Decoder($message);
    switch ($message.ordinal) {
{{- range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}
      case {{ .OrdinalName }}:
        final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response;
        $decoder.claimMemory({{ .ResponseSize }});
        $callback(
      {{- range $index, $response := .Response }}
          $types[{{ $index }}].decode($decoder, 0),
      {{- end }}
        );
        break;
    {{- end }}
  {{- end }}
{{- end }}
      default:
        ctrl.proxyError('Unexpected message ordinal: ${$message.ordinal}');
        ctrl.close();
        break;
    }
  }

{{- range .Methods }}
  {{- if .HasRequest }}
  @override
  void {{ template "RequestMethodSignature" . }} {
    if (!ctrl.isBound) {
      ctrl.proxyError('The proxy is closed.');
      return;
    }

    final $fidl.Encoder $encoder = new $fidl.Encoder({{ .OrdinalName }});
    {{- if .Request }}
    $encoder.alloc({{ .RequestSize }} - $fidl.kMessageHeaderSize);
    final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.request;
    {{- end }}
    {{- range $index, $request := .Request }}
    $types[{{ $index }}].encode($encoder, {{ .Name }}, 0);
    {{- end }}
    {{- if .HasResponse }}
    Function $zonedCallback;
    if ((callback == null) || identical(Zone.current, Zone.root)) {
      $zonedCallback = callback;
    } else {
      Zone $z = Zone.current;
      {{- if .Response }}
      $zonedCallback = (({{ template "Params" .Response }}) {
        $z.bindCallback(() {
          callback(
        {{- range .Response -}}
            {{ .Name }},
        {{- end -}}
          );
        })();
      });
      {{- else }}
      $zonedCallback = $z.bindCallback(callback);
      {{- end }}
    }
    ctrl.sendMessageWithResponse($encoder.message, $zonedCallback);
    {{- else }}
    ctrl.sendMessage($encoder.message);
    {{- end }}
  }
  {{- else if .HasResponse }}
  {{ .CallbackType }} {{ .Name }};
  {{- end }}
{{- end }}
}

{{- if .HasEvents }}

class {{ .EventsName }} {
  $fidl.Binding<{{ .Name }}> _binding;

{{- range .Methods }}
  {{- if not .HasRequest }}
    {{- if .HasResponse }}
  void {{ template "ResponseMethodSignature" . }} {
    final $fidl.Encoder $encoder = new $fidl.Encoder({{ .OrdinalName }});
      {{- if .Response }}
    $encoder.alloc({{ .ResponseSize }} - $fidl.kMessageHeaderSize);
    final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response;
      {{- end }}
      {{- range $index, $response := .Response }}
    $types[{{ $index }}].encode($encoder, {{ .Name }}, 0);
      {{- end }}
    _binding.sendMessage($encoder.message);
  }
    {{- end }}
  {{- end }}
{{- end }}
}

{{- end }}

class {{ .BindingName }} extends $fidl.Binding<{{ .Name }}> {
{{- if .HasEvents }}
  {{ .BindingName }}() {
    events._binding = this;
  }

  final {{ .EventsName }} events = new {{ .EventsName }}();
{{- end }}

{{ range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}
  Function _{{ .Name }}Responder($fidl.MessageSink $respond, int $txid) {
    return ({{ template "Params" .Response }}) {
      final $fidl.Encoder $encoder = new $fidl.Encoder({{ .OrdinalName }});
      {{- if .Response }}
      $encoder.alloc({{ .ResponseSize }} - $fidl.kMessageHeaderSize);
      final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response;
      {{- end }}
      {{- range $index, $response := .Response }}
      $types[{{ $index }}].encode($encoder, {{ .Name }}, 0);
      {{- end }}
      $fidl.Message $message = $encoder.message;
      $message.txid = $txid;
      $respond($message);
    };
  }
    {{- end }}
  {{- end }}
{{- end }}

  @override
  void handleMessage($fidl.Message $message, $fidl.MessageSink $respond) {
    final $fidl.Decoder $decoder = new $fidl.Decoder($message);
    switch ($message.ordinal) {
{{- range .Methods }}
  {{- if .HasRequest }}
      case {{ .OrdinalName }}:
        final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.request;
        $decoder.claimMemory({{ .RequestSize }});
        impl.{{ .Name }}(
    {{- range $index, $request := .Request }}
          $types[{{ $index }}].decode($decoder, 0),
    {{- end }}
    {{- if .HasResponse }}
          _{{ .Name }}Responder($respond, $message.txid),
    {{- end }}
        );
        break;
  {{- end }}
{{- end }}
      default:
        throw new $fidl.FidlError('Unexpected message name');
    }
  }
}

{{ end }}
`

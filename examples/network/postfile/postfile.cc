// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <cstdio>

#include <fuchsia/cpp/network.h>
#include <lib/async/default.h>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/fsl/socket/files.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/file_descriptor.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/unique_fd.h"

namespace examples {

class ResponsePrinter {
 public:
  void Run(network::URLResponse response) const {
    if (response.error) {
      printf("Got error: %d (%s)\n", response.error->code,
             response.error->description->c_str());
    } else {
      PrintResponse(response);
      PrintResponseBody(std::move(response.body->stream()));
    }

    fsl::MessageLoop::GetCurrent()->QuitNow();  // All done!
  }

  void PrintResponse(const network::URLResponse& response) const {
    printf(">>> Headers <<< \n");
    printf("  %s\n", response.status_line.get().c_str());
    if (response.headers) {
      for (size_t i = 0; i < response.headers->size(); ++i)
        printf("  %s=%s\n", response.headers->at(i).name->data(),
               response.headers->at(i).value->data());
    }
  }

  void PrintResponseBody(zx::socket body) const {
    // Read response body in blocking fashion.
    printf(">>> Body <<<\n");

    for (;;) {
      char buf[512];
      size_t num_bytes = sizeof(buf);
      zx_status_t result = body.read(0u, buf, num_bytes, &num_bytes);

      if (result == ZX_ERR_SHOULD_WAIT) {
        body.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                      zx::time::infinite(), nullptr);
      } else if (result == ZX_OK) {
        if (fwrite(buf, num_bytes, 1, stdout) != 1) {
          printf("\nUnexpected error writing to file\n");
          break;
        }
      } else {
        break;
      }
    }

    printf("\n>>> EOF <<<\n");
  }
};

class PostFileApp {
 public:
  PostFileApp()
      : context_(component::ApplicationContext::CreateFromStartupInfo()) {
    network_service_ =
        context_->ConnectToEnvironmentService<network::NetworkService>();
  }

  bool Start(const std::vector<std::string>& args) {
    if (args.size() < 3) {
      printf("usage: %s url upload_file\n", args[0].c_str());
      return false;
    }
    std::string url(args[1]);
    std::string upload_file(args[2]);
    printf("Posting %s to %s\n", upload_file.c_str(), url.c_str());

    std::string boundary = "XXXX";  // TODO: make an option to change this

    fxl::UniqueFD fd(open(upload_file.c_str(), O_RDONLY));
    if (!fd.is_valid()) {
      printf("cannot open %s\n", upload_file.c_str());
      return false;
    }

    network::URLRequest request;
    request.url = url;
    request.method = "POST";
    request.auto_follow_redirects = true;

    network::HttpHeader header;
    header.name = "Content-Type";
    header.value = "multipart/form-data; boundary=" + boundary;
    request.headers.push_back(std::move(header));

    zx::socket consumer;
    zx::socket producer;
    zx_status_t status = zx::socket::create(0u, &producer, &consumer);
    if (status != ZX_OK) {
      printf("cannot create socket\n");
      return false;
    }

    request.body = network::URLBody::New();
    request.body->set_stream(std::move(consumer));

    async_t* async = async_get_default();
    fsl::CopyFromFileDescriptor(std::move(fd), std::move(producer), async,
                                [](bool result, fxl::UniqueFD fd) {
                                  if (!result) {
                                    printf("file read error\n");
                                    fsl::MessageLoop::GetCurrent()->QuitNow();
                                  }
                                });

    network_service_->CreateURLLoader(url_loader_.NewRequest());

    url_loader_->Start(std::move(request),
                       [this](network::URLResponse response) {
                         ResponsePrinter printer;
                         printer.Run(std::move(response));
                       });
    return true;
  }

 private:
  std::unique_ptr<component::ApplicationContext> context_;
  network::NetworkServicePtr network_service_;
  network::URLLoaderPtr url_loader_;
};

}  // namespace examples

int main(int argc, const char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  fsl::MessageLoop loop;

  examples::PostFileApp postfile_app;
  if (postfile_app.Start(args))
    loop.Run();

  return 0;
}

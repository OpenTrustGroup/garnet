// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <ostream>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "lib/fidl/compiler/interfaces/tests/sample_service.fidl.h"

namespace fidl {

template <>
struct TypeConverter<int32_t, sample::BarPtr> {
  static int32_t Convert(const sample::BarPtr& bar) {
    return static_cast<int32_t>(bar->alpha) << 16 |
           static_cast<int32_t>(bar->beta) << 8 |
           static_cast<int32_t>(bar->gamma);
  }
};

}  // namespace fidl

namespace sample {
namespace {

// Set this variable to true to print the message in hex.
bool g_dump_message_as_hex = false;

// Set this variable to true to print the message in human readable form.
bool g_dump_message_as_text = false;

// Make a sample |Foo|.
FooPtr MakeFoo() {
  fidl::String name("foopy");

  BarPtr bar(Bar::New());
  bar->alpha = 20;
  bar->beta = 40;
  bar->gamma = 60;
  bar->type = Bar::Type::VERTICAL;

  auto extra_bars = fidl::Array<BarPtr>::New(3);
  for (size_t i = 0; i < extra_bars.size(); ++i) {
    Bar::Type type = i % 2 == 0 ? Bar::Type::VERTICAL : Bar::Type::HORIZONTAL;
    BarPtr bar(Bar::New());
    uint8_t base = static_cast<uint8_t>(i * 100);
    bar->alpha = base;
    bar->beta = base + 20;
    bar->gamma = base + 40;
    bar->type = type;
    extra_bars[i] = std::move(bar);
  }

  auto data = fidl::Array<uint8_t>::New(10);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = static_cast<uint8_t>(data.size() - i);

  auto array_of_array_of_bools = fidl::Array<fidl::Array<bool>>::New(2);
  for (size_t i = 0; i < 2; ++i) {
    auto array_of_bools = fidl::Array<bool>::New(2);
    for (size_t j = 0; j < 2; ++j)
      array_of_bools[j] = j;
    array_of_array_of_bools[i] = std::move(array_of_bools);
  }

  zx::channel handle0, handle1;
  zx::channel::create(0, &handle0, &handle1);
  FooPtr foo(Foo::New());
  foo->name = name;
  foo->x = 1;
  foo->y = 2;
  foo->a = false;
  foo->b = true;
  foo->c = false;
  foo->bar = std::move(bar);
  foo->extra_bars = std::move(extra_bars);
  foo->data = std::move(data);
  foo->source = std::move(handle1);
  foo->array_of_array_of_bools = std::move(array_of_array_of_bools);

  return foo;
}

// Check that the given |Foo| is identical to the one made by |MakeFoo()|.
void CheckFoo(const Foo& foo) {
  const std::string kName("foopy");
  ASSERT_FALSE(foo.name.is_null());
  EXPECT_EQ(kName.size(), foo.name.size());
  for (size_t i = 0; i < std::min(kName.size(), foo.name.size()); i++) {
    // Test both |operator[]| and |at|.
    EXPECT_EQ(kName[i], foo.name.at(i)) << i;
    EXPECT_EQ(kName[i], foo.name[i]) << i;
  }
  EXPECT_EQ(kName, foo.name.get());

  EXPECT_EQ(1, foo.x);
  EXPECT_EQ(2, foo.y);
  EXPECT_FALSE(foo.a);
  EXPECT_TRUE(foo.b);
  EXPECT_FALSE(foo.c);

  EXPECT_EQ(20, foo.bar->alpha);
  EXPECT_EQ(40, foo.bar->beta);
  EXPECT_EQ(60, foo.bar->gamma);
  EXPECT_EQ(Bar::Type::VERTICAL, foo.bar->type);

  EXPECT_EQ(3u, foo.extra_bars.size());
  for (size_t i = 0; i < foo.extra_bars.size(); i++) {
    uint8_t base = static_cast<uint8_t>(i * 100);
    Bar::Type type = i % 2 == 0 ? Bar::Type::VERTICAL : Bar::Type::HORIZONTAL;
    EXPECT_EQ(base, foo.extra_bars[i]->alpha) << i;
    EXPECT_EQ(base + 20, foo.extra_bars[i]->beta) << i;
    EXPECT_EQ(base + 40, foo.extra_bars[i]->gamma) << i;
    EXPECT_EQ(type, foo.extra_bars[i]->type) << i;
  }

  EXPECT_EQ(10u, foo.data.size());
  for (size_t i = 0; i < foo.data.size(); ++i) {
    EXPECT_EQ(static_cast<uint8_t>(foo.data.size() - i), foo.data[i]) << i;
  }

  EXPECT_EQ(2u, foo.array_of_array_of_bools.size());
  for (size_t i = 0; i < foo.array_of_array_of_bools.size(); ++i) {
    EXPECT_EQ(2u, foo.array_of_array_of_bools[i].size());
    for (size_t j = 0; j < foo.array_of_array_of_bools[i].size(); ++j) {
      EXPECT_EQ(bool(j), foo.array_of_array_of_bools[i][j]);
    }
  }
}

void PrintSpacer(int depth) {
  for (int i = 0; i < depth; ++i)
    std::cout << "   ";
}

void Print(int depth, const char* name, bool value) {
  PrintSpacer(depth);
  std::cout << name << ": " << (value ? "true" : "false") << std::endl;
}

void Print(int depth, const char* name, int32_t value) {
  PrintSpacer(depth);
  std::cout << name << ": " << value << std::endl;
}

void Print(int depth, const char* name, uint8_t value) {
  PrintSpacer(depth);
  std::cout << name << ": " << uint32_t(value) << std::endl;
}

template <typename H>
void Print(int depth,
           const char* name,
           const zx::object<H>& value) {
  PrintSpacer(depth);
  std::cout << name << ": 0x" << std::hex << value.get() << std::endl;
}

void Print(int depth, const char* name, const fidl::String& str) {
  PrintSpacer(depth);
  std::cout << name << ": \"" << str.get() << "\"" << std::endl;
}

void Print(int depth, const char* name, const BarPtr& bar) {
  PrintSpacer(depth);
  std::cout << name << ":" << std::endl;
  if (!bar.is_null()) {
    ++depth;
    Print(depth, "alpha", bar->alpha);
    Print(depth, "beta", bar->beta);
    Print(depth, "gamma", bar->gamma);
    Print(depth, "packed", bar.To<int32_t>());
    --depth;
  }
}

template <typename T>
void Print(int depth, const char* name, const fidl::Array<T>& array) {
  PrintSpacer(depth);
  std::cout << name << ":" << std::endl;
  if (!array.is_null()) {
    ++depth;
    for (size_t i = 0; i < array.size(); ++i) {
      std::stringstream buf;
      buf << i;
      Print(depth, buf.str().data(), array.at(i));
    }
    --depth;
  }
}

void Print(int depth, const char* name, const FooPtr& foo) {
  PrintSpacer(depth);
  std::cout << name << ":" << std::endl;
  if (!foo.is_null()) {
    ++depth;
    Print(depth, "name", foo->name);
    Print(depth, "x", foo->x);
    Print(depth, "y", foo->y);
    Print(depth, "a", foo->a);
    Print(depth, "b", foo->b);
    Print(depth, "c", foo->c);
    Print(depth, "bar", foo->bar);
    Print(depth, "extra_bars", foo->extra_bars);
    Print(depth, "data", foo->data);
    Print(depth, "source", foo->source);
    Print(depth, "array_of_array_of_bools", foo->array_of_array_of_bools);
    --depth;
  }
}

void DumpHex(const uint8_t* bytes, uint32_t num_bytes) {
  for (uint32_t i = 0; i < num_bytes; ++i) {
    std::cout << std::setw(2) << std::setfill('0') << std::hex
              << uint32_t(bytes[i]);

    if (i % 16 == 15) {
      std::cout << std::endl;
      continue;
    }

    if (i % 2 == 1)
      std::cout << " ";
    if (i % 8 == 7)
      std::cout << " ";
  }
}

class ServiceImpl : public Service {
 public:
  void Frobinate(FooPtr foo,
                 BazOptions baz,
                 fidl::InterfaceHandle<Port> port,
                 const Service::FrobinateCallback& callback) override {
    // Users code goes here to handle the incoming Frobinate message.

    // We mainly check that we're given the expected arguments.
    EXPECT_FALSE(foo.is_null());
    if (!foo.is_null())
      CheckFoo(*foo);
    EXPECT_EQ(Service::BazOptions::EXTRA, baz);

    if (g_dump_message_as_text) {
      // Also dump the Foo structure and all of its members.
      std::cout << "Frobinate:" << std::endl;
      int depth = 1;
      Print(depth, "foo", foo);
      Print(depth, "baz", static_cast<int32_t>(baz));
      auto portptr = port.Bind();
      Print(depth, "port", portptr.get());
    }
    callback(5);
  }

  void GetPort(fidl::InterfaceRequest<Port> port_request) override {}
};

class ServiceProxyImpl : public ServiceProxy {
 public:
  explicit ServiceProxyImpl(fidl::MessageReceiverWithResponder* receiver)
      : ServiceProxy(receiver) {}
};

class SimpleMessageReceiver : public fidl::MessageReceiverWithResponder {
 public:
  bool Accept(fidl::Message* message) override {
    // Imagine some IPC happened here.

    if (g_dump_message_as_hex) {
      DumpHex(reinterpret_cast<const uint8_t*>(message->data()),
              message->data_num_bytes());
    }

    // In the receiving process, an implementation of ServiceStub is known to
    // the system. It receives the incoming message.
    ServiceImpl impl;

    ServiceStub stub;
    stub.set_sink(&impl);
    return stub.Accept(message);
  }

  bool AcceptWithResponder(fidl::Message* message,
                           fidl::MessageReceiver* responder) override {
    return false;
  }
};

TEST(BindingsSampleTest, Basic) {
  SimpleMessageReceiver receiver;

  // User has a proxy to a Service somehow.
  Service* service = new ServiceProxyImpl(&receiver);

  // User constructs a message to send.

  // Notice that it doesn't matter in what order the structs / arrays are
  // allocated. Here, the various members of Foo are allocated before Foo is
  // allocated.

  FooPtr foo = MakeFoo();
  CheckFoo(*foo);

  PortPtr port;
  service->Frobinate(std::move(foo), Service::BazOptions::EXTRA,
                     std::move(port), [](uint32_t){});

  delete service;
}

TEST(BindingsSampleTest, DefaultValues) {
  DefaultsTestPtr defaults(DefaultsTest::New());
  EXPECT_EQ(-12, defaults->a0);
  EXPECT_EQ(kTwelve, defaults->a1);
  EXPECT_EQ(1234, defaults->a2);
  EXPECT_EQ(34567U, defaults->a3);
  EXPECT_EQ(123456, defaults->a4);
  EXPECT_EQ(3456789012U, defaults->a5);
  EXPECT_EQ(-111111111111LL, defaults->a6);
  EXPECT_EQ(9999999999999999999ULL, defaults->a7);
  EXPECT_EQ(0x12345, defaults->a8);
  EXPECT_EQ(-0x12345, defaults->a9);
  EXPECT_EQ(1234, defaults->a10);
  EXPECT_TRUE(defaults->a11);
  EXPECT_FALSE(defaults->a12);
  EXPECT_FLOAT_EQ(123.25f, defaults->a13);
  EXPECT_DOUBLE_EQ(1234567890.123, defaults->a14);
  EXPECT_DOUBLE_EQ(1E10, defaults->a15);
  EXPECT_DOUBLE_EQ(-1.2E+20, defaults->a16);
  EXPECT_DOUBLE_EQ(1.23E-20, defaults->a17);
  EXPECT_TRUE(defaults->a18.is_null());
  EXPECT_TRUE(defaults->a19.is_null());
  EXPECT_EQ(Bar::Type::BOTH, defaults->a20);
  EXPECT_TRUE(defaults->a21.is_null());
  ASSERT_FALSE(defaults->a22.is_null());
  EXPECT_EQ(imported::Shape::RECTANGLE, defaults->a22->shape);
  EXPECT_EQ(imported::Color::BLACK, defaults->a22->color);
  EXPECT_EQ(0xFFFFFFFFFFFFFFFFULL, defaults->a23);
  EXPECT_EQ(0x123456789, defaults->a24);
  EXPECT_EQ(-0x123456789, defaults->a25);
}

}  // namespace
}  // namespace sample

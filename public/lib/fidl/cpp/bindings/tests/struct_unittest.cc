// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <type_traits>

#include <zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/internal/fixed_buffer.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"
#include "lib/fidl/compiler/interfaces/tests/test_structs.fidl.h"

namespace f1dl {
namespace test {
namespace {

static_assert(std::is_same<std::underlying_type<ScopedConstants::EType>::type,
                           int32_t>::value,
              "The underlying type of mojom generated enums must be int32_t.");

RectPtr MakeRect(int32_t factor = 1) {
  RectPtr rect(Rect::New());
  rect->x = 1 * factor;
  rect->y = 2 * factor;
  rect->width = 10 * factor;
  rect->height = 20 * factor;
  return rect;
}

void CheckRect(const Rect& rect, int32_t factor = 1) {
  EXPECT_EQ(1 * factor, rect.x);
  EXPECT_EQ(2 * factor, rect.y);
  EXPECT_EQ(10 * factor, rect.width);
  EXPECT_EQ(20 * factor, rect.height);
}

MultiVersionStructPtr MakeMultiVersionStruct() {
  MultiVersionStructPtr output(MultiVersionStruct::New());
  output->f_int32 = 123;
  output->f_rect = MakeRect(5);
  output->f_string = "hello";
  output->f_array = Array<int8_t>::New(3);
  output->f_array[0] = 10;
  output->f_array[1] = 9;
  output->f_array[2] = 8;
  zx::channel handle0, handle1;
  zx::channel::create(0, &handle0, &handle1);
  output->f_message_pipe = std::move(handle0);
  output->f_int16 = 42;

  return output;
}

template <typename U, typename T>
U SerializeAndDeserialize(T input) {
  typedef typename f1dl::internal::WrapperTraits<T>::DataType InputDataType;
  typedef typename f1dl::internal::WrapperTraits<U>::DataType OutputDataType;

  size_t size = GetSerializedSize_(*input);
  f1dl::internal::FixedBufferForTesting buf(size + 32);
  InputDataType data;
  EXPECT_EQ(f1dl::internal::ValidationError::NONE,
            Serialize_(input.get(), &buf, &data));

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);

  // Set the subsequent area to a special value, so that we can find out if we
  // mistakenly access the area.
  void* subsequent_area = buf.Allocate(32);
  memset(subsequent_area, 0xAA, 32);

  OutputDataType output_data = reinterpret_cast<OutputDataType>(data);
  output_data->DecodePointersAndHandles(&handles);

  using RawUType = typename f1dl::internal::RemoveStructPtr<U>::type;
  U output(RawUType::New());
  Deserialize_(output_data, output.get());
  return output;
}

}  // namespace

TEST(StructTest, Rect) {
  RectPtr rect;
  EXPECT_FALSE(rect);
  EXPECT_TRUE(rect.is_null());
  EXPECT_TRUE(!rect);
  EXPECT_FALSE(rect);

  rect = nullptr;
  EXPECT_FALSE(rect);
  EXPECT_TRUE(rect.is_null());
  EXPECT_TRUE(!rect);
  EXPECT_FALSE(rect);

  rect = MakeRect();
  EXPECT_TRUE(rect);
  EXPECT_FALSE(rect.is_null());
  EXPECT_FALSE(!rect);
  EXPECT_TRUE(rect);

  RectPtr null_rect = nullptr;
  EXPECT_FALSE(null_rect);
  EXPECT_TRUE(null_rect.is_null());
  EXPECT_TRUE(!null_rect);
  EXPECT_FALSE(null_rect);

  CheckRect(*rect);
}

TEST(StructTest, Clone) {
  NamedRegionPtr region;

  NamedRegionPtr clone_region = region.Clone();
  EXPECT_TRUE(clone_region.is_null());

  region = NamedRegion::New();
  clone_region = region.Clone();
  EXPECT_TRUE(clone_region->name.is_null());
  EXPECT_TRUE(clone_region->rects.is_null());

  region->name = "hello world";
  clone_region = region.Clone();
  EXPECT_EQ(region->name, clone_region->name);

  region->rects = Array<RectPtr>::New(2);
  region->rects[1] = MakeRect();
  clone_region = region.Clone();
  EXPECT_EQ(2u, clone_region->rects.size());
  EXPECT_TRUE(clone_region->rects[0].is_null());
  CheckRect(*clone_region->rects[1]);

  // NoDefaultFieldValues contains handles, so Clone() is not available, but
  // NoDefaultFieldValuesPtr should still compile.
  NoDefaultFieldValuesPtr no_default_field_values(NoDefaultFieldValues::New());
  EXPECT_FALSE(no_default_field_values->f13);
}

// Serialization test of a struct with no pointer or handle members.
TEST(StructTest, Serialization_Basic) {
  RectPtr rect(MakeRect());

  size_t size = GetSerializedSize_(*rect);
  EXPECT_EQ(8U + 16U, size);

  f1dl::internal::FixedBufferForTesting buf(size);
  internal::Rect_Data* data;
  EXPECT_EQ(f1dl::internal::ValidationError::NONE,
            Serialize_(rect.get(), &buf, &data));

  RectPtr rect2(Rect::New());
  Deserialize_(data, rect2.get());

  CheckRect(*rect2);
}

// Construction of a struct with struct pointers from null.
TEST(StructTest, Construction_StructPointers) {
  RectPairPtr pair;
  EXPECT_TRUE(pair.is_null());

  pair = RectPair::New();
  EXPECT_FALSE(pair.is_null());
  EXPECT_TRUE(pair->first.is_null());
  EXPECT_TRUE(pair->first.is_null());

  pair = nullptr;
  EXPECT_TRUE(pair.is_null());
}

// Serialization test of a struct with struct pointers.
TEST(StructTest, Serialization_StructPointers) {
  RectPairPtr pair(RectPair::New());
  pair->first = MakeRect();
  pair->second = MakeRect();

  size_t size = GetSerializedSize_(*pair);
  EXPECT_EQ(8U + 16U + 2 * (8U + 16U), size);

  f1dl::internal::FixedBufferForTesting buf(size);
  internal::RectPair_Data* data;
  EXPECT_EQ(f1dl::internal::ValidationError::NONE,
            Serialize_(pair.get(), &buf, &data));

  RectPairPtr pair2(RectPair::New());
  Deserialize_(data, pair2.get());

  CheckRect(*pair2->first);
  CheckRect(*pair2->second);
}

// Serialization test of a struct with an array member.
TEST(StructTest, Serialization_ArrayPointers) {
  NamedRegionPtr region(NamedRegion::New());
  region->name = "region";
  region->rects = Array<RectPtr>::New(4);
  for (size_t i = 0; i < region->rects.size(); ++i)
    region->rects[i] = MakeRect(static_cast<int32_t>(i) + 1);

  size_t size = GetSerializedSize_(*region);
  EXPECT_EQ(8U +            // header
                8U +        // name pointer
                8U +        // rects pointer
                8U +        // name header
                8U +        // name payload (rounded up)
                8U +        // rects header
                4 * 8U +    // rects payload (four pointers)
                4 * (8U +   // rect header
                     16U),  // rect payload (four ints)
            size);

  f1dl::internal::FixedBufferForTesting buf(size);
  internal::NamedRegion_Data* data;
  EXPECT_EQ(f1dl::internal::ValidationError::NONE,
            Serialize_(region.get(), &buf, &data));

  NamedRegionPtr region2(NamedRegion::New());
  Deserialize_(data, region2.get());

  EXPECT_EQ(String("region"), region2->name);

  EXPECT_EQ(4U, region2->rects.size());
  for (size_t i = 0; i < region2->rects.size(); ++i)
    CheckRect(*region2->rects[i], static_cast<int32_t>(i) + 1);
}

// Serialization test of a struct with null array pointers.
TEST(StructTest, Serialization_NullArrayPointers) {
  NamedRegionPtr region(NamedRegion::New());
  EXPECT_TRUE(region->name.is_null());
  EXPECT_TRUE(region->rects.is_null());

  size_t size = GetSerializedSize_(*region);
  EXPECT_EQ(8U +      // header
                8U +  // name pointer
                8U,   // rects pointer
            size);

  f1dl::internal::FixedBufferForTesting buf(size);
  internal::NamedRegion_Data* data;
  EXPECT_EQ(f1dl::internal::ValidationError::NONE,
            Serialize_(region.get(), &buf, &data));

  NamedRegionPtr region2(NamedRegion::New());
  Deserialize_(data, region2.get());

  EXPECT_TRUE(region2->name.is_null());
  EXPECT_TRUE(region2->rects.is_null());
}

TEST(StructTest, Serialization_InterfaceRequest) {
  ContainsInterfaceRequest iface_req_struct;

  auto size = GetSerializedSize_(iface_req_struct);
  EXPECT_EQ(8U         // struct header
                + 4U   // interface request handle
                + 4U,  // interface request nullable handle
            size);

  f1dl::internal::FixedBufferForTesting buf(size * 2);
  ContainsInterfaceRequest::Data_* data;

#ifdef NDEBUG // In debug builds serialization failures abort
  // Test failure when non-nullable interface request is null.
  EXPECT_EQ(f1dl::internal::ValidationError::UNEXPECTED_INVALID_HANDLE,
            Serialize_(&iface_req_struct, &buf, &data));
#endif

  SomeInterfacePtr i_ptr;
  iface_req_struct.req = i_ptr.NewRequest();
  EXPECT_TRUE(iface_req_struct.req.is_valid());

  EXPECT_EQ(f1dl::internal::ValidationError::NONE,
            Serialize_(&iface_req_struct, &buf, &data));
  EXPECT_FALSE(iface_req_struct.req.is_valid());

  Deserialize_(data, &iface_req_struct);
  EXPECT_TRUE(iface_req_struct.req.is_valid());
}

// Tests deserializing structs as a newer version.
TEST(StructTest, Versioning_OldToNew) {
  {
    MultiVersionStructV0Ptr input(MultiVersionStructV0::New());
    input->f_int32 = 123;
    MultiVersionStructPtr expected_output(MultiVersionStruct::New());
    expected_output->f_int32 = 123;

    MultiVersionStructPtr output =
        SerializeAndDeserialize<MultiVersionStructPtr>(std::move(input));
    EXPECT_TRUE(output);
    EXPECT_TRUE(output->Equals(*expected_output));
  }

  {
    MultiVersionStructV1Ptr input(MultiVersionStructV1::New());
    input->f_int32 = 123;
    input->f_rect = MakeRect(5);
    MultiVersionStructPtr expected_output(MultiVersionStruct::New());
    expected_output->f_int32 = 123;
    expected_output->f_rect = MakeRect(5);

    MultiVersionStructPtr output =
        SerializeAndDeserialize<MultiVersionStructPtr>(std::move(input));
    EXPECT_TRUE(output);
    EXPECT_TRUE(output->Equals(*expected_output));
  }

  {
    MultiVersionStructV3Ptr input(MultiVersionStructV3::New());
    input->f_int32 = 123;
    input->f_rect = MakeRect(5);
    input->f_string = "hello";
    MultiVersionStructPtr expected_output(MultiVersionStruct::New());
    expected_output->f_int32 = 123;
    expected_output->f_rect = MakeRect(5);
    expected_output->f_string = "hello";

    MultiVersionStructPtr output =
        SerializeAndDeserialize<MultiVersionStructPtr>(std::move(input));
    EXPECT_TRUE(output);
    EXPECT_TRUE(output->Equals(*expected_output));
  }

  {
    MultiVersionStructV5Ptr input(MultiVersionStructV5::New());
    input->f_int32 = 123;
    input->f_rect = MakeRect(5);
    input->f_string = "hello";
    input->f_array = Array<int8_t>::New(3);
    input->f_array[0] = 10;
    input->f_array[1] = 9;
    input->f_array[2] = 8;
    MultiVersionStructPtr expected_output(MultiVersionStruct::New());
    expected_output->f_int32 = 123;
    expected_output->f_rect = MakeRect(5);
    expected_output->f_string = "hello";
    expected_output->f_array = Array<int8_t>::New(3);
    expected_output->f_array[0] = 10;
    expected_output->f_array[1] = 9;
    expected_output->f_array[2] = 8;

    MultiVersionStructPtr output =
        SerializeAndDeserialize<MultiVersionStructPtr>(std::move(input));
    EXPECT_TRUE(output);
    EXPECT_TRUE(output->Equals(*expected_output));
  }

  {
    MultiVersionStructV7Ptr input(MultiVersionStructV7::New());
    input->f_int32 = 123;
    input->f_rect = MakeRect(5);
    input->f_string = "hello";
    input->f_array = Array<int8_t>::New(3);
    input->f_array[0] = 10;
    input->f_array[1] = 9;
    input->f_array[2] = 8;
    zx::channel handle0, handle1;
    zx::channel::create(0, &handle0, &handle1);
    input->f_message_pipe = std::move(handle0);

    MultiVersionStructPtr expected_output(MultiVersionStruct::New());
    expected_output->f_int32 = 123;
    expected_output->f_rect = MakeRect(5);
    expected_output->f_string = "hello";
    expected_output->f_array = Array<int8_t>::New(3);
    expected_output->f_array[0] = 10;
    expected_output->f_array[1] = 9;
    expected_output->f_array[2] = 8;
    // Save the raw handle value separately so that we can compare later.
    zx_handle_t expected_handle = input->f_message_pipe.get();

    MultiVersionStructPtr output =
        SerializeAndDeserialize<MultiVersionStructPtr>(std::move(input));
    EXPECT_TRUE(output);
    EXPECT_EQ(expected_handle, output->f_message_pipe.get());
    output->f_message_pipe.reset();
    EXPECT_TRUE(output->Equals(*expected_output));
  }
}

// Tests deserializing structs as an older version.
TEST(StructTest, Versioning_NewToOld) {
  {
    MultiVersionStructPtr input = MakeMultiVersionStruct();
    MultiVersionStructV7Ptr expected_output(MultiVersionStructV7::New());
    expected_output->f_int32 = 123;
    expected_output->f_rect = MakeRect(5);
    expected_output->f_string = "hello";
    expected_output->f_array = Array<int8_t>::New(3);
    expected_output->f_array[0] = 10;
    expected_output->f_array[1] = 9;
    expected_output->f_array[2] = 8;
    // Save the raw handle value separately so that we can compare later.
    zx_handle_t expected_handle = input->f_message_pipe.get();

    MultiVersionStructV7Ptr output =
        SerializeAndDeserialize<MultiVersionStructV7Ptr>(std::move(input));
    EXPECT_TRUE(output);
    EXPECT_EQ(expected_handle, output->f_message_pipe.get());
    output->f_message_pipe.reset();
    EXPECT_TRUE(output->Equals(*expected_output));
  }

  {
    MultiVersionStructPtr input = MakeMultiVersionStruct();
    MultiVersionStructV5Ptr expected_output(MultiVersionStructV5::New());
    expected_output->f_int32 = 123;
    expected_output->f_rect = MakeRect(5);
    expected_output->f_string = "hello";
    expected_output->f_array = Array<int8_t>::New(3);
    expected_output->f_array[0] = 10;
    expected_output->f_array[1] = 9;
    expected_output->f_array[2] = 8;

    MultiVersionStructV5Ptr output =
        SerializeAndDeserialize<MultiVersionStructV5Ptr>(std::move(input));
    EXPECT_TRUE(output);
    EXPECT_TRUE(output->Equals(*expected_output));
  }

  {
    MultiVersionStructPtr input = MakeMultiVersionStruct();
    MultiVersionStructV3Ptr expected_output(MultiVersionStructV3::New());
    expected_output->f_int32 = 123;
    expected_output->f_rect = MakeRect(5);
    expected_output->f_string = "hello";

    MultiVersionStructV3Ptr output =
        SerializeAndDeserialize<MultiVersionStructV3Ptr>(std::move(input));
    EXPECT_TRUE(output);
    EXPECT_TRUE(output->Equals(*expected_output));
  }

  {
    MultiVersionStructPtr input = MakeMultiVersionStruct();
    MultiVersionStructV1Ptr expected_output(MultiVersionStructV1::New());
    expected_output->f_int32 = 123;
    expected_output->f_rect = MakeRect(5);

    MultiVersionStructV1Ptr output =
        SerializeAndDeserialize<MultiVersionStructV1Ptr>(std::move(input));
    EXPECT_TRUE(output);
    EXPECT_TRUE(output->Equals(*expected_output));
  }

  {
    MultiVersionStructPtr input = MakeMultiVersionStruct();
    MultiVersionStructV0Ptr expected_output(MultiVersionStructV0::New());
    expected_output->f_int32 = 123;

    MultiVersionStructV0Ptr output =
        SerializeAndDeserialize<MultiVersionStructV0Ptr>(std::move(input));
    EXPECT_TRUE(output);
    EXPECT_TRUE(output->Equals(*expected_output));
  }
}

}  // namespace test
}  // namespace f1dl

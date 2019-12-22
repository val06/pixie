#include <arrow/array.h>
#include <arrow/buffer.h>
#include <arrow/builder.h>
#include <gtest/gtest.h>

#include <iostream>
#include <memory>

#include "src/shared/types/column_wrapper.h"
#include "src/shared/types/types.h"

namespace pl {
namespace types {

TEST(ColumnWrapperTest, make_test_bool) {
  auto wrapper = ColumnWrapper::Make(DataType::BOOLEAN, 10);
  EXPECT_EQ(10, wrapper->Size());
  EXPECT_EQ(DataType::BOOLEAN, wrapper->data_type());
  EXPECT_NE(nullptr, wrapper->UnsafeRawData());

  auto arrow_arr = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_EQ(DataTypeTraits<DataType::BOOLEAN>::arrow_type_id, arrow_arr->type_id());
}

TEST(ColumnWrapperTest, make_test_int64) {
  auto wrapper = ColumnWrapper::Make(DataType::INT64, 10);
  EXPECT_EQ(10, wrapper->Size());
  EXPECT_EQ(DataType::INT64, wrapper->data_type());
  EXPECT_NE(nullptr, wrapper->UnsafeRawData());

  auto arrow_arr = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_EQ(DataTypeTraits<DataType::INT64>::arrow_type_id, arrow_arr->type_id());
}

TEST(ColumnWrapperTest, make_test_uint128) {
  auto wrapper = ColumnWrapper::Make(DataType::UINT128, 10);
  EXPECT_EQ(10, wrapper->Size());
  EXPECT_EQ(DataType::UINT128, wrapper->data_type());
  EXPECT_NE(nullptr, wrapper->UnsafeRawData());

  auto arrow_arr = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_EQ(DataTypeTraits<DataType::UINT128>::arrow_type_id, arrow_arr->type_id());
}

TEST(ColumnWrapperTest, make_test_float64) {
  auto wrapper = ColumnWrapper::Make(DataType::FLOAT64, 10);
  EXPECT_EQ(10, wrapper->Size());
  EXPECT_EQ(DataType::FLOAT64, wrapper->data_type());
  EXPECT_NE(nullptr, wrapper->UnsafeRawData());

  auto arrow_arr = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_EQ(DataTypeTraits<DataType::FLOAT64>::arrow_type_id, arrow_arr->type_id());
}

TEST(ColumnWrapperTest, make_test_string) {
  auto wrapper = ColumnWrapper::Make(DataType::STRING, 10);
  EXPECT_EQ(10, wrapper->Size());
  EXPECT_EQ(DataType::STRING, wrapper->data_type());
  EXPECT_NE(nullptr, wrapper->UnsafeRawData());

  auto arrow_arr = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_EQ(DataTypeTraits<DataType::STRING>::arrow_type_id, arrow_arr->type_id());
}

TEST(ColumnWrapper, FromArrowBool) {
  arrow::BooleanBuilder builder;
  PL_CHECK_OK(builder.Append(true));
  PL_CHECK_OK(builder.Append(true));
  PL_CHECK_OK(builder.Append(false));

  std::shared_ptr<arrow::Array> arr;
  PL_CHECK_OK(builder.Finish(&arr));

  auto wrapper = ColumnWrapper::FromArrow(arr);
  auto converted_to_arrow = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_TRUE(converted_to_arrow->Equals(arr));
}

TEST(ColumnWrapper, FromArrowInt64) {
  arrow::Int64Builder builder;
  PL_CHECK_OK(builder.Append(1));
  PL_CHECK_OK(builder.Append(2));
  PL_CHECK_OK(builder.Append(3));

  std::shared_ptr<arrow::Array> arr;
  PL_CHECK_OK(builder.Finish(&arr));

  auto wrapper = ColumnWrapper::FromArrow(arr);
  auto converted_to_arrow = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_TRUE(converted_to_arrow->Equals(arr));
}

TEST(ColumnWrapper, FromArrowUInt128) {
  arrow::UInt128Builder builder;
  PL_CHECK_OK(builder.Append(absl::MakeUint128(100, 200)));
  PL_CHECK_OK(builder.Append(absl::MakeUint128(200, 300)));
  PL_CHECK_OK(builder.Append(absl::MakeUint128(300, 400)));

  std::shared_ptr<arrow::Array> arr;
  PL_CHECK_OK(builder.Finish(&arr));

  auto wrapper = ColumnWrapper::FromArrow(arr);
  auto converted_to_arrow = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_TRUE(converted_to_arrow->Equals(arr));
}

TEST(ColumnWrapper, FromArrowFloat64) {
  arrow::DoubleBuilder builder;
  PL_CHECK_OK(builder.Append(1));
  PL_CHECK_OK(builder.Append(2));
  PL_CHECK_OK(builder.Append(3));

  std::shared_ptr<arrow::Array> arr;
  PL_CHECK_OK(builder.Finish(&arr));

  auto wrapper = ColumnWrapper::FromArrow(arr);
  auto converted_to_arrow = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_TRUE(converted_to_arrow->ApproxEquals(arr));
}

TEST(ColumnWrapper, FromArrowString) {
  arrow::StringBuilder builder;
  PL_CHECK_OK(builder.Append("abc"));
  PL_CHECK_OK(builder.Append("def"));
  PL_CHECK_OK(builder.Append("hello"));

  std::shared_ptr<arrow::Array> arr;
  PL_CHECK_OK(builder.Finish(&arr));

  auto wrapper = ColumnWrapper::FromArrow(arr);
  auto converted_to_arrow = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_TRUE(converted_to_arrow->Equals(arr));
}

TEST(ColumnWrapperDeathTest, AppendTypeMismatches) {
  auto wrapper = ColumnWrapper::Make(DataType::BOOLEAN, 1);
  ASSERT_EQ(1, wrapper->Size());
  EXPECT_DEATH(wrapper->Append<types::StringValue>("abc"),
               R"(\(1 vs\. 5\) Expect BOOLEAN got STRING)");
}

TEST(ColumnWrapperTest, FromVectorInt64) {
  auto wrapper = ColumnWrapper::Make(DataType::INT64, 4);
  std::vector<types::Int64Value> int_vector({4, 2, 3, 1});
  wrapper->Clear();
  wrapper->AppendFromVector(int_vector);
  auto actual_arr = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_EQ(DataTypeTraits<DataType::INT64>::arrow_type_id, actual_arr->type_id());

  // build the comparison list.
  arrow::Int64Builder builder;
  PL_CHECK_OK(builder.Append(4));
  PL_CHECK_OK(builder.Append(2));
  PL_CHECK_OK(builder.Append(3));
  PL_CHECK_OK(builder.Append(1));

  std::shared_ptr<arrow::Array> expected_arr;
  PL_CHECK_OK(builder.Finish(&expected_arr));

  EXPECT_TRUE(actual_arr->Equals(expected_arr));
}

TEST(ColumnWrapperTest, FromVectorString) {
  auto wrapper = ColumnWrapper::Make(DataType::STRING, 4);
  std::vector<types::StringValue> string_vector({"abc", "def", "ghi", "jkl"});
  wrapper->Clear();
  wrapper->AppendFromVector(string_vector);
  auto actual_arr = wrapper->ConvertToArrow(arrow::default_memory_pool());
  EXPECT_EQ(DataTypeTraits<DataType::STRING>::arrow_type_id, actual_arr->type_id());

  // build the comparison list.
  arrow::StringBuilder builder;
  PL_CHECK_OK(builder.Append("abc"));
  PL_CHECK_OK(builder.Append("def"));
  PL_CHECK_OK(builder.Append("ghi"));
  PL_CHECK_OK(builder.Append("jkl"));

  std::shared_ptr<arrow::Array> expected_arr;
  PL_CHECK_OK(builder.Finish(&expected_arr));

  EXPECT_TRUE(actual_arr->Equals(expected_arr));
}

}  // namespace types
}  // namespace pl

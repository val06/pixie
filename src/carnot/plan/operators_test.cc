#include <gtest/gtest.h>
#include <iostream>

#include "src/carnot/plan/operators.h"
#include "src/carnot/planpb/plan.pb.h"
#include "src/carnot/planpb/test_proto.h"
#include "src/carnot/udf/registry.h"
#include "src/carnot/udf/udf.h"
#include "src/common/base/test_utils.h"
#include "src/table_store/table_store.h"

namespace pl {
namespace carnot {
namespace plan {

using table_store::schema::Relation;
using table_store::schema::Schema;

class DummyTestUDF : public udf::ScalarUDF {
 public:
  types::Int64Value Exec(udf::FunctionContext*, types::BoolValue, types::Float64Value) { return 0; }
};

class DummyTestUDA : public udf::UDA {
 public:
  Status Init(udf::FunctionContext*) { return Status::OK(); }
  void Update(udf::FunctionContext*, types::BoolValue) {}
  void Merge(udf::FunctionContext*, const DummyTestUDA&) {}
  types::Int64Value Finalize(udf::FunctionContext*) { return 0; }
};

class OperatorTest : public ::testing::Test {
 public:
  OperatorTest() {
    udf_registry_ = std::make_unique<udf::ScalarUDFRegistry>("test");
    uda_registry_ = std::make_unique<udf::UDARegistry>("testUDA");
    udtf_registry_ = std::make_unique<udf::UDTFRegistry>("testUDTF");
    state_ =
        std::make_unique<PlanState>(udf_registry_.get(), uda_registry_.get(), udtf_registry_.get());

    state_->udf_registry()->RegisterOrDie<DummyTestUDF>("testUdf");
    state_->uda_registry()->RegisterOrDie<DummyTestUDA>("testUda");
    Relation rel0;
    rel0.AddColumn(types::INT64, "col0");
    rel0.AddColumn(types::FLOAT64, "col1");

    Relation rel1;
    rel1.AddColumn(types::INT64, "col0");
    rel1.AddColumn(types::FLOAT64, "col1");

    Relation rel2;
    rel2.AddColumn(types::DataType::FLOAT64, "abc");
    rel2.AddColumn(types::DataType::INT64, "time_");

    Relation rel3;
    rel3.AddColumn(types::DataType::INT64, "time_");
    rel3.AddColumn(types::DataType::FLOAT64, "abc");

    Relation rel4;
    rel4.AddColumn(types::DataType::FLOAT64, "time_");
    rel4.AddColumn(types::DataType::INT64, "xyz");

    Relation rel5;
    rel5.AddColumn(types::DataType::INT64, "time_");

    schema_.AddRelation(0, rel0);
    schema_.AddRelation(1, rel1);
    schema_.AddRelation(2, rel2);
    schema_.AddRelation(3, rel3);
    schema_.AddRelation(4, rel4);
    schema_.AddRelation(5, rel5);
  }

  ~OperatorTest() override = default;

 protected:
  Schema schema_;
  std::unique_ptr<PlanState> state_;
  std::unique_ptr<udf::ScalarUDFRegistry> udf_registry_;
  std::unique_ptr<udf::UDARegistry> uda_registry_;
  std::unique_ptr<udf::UDTFRegistry> udtf_registry_;
};

TEST_F(OperatorTest, from_proto_map) {
  auto map_pb = planpb::testutils::CreateTestMap1PB();
  auto map_op = Operator::FromProto(map_pb, 1);
  EXPECT_EQ(1, map_op->id());
  EXPECT_TRUE(map_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::MAP_OPERATOR, map_op->op_type());
}

TEST_F(OperatorTest, from_proto_mem_src) {
  auto src_pb = planpb::testutils::CreateTestSource1PB();
  auto src_op = Operator::FromProto(src_pb, 1);
  EXPECT_EQ(1, src_op->id());
  EXPECT_TRUE(src_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::MEMORY_SOURCE_OPERATOR, src_op->op_type());
}

TEST_F(OperatorTest, from_proto_mem_sink) {
  auto sink_pb = planpb::testutils::CreateTestSink1PB();
  auto sink_op = Operator::FromProto(sink_pb, 1);
  EXPECT_EQ(1, sink_op->id());
  EXPECT_TRUE(sink_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::MEMORY_SINK_OPERATOR, sink_op->op_type());
}

TEST_F(OperatorTest, from_proto_grpc_source) {
  auto sink_pb = planpb::testutils::CreateTestGRPCSource1PB();
  auto sink_op = Operator::FromProto(sink_pb, 1);
  EXPECT_EQ(1, sink_op->id());
  EXPECT_TRUE(sink_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::GRPC_SOURCE_OPERATOR, sink_op->op_type());
}

TEST_F(OperatorTest, from_proto_grpc_sink) {
  auto sink_pb = planpb::testutils::CreateTestGRPCSink1PB();
  auto sink_op = Operator::FromProto(sink_pb, 1);
  EXPECT_EQ(1, sink_op->id());
  EXPECT_TRUE(sink_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::GRPC_SINK_OPERATOR, sink_op->op_type());
}

TEST_F(OperatorTest, from_proto_blocking_agg) {
  auto agg_pb = planpb::testutils::CreateTestBlockingAgg1PB();
  auto agg_op = Operator::FromProto(agg_pb, 1);
  EXPECT_EQ(1, agg_op->id());
  EXPECT_TRUE(agg_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::AGGREGATE_OPERATOR, agg_op->op_type());
}

TEST_F(OperatorTest, from_proto_windowed_agg) {
  auto agg_pb = planpb::testutils::CreateTestWindowedAgg1PB();
  auto agg_op = Operator::FromProto(agg_pb, 1);
  EXPECT_EQ(1, agg_op->id());
  EXPECT_TRUE(agg_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::AGGREGATE_OPERATOR, agg_op->op_type());
}

TEST_F(OperatorTest, from_proto_filter) {
  auto filter_pb = planpb::testutils::CreateTestFilter1PB();
  auto filter_op = Operator::FromProto(filter_pb, 1);
  EXPECT_EQ(1, filter_op->id());
  EXPECT_TRUE(filter_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::FILTER_OPERATOR, filter_op->op_type());
}

TEST_F(OperatorTest, from_proto_union_time_column) {
  auto union_pb = planpb::testutils::CreateTestUnionOrderedPB();
  auto union_op = std::make_unique<UnionOperator>(1);
  auto s = union_op->Init(union_pb.union_op());
  EXPECT_EQ(1, union_op->id());
  EXPECT_TRUE(union_op->is_initialized());
  EXPECT_EQ(5, union_op->rows_per_batch());
  EXPECT_EQ(planpb::OperatorType::UNION_OPERATOR, union_op->op_type());
}

TEST_F(OperatorTest, from_proto_union_no_time_column) {
  auto union_pb = planpb::testutils::CreateTestUnionUnorderedPB();
  auto union_op = Operator::FromProto(union_pb, 1);
  EXPECT_EQ(1, union_op->id());
  EXPECT_TRUE(union_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::UNION_OPERATOR, union_op->op_type());
}

TEST_F(OperatorTest, from_proto_union_out_of_range_1) {
  auto union_pb = planpb::testutils::CreateTestUnionOutOfRange1();
  auto union_op = std::make_unique<UnionOperator>(1);
  auto s = union_op->Init(union_pb.union_op());
  EXPECT_NOT_OK(s);
  EXPECT_EQ(
      s.msg(),
      "Inconsistent number of columns in UnionOperator, expected 2 but received 1 for input 0.");
}

TEST_F(OperatorTest, from_proto_union_out_of_range_2) {
  auto union_pb = planpb::testutils::CreateTestUnionOutOfRange2();
  auto union_op = std::make_unique<UnionOperator>(1);
  auto s = union_op->Init(union_pb.union_op());
  EXPECT_NOT_OK(s);
  EXPECT_EQ(
      s.msg(),
      "Inconsistent number of columns in UnionOperator, expected 2 but received 3 for input 0.");
}

TEST_F(OperatorTest, from_proto_limit) {
  auto limit_pb = planpb::testutils::CreateTestLimit1PB();
  auto limit_op = Operator::FromProto(limit_pb, 1);
  EXPECT_EQ(1, limit_op->id());
  EXPECT_TRUE(limit_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::LIMIT_OPERATOR, limit_op->op_type());
}

TEST_F(OperatorTest, from_proto_join_with_time) {
  auto join_pb = planpb::testutils::CreateTestJoinWithTimePB();
  auto join_op = std::make_unique<JoinOperator>(1);
  auto s = join_op->Init(join_pb.join_op());
  EXPECT_EQ(1, join_op->id());
  EXPECT_TRUE(join_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::JOIN_OPERATOR, join_op->op_type());

  EXPECT_EQ(10, join_op->rows_per_batch());
  EXPECT_TRUE(join_op->order_by_time());
  EXPECT_EQ(1, join_op->time_column().parent_index());
  EXPECT_EQ(0, join_op->time_column().column_index());
}

TEST_F(OperatorTest, from_proto_join_full_outer_time_ordered_error) {
  auto join_pb = planpb::testutils::CreateTestErrorJoin1PB();
  auto join_op = std::make_unique<JoinOperator>(1);
  auto s = join_op->Init(join_pb.join_op());
  EXPECT_NOT_OK(s);
  EXPECT_EQ(s.msg(), "For time ordered joins, full outer join is not supported.");
}

TEST_F(OperatorTest, from_proto_join_left_outer_time_ordered_error) {
  auto join_pb = planpb::testutils::CreateTestErrorJoin2PB();
  auto join_op = std::make_unique<JoinOperator>(1);
  auto s = join_op->Init(join_pb.join_op());
  EXPECT_NOT_OK(s);
  EXPECT_EQ(
      s.msg(),
      "For time ordered joins, left join is only supported when time_ comes from the left table.");
}

TEST_F(OperatorTest, from_proto_join_no_time) {
  auto join_pb = planpb::testutils::CreateTestJoinNoTimePB();
  auto join_op = std::make_unique<JoinOperator>(1);
  auto s = join_op->Init(join_pb.join_op());
  EXPECT_EQ(1, join_op->id());
  EXPECT_TRUE(join_op->is_initialized());
  EXPECT_EQ(planpb::OperatorType::JOIN_OPERATOR, join_op->op_type());

  EXPECT_EQ(10, join_op->rows_per_batch());
  EXPECT_FALSE(join_op->order_by_time());
}

TEST_F(OperatorTest, output_relation_source) {
  auto src_pb = planpb::testutils::CreateTestSource1PB();
  auto src_op = Operator::FromProto(src_pb, 1);

  auto rel = src_op->OutputRelation(schema_, *state_, std::vector<int64_t>()).ConsumeValueOrDie();

  Relation expected_relation;
  expected_relation.AddColumn(types::DataType::FLOAT64, "usage");
  EXPECT_EQ(expected_relation, rel);
}

TEST_F(OperatorTest, output_relation_source_inputs) {
  auto src_pb = planpb::testutils::CreateTestSource1PB();
  auto src_op = Operator::FromProto(src_pb, 1);

  auto rel = src_op->OutputRelation(schema_, *state_, std::vector<int64_t>({1}));
  EXPECT_NOT_OK(rel);
  EXPECT_EQ(rel.msg(), "Source operator cannot have any inputs");
}

TEST_F(OperatorTest, output_relation_sink) {
  auto sink_pb = planpb::testutils::CreateTestSink1PB();
  auto sink_op = Operator::FromProto(sink_pb, 1);
  auto rel = sink_op->OutputRelation(schema_, *state_, std::vector<int64_t>());
  EXPECT_EQ(0, rel.ValueOrDie().NumColumns());
}

TEST_F(OperatorTest, output_relation_map) {
  auto map_pb = planpb::testutils::CreateTestMap1PB();
  auto map_op = Operator::FromProto(map_pb, 1);
  auto rel =
      map_op->OutputRelation(schema_, *state_, std::vector<int64_t>({1})).ConsumeValueOrDie();

  Relation expected_relation;
  expected_relation.AddColumn(types::DataType::INT64, "col1");
  EXPECT_EQ(expected_relation, rel);
}

TEST_F(OperatorTest, output_relation_map_no_input) {
  auto map_pb = planpb::testutils::CreateTestMap1PB();
  auto map_op = Operator::FromProto(map_pb, 1);
  auto rel = map_op->OutputRelation(schema_, *state_, std::vector<int64_t>({}));
  EXPECT_NOT_OK(rel);
  EXPECT_EQ(rel.msg(), "Map operator must have exactly one input");
}

TEST_F(OperatorTest, output_relation_map_missing_rel) {
  auto map_pb = planpb::testutils::CreateTestMap1PB();
  auto map_op = Operator::FromProto(map_pb, 1);
  auto rel = map_op->OutputRelation(schema_, *state_, std::vector<int64_t>({10}));
  EXPECT_NOT_OK(rel);
  EXPECT_EQ(rel.msg(), "Missing relation (10) for input of Map");
}

TEST_F(OperatorTest, output_relation_blocking_agg_no_input) {
  auto agg_pb = planpb::testutils::CreateTestBlockingAgg1PB();
  auto agg_op = Operator::FromProto(agg_pb, 1);
  auto rel = agg_op->OutputRelation(schema_, *state_, std::vector<int64_t>({}));
  EXPECT_NOT_OK(rel);
  EXPECT_EQ(rel.msg(), "BlockingAgg operator must have exactly one input");
}

TEST_F(OperatorTest, output_relation_blocking_agg_missing_rel) {
  auto agg_pb = planpb::testutils::CreateTestBlockingAgg1PB();
  auto agg_op = Operator::FromProto(agg_pb, 1);
  auto rel = agg_op->OutputRelation(schema_, *state_, std::vector<int64_t>({10}));
  EXPECT_NOT_OK(rel);
  EXPECT_EQ(rel.msg(), "Missing relation (10) for input of BlockingAggregateOperator");
}

TEST_F(OperatorTest, output_relation_agg) {
  auto agg_pb = planpb::testutils::CreateTestBlockingAgg1PB();
  auto agg_op = Operator::FromProto(agg_pb, 1);

  auto rel =
      agg_op->OutputRelation(schema_, *state_, std::vector<int64_t>({0})).ConsumeValueOrDie();

  Relation expected_relation;
  expected_relation.AddColumn(types::DataType::FLOAT64, "group1");
  expected_relation.AddColumn(types::DataType::INT64, "value1");
  EXPECT_EQ(expected_relation, rel);
}

TEST_F(OperatorTest, output_relation_filter) {
  auto filter_pb = planpb::testutils::CreateTestFilter1PB();
  auto filter_op = Operator::FromProto(filter_pb, 1);

  auto rel =
      filter_op->OutputRelation(schema_, *state_, std::vector<int64_t>({0})).ConsumeValueOrDie();

  Relation expected_relation;
  expected_relation.AddColumn(types::DataType::INT64, "col0");
  expected_relation.AddColumn(types::DataType::FLOAT64, "col1");
  EXPECT_EQ(expected_relation, rel);
}

TEST_F(OperatorTest, output_relation_limit) {
  auto limit_pb = planpb::testutils::CreateTestLimit1PB();
  auto limit_op = Operator::FromProto(limit_pb, 1);

  auto rel =
      limit_op->OutputRelation(schema_, *state_, std::vector<int64_t>({0})).ConsumeValueOrDie();
  Relation expected_relation;
  expected_relation.AddColumn(types::DataType::INT64, "col0");
  expected_relation.AddColumn(types::DataType::FLOAT64, "col1");
  EXPECT_EQ(expected_relation, rel);
}

TEST_F(OperatorTest, output_relation_union) {
  auto union_pb = planpb::testutils::CreateTestUnionOrderedPB();
  auto union_op = Operator::FromProto(union_pb, 4);

  auto rel =
      union_op->OutputRelation(schema_, *state_, std::vector<int64_t>({2, 3})).ConsumeValueOrDie();
  Relation expected_relation;
  expected_relation.AddColumn(types::DataType::FLOAT64, "abc");
  expected_relation.AddColumn(types::DataType::INT64, "time_");
  EXPECT_EQ(expected_relation, rel);
}

TEST_F(OperatorTest, output_relation_union_mismatched) {
  auto union_pb = planpb::testutils::CreateTestUnionOrderedPB();
  auto union_op = Operator::FromProto(union_pb, 4);

  auto rel = union_op->OutputRelation(schema_, *state_, std::vector<int64_t>({2, 4}));
  EXPECT_NOT_OK(rel);
  EXPECT_EQ(rel.msg(), "Conflicting types for column (abc) in UnionOperator");
}

TEST_F(OperatorTest, output_relation_union_out_of_range) {
  auto union_pb = planpb::testutils::CreateTestUnionOrderedPB();
  auto union_op = Operator::FromProto(union_pb, 4);

  auto rel = union_op->OutputRelation(schema_, *state_, std::vector<int64_t>({2, 5}));
  EXPECT_NOT_OK(rel);
  EXPECT_EQ(rel.msg(), "Missing column 1 of input 1 in UnionOperator");
}

}  // namespace plan
}  // namespace carnot
}  // namespace pl

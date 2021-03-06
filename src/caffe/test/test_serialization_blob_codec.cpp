#include <boost/assign.hpp>
#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vector>
#include "caffe/internode/configuration.hpp"
#include "caffe/serialization/BlobCodec.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {
namespace {

using ::testing::_;
using ::testing::Return;

TEST(BlobCodecTest, encode_4321_diff) {
  BlobUpdate msg;
  Blob<float> srcblob;
  vector<int> v = boost::assign::list_of(4)(3)(2)(1);
  srcblob.Reshape(v);
  vector<float> diff = boost::assign::list_of(999.99)(12.3)(0.1)(-3.3)
                                             (+2.0)(12.3)(10.2)(FLT_MAX)
                                             (+4.4)(12.3)(0.0)(-1.3)
                                             (+6.5)(12.3)(24.42)(1010.10)
                                             (FLT_MIN)(12.3)(66.6)(133.1)
                                             (12.4)(12.3)(0.0001)(100.3);

  ASSERT_EQ(diff.size(), srcblob.count());

  caffe_copy(srcblob.count(),
          &diff.front(),
          srcblob.mutable_cpu_diff());

  shared_ptr<BlobCodec<float> > codec = BlobCodec<float>::create_codec(
          MultinodeParameter::default_instance(), true);

  codec->encode(&msg, &srcblob, BlobEncoding::GRADS, msg.info().part());

  EXPECT_EQ(0, memcmp(msg.data().c_str(), &diff.front(),
           sizeof(float)*diff.size()));
}

TEST(BlobCodecTest, encode_decode_2222_data) {
  BlobUpdate msg;
  Blob<float> srcblob;
  Blob<float> dstblob;
  vector<int> v = boost::assign::list_of(2)(2)(2)(2);
  srcblob.Reshape(v);
  dstblob.Reshape(v);
  vector<float> data = boost::assign::list_of(1.1)(-2.2)(3.3)(5.5)
                                             (6.6)(-7.7)(8.8)(9.9)
                                             (13.13)(-12.12)(12.12)(11.11)
                                             (128.128)(-132.312)(1.1)(-10.10);
  vector<float> data_zero = boost::assign::list_of(0.0)(0.0)(0.0)(0.0)
                                                  (0.0)(0.0)(0.0)(0.0)
                                                  (0.0)(0.0)(0.0)(0.0)
                                                  (0.0)(0.0)(0.0)(0.0);
  ASSERT_EQ(data.size(), srcblob.count());
  ASSERT_EQ(data_zero.size(), dstblob.count());

  caffe_copy<float>(srcblob.count(),
          &data.front(),
          srcblob.mutable_cpu_data());
  caffe_copy<float>(dstblob.count(),
          &data_zero.front(),
          dstblob.mutable_cpu_diff());

  shared_ptr<BlobCodec<float> > codec = BlobCodec<float>::create_codec(
          MultinodeParameter::default_instance(), true);

  codec->encode(&msg, &srcblob, BlobEncoding::PARAMS, msg.info().part());
  codec->decode(msg,  &dstblob, BlobEncoding::PARAMS, 1.0f, 0.0f);

  EXPECT_EQ(0, memcmp(dstblob.cpu_data(), &data.front(),
          sizeof(float)*dstblob.count()));
}

TEST(BlobCodecTest, encode_8width_data_) {
  BlobUpdate msg;
  Blob<float> srcblob;
  vector<int> v = boost::assign::list_of(1)(1)(1)(8);
  srcblob.Reshape(v);
  vector<float> data = boost::assign::list_of(-0.0)(-0.3)(-2.2)(-3.3)
                                             (+0.0)(12.3)(10.2)(-1.3);

  ASSERT_EQ(data.size(), srcblob.count());

  caffe_copy<float>(srcblob.count(),
          &data.front(),
          srcblob.mutable_cpu_data());

  shared_ptr<BlobCodec<float> > codec = BlobCodec<float>::create_codec(
          MultinodeParameter::default_instance(), true);

  codec->encode(&msg, &srcblob, BlobEncoding::PARAMS, msg.info().part());

  EXPECT_EQ(0, memcmp(msg.data().c_str(), &data.front(),
          sizeof(float)*data.size()));
}

TEST(BlobCodecTest, encode_4width_diff) {
  BlobUpdate msg;
  Blob<float> srcblob;
  vector<int> v = boost::assign::list_of(1)(1)(1)(4);
  srcblob.Reshape(v);
  vector<float> diff = boost::assign::list_of(-0.0)(-99.99)(-0.3)(0.4);

  ASSERT_EQ(diff.size(), srcblob.count());

  caffe_copy<float>(srcblob.count(),
          &diff.front(),
          srcblob.mutable_cpu_diff());

  shared_ptr<BlobCodec<float> > codec = BlobCodec<float>::create_codec(
          MultinodeParameter::default_instance(), true);

  codec->encode(&msg, &srcblob, BlobEncoding::GRADS, msg.info().part());

  EXPECT_EQ(0, memcmp(msg.data().c_str(), &diff.front(),
          sizeof(float)*diff.size()));
}

TEST(BlobCodecTest, encode_decode_4width_diff) {
  BlobUpdate msg;
  Blob<float> srcblob;
  Blob<float> dstblob;
  vector<int> v = boost::assign::list_of(1)(1)(1)(4);
  srcblob.Reshape(v);
  dstblob.Reshape(v);
  vector<float> diff_zero = boost::assign::list_of(0.0)(0.0)(0.0)(0.0);
  vector<float> diff = boost::assign::list_of(1.0)(2.2)(3.3)(4.4);

  ASSERT_EQ(diff.size(), srcblob.count());
  ASSERT_EQ(diff_zero.size(), srcblob.count());

  caffe_copy<float>(srcblob.count(),
          &diff.front(),
          srcblob.mutable_cpu_diff());
  caffe_copy<float>(dstblob.count(),
          &diff_zero.front(),
          dstblob.mutable_cpu_diff());

  shared_ptr<BlobCodec<float> > codec = BlobCodec<float>::create_codec(
           MultinodeParameter::default_instance(), true);

  codec->encode(&msg, &srcblob, BlobEncoding::GRADS, msg.info().part());
  codec->decode(msg,  &dstblob, BlobEncoding::GRADS, 1.0f, 0.0f);

  EXPECT_EQ(0, memcmp(dstblob.cpu_diff(), &diff.front(),
          sizeof(float)*dstblob.count()));
}

TEST(BlobCodecTest, encode_decode_4width_data) {
  BlobUpdate msg;
  Blob<float> srcblob;
  Blob<float> dstblob;
  vector<int> v = boost::assign::list_of(1)(1)(1)(4);
  srcblob.Reshape(v);
  dstblob.Reshape(v);
  vector<float> data_zero = boost::assign::list_of(0.0)(0.0)(0.0)(0.0);
  vector<float> data = boost::assign::list_of(4.0)(3.2)(2.3)(1.4);

  ASSERT_EQ(data.size(), srcblob.count());
  ASSERT_EQ(data_zero.size(), srcblob.count());

  caffe_copy<float>(srcblob.count(),
          &data.front(),
          srcblob.mutable_cpu_data());
  caffe_copy<float>(dstblob.count(),
          &data_zero.front(),
          dstblob.mutable_cpu_data());

  shared_ptr<BlobCodec<float> > codec = BlobCodec<float>::create_codec(
          MultinodeParameter::default_instance(), true);

  codec->encode(&msg, &srcblob, BlobEncoding::PARAMS, msg.info().part());
  codec->decode(msg,  &dstblob, BlobEncoding::PARAMS, 1.0f, 0.0f);

  EXPECT_EQ(0, memcmp(dstblob.cpu_data(), &data.front(),
          sizeof(float)*dstblob.count()));
}

TEST(BlobCodecTest, encode_decode_4width_data_alpha_0_5) {
  BlobUpdate msg;
  Blob<float> srcblob;
  Blob<float> dstblob;
  vector<int> v = boost::assign::list_of(1)(1)(1)(4);
  srcblob.Reshape(v);
  dstblob.Reshape(v);
  vector<float> data_zero = boost::assign::list_of(0.0)(0.0)(0.0)(0.0);
  vector<float> data = boost::assign::list_of(4.0)(3.2)(2.4)(1.4);
  vector<float> data_expected = boost::assign::list_of(2.0)(1.6)(1.2)(0.7);

  ASSERT_EQ(data.size(), srcblob.count());
  ASSERT_EQ(data_zero.size(), srcblob.count());

  caffe_copy<float>(srcblob.count(),
          &data.front(),
          srcblob.mutable_cpu_data());
  caffe_copy<float>(dstblob.count(),
          &data_zero.front(),
          dstblob.mutable_cpu_data());

  shared_ptr<BlobCodec<float> > codec = BlobCodec<float>::create_codec(
          MultinodeParameter::default_instance(), true);

  codec->encode(&msg, &srcblob, BlobEncoding::PARAMS, msg.info().part());
  codec->decode(msg,  &dstblob, BlobEncoding::PARAMS, 0.5f, 0.0f);

  EXPECT_EQ(0, memcmp(dstblob.cpu_data(), &data_expected.front(),
          sizeof(float)*dstblob.count()));
}

TEST(BlobCodecTest, encode_decode_4width_data_beta_0_5) {
  BlobUpdate msg;
  Blob<float> srcblob;
  Blob<float> dstblob;
  vector<int> v = boost::assign::list_of(1)(1)(1)(4);
  srcblob.Reshape(v);
  dstblob.Reshape(v);
  vector<float> data_one = boost::assign::list_of(1.0)(1.0)(1.0)(1.0);
  vector<float> data = boost::assign::list_of(4.0)(3.2)(2.4)(1.4);
  vector<float> data_expected = boost::assign::list_of(4.5)(3.7)(2.9)(1.9);

  ASSERT_EQ(data.size(), srcblob.count());
  ASSERT_EQ(data_one.size(), srcblob.count());

  caffe_copy<float>(srcblob.count(), &data.front(),
          srcblob.mutable_cpu_data());
  caffe_copy<float>(dstblob.count(), &data_one.front(),
          dstblob.mutable_cpu_data());

  shared_ptr<BlobCodec<float> > codec = BlobCodec<float>::create_codec(
          MultinodeParameter::default_instance(), true);

  codec->encode(&msg, &srcblob, BlobEncoding::PARAMS, msg.info().part());
  codec->decode(msg,  &dstblob, BlobEncoding::PARAMS, 1.0f, 0.5f);

  EXPECT_EQ(0, memcmp(dstblob.cpu_data(), &data_expected.front(),
          sizeof(float)*dstblob.count()));
}

TEST(BlobCodecTest, encode_decode_4width_data_alpha_0_5_beta_0_5) {
  BlobUpdate msg;
  Blob<float> srcblob;
  Blob<float> dstblob;
  vector<int> v = boost::assign::list_of(1)(1)(1)(4);
  srcblob.Reshape(v);
  dstblob.Reshape(v);
  vector<float> data_one = boost::assign::list_of(1.0)(1.0)(1.0)(1.0);
  vector<float> data = boost::assign::list_of(4.0)(3.2)(2.4)(1.4);
  vector<float> data_expected = boost::assign::list_of(2.5)(2.1)(1.7)(1.2);

  ASSERT_EQ(data.size(), srcblob.count());
  ASSERT_EQ(data_one.size(), srcblob.count());

  caffe_copy<float>(srcblob.count(), &data.front(),
          srcblob.mutable_cpu_data());
  caffe_copy<float>(dstblob.count(), &data_one.front(),
          dstblob.mutable_cpu_data());

  shared_ptr<BlobCodec<float> > codec = BlobCodec<float>::create_codec(
          MultinodeParameter::default_instance(), true);

  codec->encode(&msg, &srcblob, BlobEncoding::PARAMS, msg.info().part());
  codec->decode(msg,  &dstblob, BlobEncoding::PARAMS, 0.5f, 0.5f);

  EXPECT_EQ(0, memcmp(dstblob.cpu_data(), &data_expected.front(),
          sizeof(float)*dstblob.count()));
}

TEST(BlobCodecTest, encode_decode_4width_data_alpha_0_beta_1) {
  BlobUpdate msg;
  Blob<float> srcblob;
  Blob<float> dstblob;
  vector<int> v = boost::assign::list_of(1)(1)(1)(4);
  srcblob.Reshape(v);
  dstblob.Reshape(v);
  vector<float> data_one = boost::assign::list_of(1.1)(2.3)(1.4)(0.01);
  vector<float> data = boost::assign::list_of(4.0)(3.2)(2.4)(1.4);
  vector<float> data_expected = boost::assign::list_of(1.1)(2.3)(1.4)(0.01);

  ASSERT_EQ(data.size(), srcblob.count());
  ASSERT_EQ(data_one.size(), srcblob.count());

  caffe_copy<float>(srcblob.count(), &data.front(),
          srcblob.mutable_cpu_data());
  caffe_copy<float>(dstblob.count(), &data_one.front(),
          dstblob.mutable_cpu_data());

  shared_ptr<BlobCodec<float> > codec = BlobCodec<float>::create_codec(
          MultinodeParameter::default_instance(), true);

  codec->encode(&msg, &srcblob, BlobEncoding::PARAMS, msg.info().part());
  codec->decode(msg,  &dstblob, BlobEncoding::PARAMS, 0.0f, 1.0f);

  EXPECT_EQ(0, memcmp(dstblob.cpu_data(), &data_expected.front(),
          sizeof(float)*dstblob.count()));
}


}  // namespace
}  // namespace caffe

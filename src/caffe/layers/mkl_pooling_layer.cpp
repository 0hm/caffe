#ifdef MKL2017_SUPPORTED
#include <algorithm>
#include <cfloat>
#include <vector>

#include "caffe/common.hpp"
#include "caffe/layer.hpp"
#include "caffe/layers/mkl_layers.hpp"
#include "caffe/syncedmem.hpp"
#include "caffe/util/math_functions.hpp"


namespace caffe {
template <typename Dtype>
MKLPoolingLayer<Dtype>::~MKLPoolingLayer() {
  dnnDelete<Dtype>(poolingFwd);
  dnnDelete<Dtype>(poolingBwd);
}

template <typename Dtype>
void MKLPoolingLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  PoolingParameter pool_param = this->layer_param_.pooling_param();

  if (pool_param.global_pooling()) {
    CHECK(!(pool_param.has_kernel_size() ||
      pool_param.has_kernel_h() || pool_param.has_kernel_w()))
      << "With Global_pooling: true Filter size cannot specified";
  } else {
    CHECK(!pool_param.has_kernel_size() !=
      !(pool_param.has_kernel_h() && pool_param.has_kernel_w()))
      << "Filter size is kernel_size OR kernel_h and kernel_w; not both";
    CHECK(pool_param.has_kernel_size() ||
      (pool_param.has_kernel_h() && pool_param.has_kernel_w()))
      << "For non-square filters both kernel_h and kernel_w are required.";
  }
  CHECK((!pool_param.has_pad() && pool_param.has_pad_h()
      && pool_param.has_pad_w())
      || (!pool_param.has_pad_h() && !pool_param.has_pad_w()))
      << "pad is pad OR pad_h and pad_w are required.";
  CHECK((!pool_param.has_stride() && pool_param.has_stride_h()
      && pool_param.has_stride_w())
      || (!pool_param.has_stride_h() && !pool_param.has_stride_w()))
      << "Stride is stride OR stride_h and stride_w are required.";
  global_pooling_ = pool_param.global_pooling();
  if (global_pooling_) {
    kernel_h_ = bottom[0]->height();
    kernel_w_ = bottom[0]->width();
  } else {
    if (pool_param.has_kernel_size()) {
      kernel_h_ = kernel_w_ = pool_param.kernel_size();
    } else {
      kernel_h_ = pool_param.kernel_h();
      kernel_w_ = pool_param.kernel_w();
    }
  }
  CHECK_GT(kernel_h_, 0) << "Filter dimensions cannot be zero.";
  CHECK_GT(kernel_w_, 0) << "Filter dimensions cannot be zero.";
  if (!pool_param.has_pad_h()) {
    pad_h_ = pad_w_ = pool_param.pad();
  } else {
    pad_h_ = pool_param.pad_h();
    pad_w_ = pool_param.pad_w();
  }
  if (!pool_param.has_stride_h()) {
    stride_h_ = stride_w_ = pool_param.stride();
  } else {
    stride_h_ = pool_param.stride_h();
    stride_w_ = pool_param.stride_w();
  }
  if (global_pooling_) {
    CHECK(pad_h_ == 0 && pad_w_ == 0 && stride_h_ == 1 && stride_w_ == 1)
      << "With Global_pooling: true; only pad = 0 and stride = 1";
  }
  if (pad_h_ != 0 || pad_w_ != 0) {
    CHECK(this->layer_param_.pooling_param().pool()
        == PoolingParameter_PoolMethod_AVE
        || this->layer_param_.pooling_param().pool()
        == PoolingParameter_PoolMethod_MAX)
        << "Padding implemented only for average and max pooling.";
    CHECK_LT(pad_h_, kernel_h_);
    CHECK_LT(pad_w_, kernel_w_);
  }

  pooled_height_ = static_cast<int>(ceil(static_cast<float>(
      bottom[0]->height() + 2 * pad_h_ - kernel_h_) / stride_h_)) + 1;
  pooled_width_ = static_cast<int>(ceil(static_cast<float>(
      bottom[0]->height() + 2 * pad_w_ - kernel_w_) / stride_w_)) + 1;
  if (pad_h_ || pad_w_) {
    // If we have padding, ensure that the last pooling starts strictly
    // inside the image (instead of at the padding); otherwise clip the last.
    if ((pooled_height_ - 1) * stride_h_ >= bottom[0]->height() + pad_h_) {
      --pooled_height_;
    }
    if ((pooled_width_ - 1) * stride_w_ >= bottom[0]->height() + pad_w_) {
      --pooled_width_;
    }
    CHECK_LT((pooled_height_ - 1) * stride_h_, bottom[0]->height() + pad_h_);
    CHECK_LT((pooled_width_ - 1) * stride_w_, bottom[0]->height() + pad_w_);
  }

  size_t dim = 4;
  size_t src_sizes[4], src_strides[4];
  size_t dst_sizes[4], dst_strides[4];

  src_sizes[0] = bottom[0]->width();
  src_sizes[1] = bottom[0]->height();
  src_sizes[2] = bottom[0]->channels();
  src_sizes[3] = bottom[0]->num();

  src_strides[0] = 1;
  src_strides[1] = src_sizes[0];
  src_strides[2] = src_sizes[0]*src_sizes[1];
  src_strides[3] = src_sizes[0]*src_sizes[1]*src_sizes[2];

  dst_sizes[0] = pooled_width_;
  dst_sizes[1] = pooled_height_;
  dst_sizes[2] = src_sizes[2];
  dst_sizes[3] = src_sizes[3];

  dst_strides[0] = 1;
  dst_strides[1] = dst_sizes[0];
  dst_strides[2] = dst_sizes[0]*dst_sizes[1];
  dst_strides[3] = dst_sizes[0]*dst_sizes[1]*dst_sizes[2];

  src_offset[0] = -pad_w_;
  src_offset[1] = -pad_h_;

  kernel_stride[0] = stride_w_;
  kernel_stride[1] = stride_h_;

  kernel_size[0] = kernel_w_;
  kernel_size[1] = kernel_h_;

  dnnError_t e;
  e = dnnLayoutCreate<Dtype>(&fwd_bottom_data->layout_usr, dim, src_sizes,
          src_strides);
  CHECK_EQ(e, E_SUCCESS);
  e = dnnLayoutCreate<Dtype>(&fwd_top_data->layout_usr, dim, dst_sizes,
          dst_strides);
  CHECK_EQ(e, E_SUCCESS);
  e = dnnLayoutCreate<Dtype>(&bwd_bottom_diff->layout_usr, dim, src_sizes,
          src_strides);
  CHECK_EQ(e, E_SUCCESS);
  e = dnnLayoutCreate<Dtype>(&bwd_top_diff->layout_usr, dim, dst_sizes,
          dst_strides);
  CHECK_EQ(e, E_SUCCESS);

  // Names are for debugging only
  fwd_bottom_data->name = "fwd_bottom_data   @ " + this->layer_param_.name();
  fwd_top_data->name =    "fwd_top_data      @ " + this->layer_param_.name();
  bwd_top_diff->name =    "bwd_top_diff      @ " + this->layer_param_.name();
  bwd_bottom_diff->name = "bwd_bottom_diff   @ " + this->layer_param_.name();

  // Primitives will be allocated during the first fwd pass
  poolingFwd = NULL;
  poolingBwd = NULL;
}

template <typename Dtype>
void MKLPoolingLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  CHECK_EQ(4, bottom[0]->num_axes()) << "Input must have 4 axes, "
      << "corresponding to (num, channels, height, width)";
  channels_ = bottom[0]->channels();
  height_ = bottom[0]->height();
  width_ = bottom[0]->width();
  if (global_pooling_) {
    kernel_h_ = bottom[0]->height();
    kernel_w_ = bottom[0]->width();
  }
  pooled_height_ = static_cast<int>(ceil(static_cast<float>(
      height_ + 2 * pad_h_ - kernel_h_) / stride_h_)) + 1;
  pooled_width_ = static_cast<int>(ceil(static_cast<float>(
      width_ + 2 * pad_w_ - kernel_w_) / stride_w_)) + 1;
  if (pad_h_ || pad_w_) {
    // If we have padding, ensure that the last pooling starts strictly
    // inside the image (instead of at the padding); otherwise clip the last.
    if ((pooled_height_ - 1) * stride_h_ >= height_ + pad_h_) {
      --pooled_height_;
    }
    if ((pooled_width_ - 1) * stride_w_ >= width_ + pad_w_) {
      --pooled_width_;
    }
    CHECK_LT((pooled_height_ - 1) * stride_h_, height_ + pad_h_);
    CHECK_LT((pooled_width_ - 1) * stride_w_, width_ + pad_w_);
  }
  top[0]->Reshape(bottom[0]->num(), channels_, pooled_height_,
      pooled_width_);
  if (top.size() > 1) {
    (reinterpret_cast<Blob<size_t>* > (top[1]) )->Reshape(bottom[0]->num(),
            channels_, pooled_height_, pooled_width_);
  }
  // If max pooling, we will initialize the vector index part.
  if (this->layer_param_.pooling_param().pool() ==
      PoolingParameter_PoolMethod_MAX && top.size() == 1) {
    max_idx_.Reshape(bottom[0]->num(), channels_, pooled_height_,
            pooled_width_);
  }
  // If stochastic pooling, we will initialize the random index part.
  if (this->layer_param_.pooling_param().pool() ==
      PoolingParameter_PoolMethod_STOCHASTIC) {
    rand_idx_.Reshape(bottom[0]->num(), channels_, pooled_height_,
      pooled_width_);
  }
}


// TODO(Yangqing): Is there a faster way to do pooling in the channel-first
// case?
template <typename Dtype>
void MKLPoolingLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  // printf(" len(top_data) = %i\n", sizeof(top_data)/sizeof(Dtype));
  const int top_count = top[0]->count();
  // We'll output the mask to top[1] if it's of size >1.
  size_t* mask = NULL;  // suppress warnings about uninitalized variables

  // We'll output the mask to top[1] if it's of size >1.
  const bool use_top_mask = top.size() > 1;

  switch (this->layer_param_.pooling_param().pool()) {
  case PoolingParameter_PoolMethod_MAX:
    {
    dnnError_t status;
    void* pooling_res[dnnResourceNumber];
    mask = (use_top_mask) ?
      reinterpret_cast<size_t*>(top[1]->mutable_cpu_data()) :
      (max_idx_.mutable_cpu_data());

    caffe_set<size_t>(top_count, -1, mask);
    pooling_res[dnnResourceWorkspace] = reinterpret_cast<void*>(mask);

    void* bottom_data =
      reinterpret_cast<void *>(const_cast<Dtype*>(bottom[0]->prv_data()));
    if (NULL == bottom_data) {
      bottom_data =
        reinterpret_cast<void *>(const_cast<Dtype*>(bottom[0]->cpu_data()));
      if (NULL == poolingFwd) {
        // Now create poolingFwd
        status = dnnPoolingCreateForward<Dtype>(&poolingFwd, NULL,
                dnnAlgorithmPoolingMax, fwd_bottom_data->layout_usr,
                kernel_size, kernel_stride, src_offset, dnnBorderZeros);
        CHECK_EQ(status, E_SUCCESS);

        // Now create poolingBwd
        status = dnnPoolingCreateBackward<Dtype>(&poolingBwd, NULL,
                dnnAlgorithmPoolingMax, fwd_bottom_data->layout_usr,
                kernel_size, kernel_stride, src_offset, dnnBorderZeros);
        CHECK_EQ(status, E_SUCCESS);
      }
    } else if (NULL == poolingFwd) {
      // Is it the first pass? Create a primitive.
      CHECK_EQ((bottom[0]->get_prv_descriptor_data())->get_descr_type(),
              PrvMemDescr::PRV_DESCR_MKL2017);
      shared_ptr<MKLData<Dtype> > mem_descr
        =  boost::static_pointer_cast<MKLData<Dtype> >
              (bottom[0]->get_prv_descriptor_data());
      CHECK(mem_descr != NULL);

      DLOG(INFO) << "Using layout of " << mem_descr->name
              << " as input layout for " << this->layer_param_.name();

      // copy shared_ptr
      fwd_bottom_data = mem_descr;

      // Now create poolingFwd
      status = dnnPoolingCreateForward<Dtype>(&poolingFwd, NULL,
              dnnAlgorithmPoolingMax, fwd_bottom_data->layout_int, kernel_size,
              kernel_stride, src_offset, dnnBorderZeros);
      CHECK_EQ(status, E_SUCCESS);

      status = dnnLayoutCreateFromPrimitive<Dtype>(&fwd_top_data->layout_int,
              poolingFwd, dnnResourceDst);
      CHECK_EQ(status, 0) << "Failed dnnLayoutCreateFromPrimitive with status "
              << status << "\n";

      fwd_top_data->create_conversions();

      // Now create poolingBwd
      status = dnnPoolingCreateBackward<Dtype>(&poolingBwd, NULL,
              dnnAlgorithmPoolingMax, fwd_bottom_data->layout_int, kernel_size,
              kernel_stride, src_offset, dnnBorderZeros);
      CHECK_EQ(status, E_SUCCESS);

      status = dnnLayoutCreateFromPrimitive<Dtype>(&bwd_top_diff->layout_int,
              poolingFwd, dnnResourceDst);
      CHECK_EQ(status, E_SUCCESS);

      status = dnnLayoutCreateFromPrimitive<Dtype>(&bwd_bottom_diff->layout_int,
              poolingFwd, dnnResourceSrc);
      CHECK_EQ(status, E_SUCCESS);

      bwd_top_diff->create_conversions();
      bwd_bottom_diff->create_conversions();
    }

    pooling_res[dnnResourceSrc] = bottom_data;
    if (fwd_top_data->convert_from_int) {
      top[0]->set_prv_data(fwd_top_data->internal_ptr, fwd_top_data, false);
      pooling_res[dnnResourceDst] =reinterpret_cast<void *>(
              const_cast<Dtype*>(fwd_top_data->internal_ptr));
    } else {
      pooling_res[dnnResourceDst] =
              reinterpret_cast<void *>(top[0]->mutable_cpu_data());
      DLOG(INFO) << "Using cpu_data for top in DnnPooling.";
    }
    status = dnnExecute<Dtype>(poolingFwd, pooling_res);
    CHECK_EQ(status, E_SUCCESS);
    }
    break;
  case PoolingParameter_PoolMethod_AVE:
    NOT_IMPLEMENTED;
    break;
  case PoolingParameter_PoolMethod_STOCHASTIC:
    NOT_IMPLEMENTED;
    break;
  default:
    LOG(FATAL) << "Unknown pooling method.";
  }
}

template <typename Dtype>
void MKLPoolingLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  if (!propagate_down[0]) {
    return;
  }
  // Different pooling methods. We explicitly do the switch outside the for
  // loop to save time, although this results in more codes.

  const size_t* mask = NULL;  // suppress warnings about uninitialized variables

  switch (this->layer_param_.pooling_param().pool()) {
  case PoolingParameter_PoolMethod_MAX:
    // The main loop
    mask = (top.size() > 1) ?
      reinterpret_cast<const size_t*>(top[1]->cpu_data()) :
      (max_idx_.cpu_data());

    dnnError_t e;
    void* pooling_res[dnnResourceNumber];

    pooling_res[dnnResourceWorkspace] =
            reinterpret_cast<void *>(const_cast<size_t*>(mask));
    pooling_res[dnnResourceDiffDst] = bwd_top_diff->get_converted_prv(top[0],
            true);

    if (bwd_bottom_diff->convert_from_int) {
      bottom[0]->set_prv_diff(bwd_bottom_diff->internal_ptr, bwd_bottom_diff,
              false);
      pooling_res[dnnResourceDiffSrc] =
              reinterpret_cast<void *>(bwd_bottom_diff->internal_ptr);
    } else {
      pooling_res[dnnResourceDiffSrc] = bottom[0]->mutable_cpu_diff();
    }
    caffe_set(bottom[0]->count(), Dtype(0),
            reinterpret_cast<Dtype *>(pooling_res[dnnResourceDiffSrc]));

    e = dnnExecute<Dtype>(poolingBwd, pooling_res);
    CHECK_EQ(e, E_SUCCESS);

    break;
  case PoolingParameter_PoolMethod_AVE:
    NOT_IMPLEMENTED;
    break;
  case PoolingParameter_PoolMethod_STOCHASTIC:
    NOT_IMPLEMENTED;
    break;
  default:
    LOG(FATAL) << "Unknown pooling method.";
  }
}


#ifdef CPU_ONLY
STUB_GPU(MKLPoolingLayer);
#else
template <typename Dtype>
void MKLPoolingLayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {NOT_IMPLEMENTED;}
template <typename Dtype>
void MKLPoolingLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom)
  {NOT_IMPLEMENTED;}
#endif

INSTANTIATE_CLASS(MKLPoolingLayer);
}  // namespace caffe
#endif  // #ifdef MKL2017_SUPPORTED

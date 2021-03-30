/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#if GOOGLE_CUDA
#define EIGEN_USE_GPU
#endif  // GOOGLE_CUDA

#include "time_two.h"
#include "tensorflow/core/framework/op_kernel.h"


#include <cmath>
const float verybig = INFINITY;


namespace tensorflow {

typedef Eigen::ThreadPoolDevice CPUDevice;
typedef Eigen::GpuDevice GPUDevice;

namespace functor {

#define calcint(q,Q,f,F) ((f+q*q) - (F+Q*Q)) / (2*q - 2*Q);

// CPU specialization of actual computation.
template <typename T,typename S>
struct BasinFinderFunctor<CPUDevice, T,S> {
  void operator()(const CPUDevice& d, int dim0, int dim1, int dim2, const T* f, T* out, T* z, S* v, S* basins) {
    for (int i0 = 0; i0< dim0; i0++) {
      for(int i2 =0; i2<dim2; i2++) {
        const int offset1= i0*dim1*dim2+i2;
        const int offset2= i0*(dim1+1)*dim2+i2;

        // initialize v,z
        for(int i1=0; i1<dim1; i1++) {
          v[offset1+i1*dim2]=0;
          z[offset2+i1*dim2]=0;
        }
        z[offset2+dim1*dim2]=0;

        // compute lower parabolas
        int k=0;
        z[offset2+0]=-verybig;
        z[offset2+dim2]=verybig;

        for(int q=1; q<dim1; q++) {
              //printf("%d %d %d :: %d %d \n",i0,i2,q,offset1+k*dim2,offset1+v[offset1+k*dim2]*dim2);
              float s=calcint(q,v[offset1+k*dim2],f[offset1+q*dim2],f[offset1+v[offset1+k*dim2]*dim2]);
              //printf("%d %d %d :: %d %d %f \n",i0,i2,q,offset1+k*dim2,offset1+v[offset1+k*dim2]*dim2,s);

              while(s<=z[offset2+k*dim2]){
                  k=k-1;
                  //printf("%d %d %d :: %d %d \n",i0,i2,q,offset1+k*dim2,offset1+v[offset1+k*dim2]*dim2);
                  s=calcint(q,v[offset1+k*dim2],f[offset1+q*dim2],f[offset1+v[offset1+k*dim2]*dim2]);
                  //printf("%d %d %d :: %d %d %f \n",i0,i2,q,offset1+k*dim2,offset1+v[offset1+k*dim2]*dim2,s);
              }
              k=k+1;
              v[offset1+k*dim2]=q;
              z[offset2+k*dim2]=s;
              z[offset2+k*dim2+dim2]=verybig;
        }

        // compute basins and out
        k=0;
        for(int q=0; q<dim1; q++) {
          while(z[offset2+(k+1)*dim2]<q) {
            k=k+1;
          }
          int thisv=v[offset1+k*dim2];
          basins[offset1+q*dim2]=thisv;
          out[offset1+q*dim2] = (q-thisv)*(q-thisv) + f[offset1+thisv*dim2];
        }
      }
    }

  }
};

// OpKernel definition.
// template parameter <T> is the datatype of the tensors.
template <typename Device, typename T, typename S>
class BasinFinderOp : public OpKernel {
 public:
  explicit BasinFinderOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* context) override {
    // Grab the input tensor
    const Tensor& input_tensor = context->input(0);

    OP_REQUIRES(context, input_tensor.dims() == 3,errors::InvalidArgument("input should be a 3-tensor"));

    // Create an output tensors
    Tensor* output_tensor = NULL;
    OP_REQUIRES_OK(context, context->allocate_output(0, input_tensor.shape(),
                                                     &output_tensor));


    Tensor* z_tensor = NULL;
    TensorShape z_shape;
    z_shape.AddDim(input_tensor.dim_size(0));
    z_shape.AddDim(input_tensor.dim_size(1)+1);
    z_shape.AddDim(input_tensor.dim_size(2));
    OP_REQUIRES_OK(context, context->allocate_output(1, z_shape,&z_tensor));


    Tensor* v_tensor = NULL;
    OP_REQUIRES_OK(context, context->allocate_output(2, input_tensor.shape(),
                                                     &v_tensor));
    Tensor* basins_tensor = NULL;
    OP_REQUIRES_OK(context, context->allocate_output(3, input_tensor.shape(),
                                                     &basins_tensor));


    // Do the computation.
    const int dim0=static_cast<int>(input_tensor.dim_size(0));
    const int dim1=static_cast<int>(input_tensor.dim_size(1));
    const int dim2=static_cast<int>(input_tensor.dim_size(2));

    OP_REQUIRES(context, input_tensor.NumElements() <= tensorflow::kint32max,
                errors::InvalidArgument("Too many elements in tensor"));
    BasinFinderFunctor<Device, T,S>()(
        context->eigen_device<Device>(),
        dim0,dim1,dim2,
        input_tensor.flat<T>().data(),
        output_tensor->flat<T>().data(),
        z_tensor->flat<T>().data(),
        v_tensor->flat<S>().data(),
        basins_tensor->flat<S>().data()
      );
  }
};

// Register the CPU kernels.
#define REGISTER_CPU(T)                                          \
  REGISTER_KERNEL_BUILDER(                                       \
      Name("BasinFinder").Device(DEVICE_CPU).TypeConstraint<T>("T"), \
      BasinFinderOp<CPUDevice, T,int32>);
REGISTER_CPU(float);

// Register the GPU kernels.
#ifdef GOOGLE_CUDA
#define REGISTER_GPU(T,S)                                          \
  extern template struct BasinFinderFunctor<GPUDevice, T,S>;           \
  REGISTER_KERNEL_BUILDER(                                       \
      Name("BasinFinder").Device(DEVICE_GPU).TypeConstraint<T>("T"), \
      BasinFinderOp<GPUDevice, T,S>);
REGISTER_GPU(float,int32);
#endif  // GOOGLE_CUDA
}
}  // namespace tensorflow
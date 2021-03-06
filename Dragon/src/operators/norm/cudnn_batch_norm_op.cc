#include "operators/norm/batch_norm_op.h"
#include "core/workspace.h"
#include "utils/filler.h"

#ifdef WITH_CUDNN

#if CUDNN_VERSION_MIN(5, 0, 0)

namespace dragon {

template <class Context> template <typename T>
void CuDNNBatchNormOp<Context>::RunWithType() {
    //  determine the bn desc
    if (Input(0).ndim() == 2) {
        bn_mode = CUDNN_BATCHNORM_PER_ACTIVATION;
        Tensor x_reshape;
        x_reshape.Reshape(vector<TIndex>({ N, C, 1, 1 }));
        cudnnSetTensorDesc<T>(&input_desc, &x_reshape);
        cudnnSetTensorDesc<T>(&output_desc, &x_reshape);
    } else {
        CHECK_GE((int)Input(0).ndim(), 3)
            << "The number of dimensions should be at least 3.";
        bn_mode = CUDNN_BATCHNORM_SPATIAL;
#if CUDNN_VERSION_MIN(7, 0, 0)
        if (!this->use_global_stats) 
            bn_mode = CUDNN_BATCHNORM_SPATIAL_PERSISTENT;
#endif
        if (data_format == "NCHW") {
            cudnnSetTensorDesc<T>(&input_desc, &Input(0));
            cudnnSetTensorDesc<T>(&output_desc, Output(0));
        }
        else if (data_format == "NHWC") {
            switch (Input(0).ndim()) {
            case 3:
                cudnnSetTensor3dDesc<T>(&input_desc, data_format, &Input(0));
                cudnnSetTensor3dDesc<T>(&output_desc, data_format, Output(0));
            case 4:
                cudnnSetTensor4dDesc<T>(&input_desc, data_format, &Input(0));
                cudnnSetTensor4dDesc<T>(&output_desc, data_format, Output(0));
            case 5:
                cudnnSetTensor5dDesc<T>(&input_desc, data_format, &Input(0));
                cudnnSetTensor5dDesc<T>(&output_desc, data_format, Output(0));
            default:
                LOG(FATAL) << "Only support the 3d/4d/5d input at NHWC mode.";
            }
        }
    }
    //  derive the bn desc
    CUDNN_CHECK(cudnnDeriveBNTensorDescriptor(bn_desc, input_desc, bn_mode));

    TENSOR_FILL(Input(1), vector<TIndex>(1, C));  //  history_mean
    TENSOR_FILL(Input(2), vector<TIndex>(1, C));  //  history_var
    TENSOR_FILL(Input(3), vector<TIndex>(1, C));  //  scale
    TENSOR_FILL(Input(4), vector<TIndex>(1, C));  //  bias

    auto* Xdata = Input(0).template data<T, Context>();
    auto* Ydata = Output(0)->template mutable_data<T, Context>();

    auto* hMean_data = Input(1).template mutable_data<T, Context>();
    auto* hVar_data = Input(2).template mutable_data<T, Context>();
    auto* Sdata = Input(3).template data<T, Context>();
    auto* Bdata = Input(4).template data<T, Context>();

    if (this->use_global_stats) {
        CUDNN_CHECK(cudnnBatchNormalizationForwardInference(cudnn_handle(),
                                                                   bn_mode,
                                                         CUDNNType<T>::one,
                                                        CUDNNType<T>::zero,
                                                         input_desc, Xdata,
                                                        output_desc, Ydata,
                                                                   bn_desc,
                                                                     Sdata,
                                                                     Bdata,
                                                                hMean_data,
                                                                 hVar_data,
                                                               this->eps));

    } else {
        auto* tMean_data = mean->template mutable_data<T, Context>();
        auto* tVar_data = var->template mutable_data<T, Context>();
        CUDNN_CHECK(cudnnBatchNormalizationForwardTraining(cudnn_handle(),
                                                                  bn_mode,
                                                        CUDNNType<T>::one,
                                                       CUDNNType<T>::zero,
                                                        input_desc, Xdata,
                                                       output_desc, Ydata,
                                                                  bn_desc,
                                                                    Sdata,
                                                                    Bdata,
                        this->is_recomputing ? 0.0 : 1.0 - this->momentum,
                                                               hMean_data,
                                                                hVar_data,
                                                                this->eps,
                                                               tMean_data,
                                                              tVar_data));
    }
}

template <class Context>
void CuDNNBatchNormOp<Context>::Setup() {
    //  determine the mode
    if (this->use_stats == -1) {
        this->use_global_stats = phase() == "TEST" ? true : false;
    } else {
        this->use_global_stats = this->use_stats == 1 ? true : false;
    }
    this->is_recomputing = ws()->GetTensor("/opt/mirror_stage/recompute_flag")
                               ->template data<bool, CPUContext>()[0];

    //  determine the data format
    TIndex channel_axis = this->axis;
    data_format = "NCHW";
    if (channel_axis == -1) channel_axis += (int)Input(0).ndim();
    if (channel_axis + 1 == (int)Input(0).ndim()) data_format = "NHWC";
    N = Input(0).dim(0);
    C = Input(0).dim(channel_axis);

    //  make resource
    mean = ws()->CreateTensor("/mnt/" + Anchor() + "/bn/mean");
    var = ws()->CreateTensor("/mnt/" + Anchor() + "/bn/var");

    //  reshape
    mean->Reshape(vector<TIndex>(1, C));
    var->Reshape(vector<TIndex>(1, C));
    Output(0)->ReshapeLike(Input(0));
}

template <class Context>
void CuDNNBatchNormOp<Context>::RunOnDevice() {
    Setup();

    if (Input(0).template IsType<float>()) RunWithType<float>();
#ifdef WITH_CUDA_FP16
    else if (Input(0).template IsType<float16>()) RunWithType<float16>();
#endif
    else LOG(FATAL) << "Unsupported input types.";
}

REGISTER_CUDNN_OPERATOR(FusedBatchNorm, CuDNNBatchNormOp<CUDAContext>);
INSTANTIATE_CUDNN_OPERATOR(BatchNorm);

template <class Context>
void CuDNNBatchNormGradientOp<Context>::Setup() {
    //  determine the mode
    if (this->use_stats == -1) {
        this->use_global_stats = phase() == "TEST" ? true : false;
    } else {
        this->use_global_stats = this->use_stats == 1 ? true : false;
    }

    //  determine the data format
    TIndex channel_axis = this->axis;
    data_format = "NCHW";
    if (channel_axis == -1) channel_axis += (int)Input(0).ndim();
    if (channel_axis + 1 == (int)Input(0).ndim()) data_format = "NHWC";
    N = Input(0).dim(0);
    C = Input(0).dim(channel_axis);
    NC = N * C;
    S = Input(0).count() / NC;
    NS = N * S;

    //  make resource
    mean = ws()->GetTensor("/mnt/" + Anchor() + "/bn/mean");
    var = ws()->GetTensor("/mnt/" + Anchor() + "/bn/var");

    //  reshape
    mean->Reshape(vector<TIndex>(1, C));
    var->Reshape(vector<TIndex>(1, C));
    num_by_chans.Reshape(vector<TIndex>(1, NC));
    Output(0)->ReshapeLike(Input(0));  // dX
    Output(1)->ReshapeLike(Input(3));  // dScale
    Output(2)->ReshapeLike(Input(3));  // dBias
}

template <class Context> template <typename T>
void CuDNNBatchNormGradientOp<Context>::TrainingRunWithType() {
    //  determine the bn desc
    if (Input(0).ndim() == 2) {
        bn_mode = CUDNN_BATCHNORM_PER_ACTIVATION;
        Tensor x_reshape;
        x_reshape.Reshape(vector<TIndex>({ N, C, 1, 1 }));
        cudnnSetTensorDesc<T>(&input_desc, &x_reshape);
        cudnnSetTensorDesc<T>(&output_desc, &x_reshape);
    } else {
        CHECK_GE((int)Input(0).ndim(), 3)
            << "The number of dimensions should be at least 3.";
        bn_mode = CUDNN_BATCHNORM_SPATIAL;
#if CUDNN_VERSION_MIN(7, 0, 0)
        if (!this->use_global_stats) 
            bn_mode = CUDNN_BATCHNORM_SPATIAL_PERSISTENT;
#endif
        if (data_format == "NCHW") {
            cudnnSetTensorDesc<T>(&input_desc, &Input(-1));
            cudnnSetTensorDesc<T>(&output_desc, Output(0));
        } else if (data_format == "NHWC") {
            switch (Input(0).ndim()) {
                case 3:
                    cudnnSetTensor3dDesc<T>(&input_desc, data_format, &Input(-1));
                    cudnnSetTensor3dDesc<T>(&output_desc, data_format, Output(0));
                case 4:
                    cudnnSetTensor4dDesc<T>(&input_desc, data_format, &Input(-1));
                    cudnnSetTensor4dDesc<T>(&output_desc, data_format, Output(0));
                case 5:
                    cudnnSetTensor5dDesc<T>(&input_desc, data_format, &Input(-1));
                    cudnnSetTensor5dDesc<T>(&output_desc, data_format, Output(0));
                default:
                    LOG(FATAL) << "Only support the 3d/4d/5d input at NHWC mode.";
            }
        }
    }
    //  derive the bn desc
    CUDNN_CHECK(cudnnDeriveBNTensorDescriptor(bn_desc, input_desc, bn_mode));

    if (Output(0)->name() != "ignore" ||
        Output(1)->name() != "ignore" ||
        Output(2)->name() != "ignore") {
        auto* dYdata = Input(-1).template data<T, Context>();
        auto* dXdata = Output(0)->template mutable_data<T, Context>();
        auto* Xdata = Input(0).template data<T, Context>();
        auto* Sdata = Input(3).template data<T, Context>();
        auto* dSdata = Output(1)->template mutable_data<T, Context>();
        auto* dBdata = Output(2)->template mutable_data<T, Context>();
        auto* tMean_data = mean->template data<T, Context>();
        auto* tVar_data = var->template data<T, Context>();

        CUDNN_CHECK(cudnnBatchNormalizationBackward(cudnn_handle(),
                                                           bn_mode,
                                                 CUDNNType<T>::one,
                                                CUDNNType<T>::zero,
                                                 CUDNNType<T>::one,
                                                 CUDNNType<T>::one,
                                                output_desc, Xdata,
                                                input_desc, dYdata,
                                               output_desc, dXdata,
                                                           bn_desc,
                                                             Sdata,
                                                            dSdata,
                                                            dBdata,
                                                         this->eps,
                                                        tMean_data,
                                                       tVar_data));
    }
}

template <class Context> template <typename T>
void CuDNNBatchNormGradientOp<Context>::InferenceRunWithType() {
    INIT_MULTIPLIER(multiplier, NS);
    INIT_MULTIPLIER(num_multiplier, N);
    INIT_MULTIPLIER(spatial_multiplier, S);

    auto* dYdata = Input(-1).template data<T, Context>();
    auto* Sdata = Input(3).template data<T, Context>();
    auto* hVar_data = Input(2).template data<T, Context>();
    auto* tVar_data = var->template mutable_data<T, Context>();
    auto* NSMul_data = multiplier->template data<T, Context>();
    auto* SMul_data = spatial_multiplier->template data<T, Context>();
    auto* NMul_data = num_multiplier->template data<T, Context>();
    auto* NC_data = num_by_chans.template mutable_data<T, Context>();

    //  gradient w.r.t. scale
    if (Output(1)->name() != "ignore") 
        LOG(FATAL) << "The gamma should be fixed if using global stats.";
       
    //  gradient w.r.t. bias
    if (Output(2)->name() != "ignore") {
        auto* dBdata = Output(2)->template mutable_data<T, Context>();
        if (data_format == "NCHW") {
            math::Gemv<T, Context>(CblasNoTrans, NC, S,
                                1.0, dYdata, SMul_data,
                                         0.0, NC_data);
            math::Gemv<T, Context>(CblasTrans, N, C,
                            1.0, NC_data, NMul_data,
                                       1.0, dBdata);
        } else if (data_format == "NHWC") {
            math::Gemv<T, Context>(CblasTrans, NS, C,
                             1.0, dYdata, NSMul_data,
                                        1.0, dBdata);
        }
    }

    //  gradient w.r.t. x
    if (Output(0)->name() != "ignore") {
        stddev = ws()->GetBuffer();
        stddev->ReshapeLike(Input(0));
        auto* dXdata = Output(0)->template mutable_data<T, Context>();
        auto* Std_data = stddev->template mutable_data<T, Context>();

        //  compute stddev
        ctx().template Copy<T, Context, Context>(var->count(), tVar_data, hVar_data);
        math::AddScalar<T, Context>(var->count(), this->eps, tVar_data);
        math::Sqrt<T, Context>(var->count(), tVar_data, tVar_data);

        //  divide scale by stddev
        math::Div<T, Context>(var->count(), Sdata, tVar_data, tVar_data);

        //  compute dE/dY \cot (scale / std(X))
        if (data_format == "NCHW") {
            math::Gemm<T, Context>(CblasNoTrans, CblasNoTrans, N, C, 1,
                                             1.0, NMul_data, tVar_data,
                                                         0.0, NC_data);
            math::Gemm<T, Context>(CblasNoTrans, CblasNoTrans, NC, S, 1,
                                                1.0, NC_data, SMul_data,
                                                         0.0, Std_data);
        } else if (data_format == "NHWC") {
            math::Gemm<T, Context>(CblasNoTrans, CblasNoTrans, NS, C, 1,
                                             1.0, NSMul_data, tVar_data,
                                                         0.0, Std_data);
        }
        math::Mul<T, Context>(Output(0)->count(), dYdata, Std_data, dXdata);
        ws()->ReleaseBuffer(stddev);
    }
}

template <class Context>
void CuDNNBatchNormGradientOp<Context>::RunOnDevice() {
    Setup();

    if (Input(0).template IsType<float>()) {
        if (this->use_global_stats) InferenceRunWithType<float>();
        else TrainingRunWithType<float>();
    }
#ifdef WITH_CUDA_FP16
    else if (Input(0).template IsType<float16>()) {
        if (this->use_global_stats) InferenceRunWithType<float16>();
        else TrainingRunWithType<float16>();
    }
#endif
    else LOG(FATAL) << "Unsupported input types.";
}

REGISTER_CUDNN_OPERATOR(FusedBatchNormGradient, CuDNNBatchNormGradientOp<CUDAContext>);
INSTANTIATE_CUDNN_OPERATOR(BatchNormGradient);

}    // namespace dragon

#endif

#endif  // WITH_CUDNN
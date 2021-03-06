#include "operators/misc/gradient_op.h"
#include "core/workspace.h"
#include "utils/math_functions.h"

namespace dragon {

template <class Context> template <typename T>
void GradientGenerateOp<Context>::RunWithType() {
    for (int i = 0; i < OutputSize(); i++) {
        if (Output(i)->name() == "ignore") continue;
        Output(i)->ReshapeLike(Input(i));
        auto* dXdata = Output(0)->template mutable_data<T, Context>();
        math::Set<T, Context>(Output(0)->count(),
                              dragon_cast<T, float>(defaults[i]),
                              dXdata);
    }
}

template <class Context>
void GradientGenerateOp<Context>::RunOnDevice() {
    if (Input(0).template IsType<float>()) RunWithType<float>();
#ifdef WITH_CUDA_FP16
    else if (Input(0).template IsType<float16>()) RunWithType<float16>();
#endif
    else LOG(FATAL) << "Unsupported input types.";
}

DEPLOY_CPU(GradientGenerate);
#ifdef WITH_CUDA
DEPLOY_CUDA(GradientGenerate);
#endif
OPERATOR_SCHEMA(GradientGenerate);

template <class Context> template <typename T>
void GradientGatherOp<Context>::RunWithType() {
    auto* dXdata = Output(0)->template mutable_data<T, Context>();
    TIndex count = Output(0)->count();
    for (int i = 0; i < indices.size(); i++) {
        CHECK(Output(0)->dims() == Input(indices[i]).dims());
        auto* dYdata = Input(indices[i]).template data<T, Context>();
        if (i == 0) ctx().template Copy<T, Context, Context>(count, dXdata, dYdata);
        else math::Add<T, Context>(count, dXdata, dYdata, dXdata);
        Input(indices[i]).Reset();
    }
}

template <class Context>
void GradientGatherOp<Context>::RunOnDevice() {
    if (indices.size() == 0) return;
    Output(0)->ReshapeLike(Input(indices[0]));

    if (Input(indices[0]).template IsType<float>()) RunWithType<float>();
    else LOG(FATAL) << "Unsupported input types.";
}

DEPLOY_CPU(GradientGather);
#ifdef WITH_CUDA
DEPLOY_CUDA(GradientGather);
#endif
OPERATOR_SCHEMA(GradientGather).NumOutputs(1);
NO_GRADIENT(GradientGather);

template <class Context>
void StopGradientOp<Context>::RunOnDevice() {
    if (Output(0)->name() != Input(0).name()) {
        Output(0)->ReshapeLike(Input(0));
        Output(0)->Share(Input(0));
    }
}

DEPLOY_CPU(StopGradient);
#ifdef WITH_CUDA
DEPLOY_CUDA(StopGradient);
#endif
OPERATOR_SCHEMA(StopGradient).NumInputs(1).NumOutputs(1).Inplace({ { 0, 0 } });;
NO_GRADIENT(StopGradient);

}    // namespace dragon
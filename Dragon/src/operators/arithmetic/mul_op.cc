#include "operators/arithmetic/mul_op.h"
#include "core/workspace.h"
#include "utils/math_functions.h" 

namespace dragon {

template <class Context> template <typename T>
void MulOp<Context>::EltwiseRunWithType() {
    auto* X1data = Input(0).template data<T, Context>();
    auto* X2data = Input(1).template data<T, Context>();
    auto* Ydata = Output(0)->template mutable_data<T, Context>();
    math::Mul<T, Context>(Input(0).count(), X1data, X2data, Ydata);
}

template <class Context> template <typename T>
void MulOp<Context>::BroadcastRunWithType(int type) {
    TIndex outer_dim, inner_dim;
    auto* X1data = Input(0).template data<T, Context>();
    auto* X2data = Input(1).template data<T, Context>();
    auto* Ydata = Output(0)->template mutable_data<T, Context>();

    if (type == 0 || type == 1) {
        if (type == 0) {
            outer_dim = Input(0).count();
            inner_dim = 1;
        } else {
            outer_dim = Input(0).count(0, Input(0).axis(-1));
            inner_dim = Input(0).dim(-1);
        }
        INIT_MULTIPLIER(bcast_multiplier, outer_dim);
        auto* BMul_data = bcast_multiplier->template data<T, Context>();
        math::Gemm<T, Context>(CblasNoTrans, CblasNoTrans, outer_dim, inner_dim, 1,
            1.0, bcast_multiplier->template data<T, Context>(), X2data, 0.0, Ydata);
        math::Mul<T, Context>(Input(0).count(), X1data, Ydata, Ydata);
    } 
    else if (type == 2) {
        outer_dim = Input(0).dim(0);
        inner_dim = Input(0).count(1);
        INIT_MULTIPLIER(bcast_multiplier, inner_dim);
        auto* BMul_data = bcast_multiplier->template data<T, Context>();
        math::Gemm<T, Context>(CblasNoTrans, CblasNoTrans, outer_dim, inner_dim, 1,
            1.0, X2data, bcast_multiplier->template data<T, Context>(), 0.0, Ydata);
        math::Mul<T, Context>(Input(0).count(), X1data, Ydata, Ydata);
    }
}

template <class Context>
void MulOp<Context>::RunOnDevice() {
    Output(0)->ReshapeLike(Input(0));

    if (Input(0).dims() == Input(1).dims()) {
        if (Input(0).template IsType<float>()) EltwiseRunWithType<float>();
        else LOG(FATAL) << "Unsupported input types.";
    } 
    else if (Input(0).dim(0) == Input(1).dim(0) && Input(1).count(1) == 1) {
        if (Input(0).template IsType<float>()) BroadcastRunWithType<float>(2);
#ifdef WITH_CUDA_FP16
        else if (Input(0).template IsType<float16>()) BroadcastRunWithType<float16>(2);
#endif
        else LOG(FATAL) << "Unsupported input types.";
    }
    else if (Input(0).dim(-1) == Input(1).dim(-1) && 
             Input(1).count(0, Input(1).axis(-1)) == 1) {
        if (Input(0).template IsType<float>()) BroadcastRunWithType<float>(1);
#ifdef WITH_CUDA_FP16
        else if (Input(0).template IsType<float16>()) BroadcastRunWithType<float16>(1);
#endif
        else LOG(FATAL) << "Unsupported input types.";
    } 
    else if (Input(1).ndim() == 1 && Input(1).dim(0) == 1) {
        if (Input(0).template IsType<float>()) BroadcastRunWithType<float>(0);
#ifdef WITH_CUDA_FP16
        else if (Input(0).template IsType<float16>()) BroadcastRunWithType<float16>(0);
#endif
        else LOG(FATAL) << "Unsupported input types.";
    }
    else {
        LOG(FATAL) << "Could not be broadcast together with shapes "
                   << Input(0).dim_string() << "  " << Input(1).dim_string();
    }
}

DEPLOY_CPU(Mul);
#ifdef WITH_CUDA
DEPLOY_CUDA(Mul);
#endif
OPERATOR_SCHEMA(Mul).NumInputs(2).NumOutputs(1);

template <class Context> template <typename T>
void MulGradientOp<Context>::EltwiseRunWithType() {
    auto* dYdata = Input(2).template data<T, Context>();
    if (Output(1)->name() != "ignore") {
        auto* X1data = Input(0).template data<T, Context>();
        auto* dX2data = Output(1)->template mutable_data<T, Context>();
        math::Mul<T, Context>(Input(0).count(), dYdata, X1data, dX2data);
    }
    if (Output(0)->name() != "ignore") {
        auto* X2data = Input(1).template data<T, Context>();
        auto* dX1data = Output(0)->template mutable_data<T, Context>();
        math::Mul<T, Context>(Input(0).count(), dYdata, X2data, dX1data);
    }
}

template <class Context> template <typename T>
void MulGradientOp<Context>::BroadcastRunWithType(int type) {
    TIndex outer_dim, inner_dim;
    auto* dYdata = Input(2).template data<T, Context>();
    if (type == 0) {
        outer_dim = Input(0).count();
        inner_dim = 1;
    } else if (type == 1) {
        outer_dim = Input(0).count(0, Input(0).axis(-1));
        inner_dim = Input(0).dim(-1);
    } else if (type == 2) {
        outer_dim = Input(0).dim(0);
        inner_dim = Input(0).count(1);
    }

    if (Output(1)->name() != "ignore") {
        auto* X1data = Input(0).template data<T, Context>();
        auto* dX1data = Output(0)->template mutable_data<T, Context>();
        auto* dX2data = Output(1)->template mutable_data<T, Context>();
        if (type == 0 || type == 1) {
            INIT_MULTIPLIER(bcast_multiplier, outer_dim);
            auto* BMul_data = bcast_multiplier->template data<T, Context>();
            math::Mul<T, Context>(Input(-1).count(), dYdata, X1data, dX1data);
            math::Gemv<T, Context>(CblasTrans, outer_dim, inner_dim, 1.0,
                                   dX1data, BMul_data, 0.0, dX2data);
        } else if (type == 2) {
            outer_dim = Input(0).dim(0);
            inner_dim = Input(0).count(1);
            INIT_MULTIPLIER(bcast_multiplier, inner_dim);
            auto* BMul_data = bcast_multiplier->template data<T, Context>();
            math::Mul<T, Context>(Input(-1).count(), dYdata, X1data, dX1data);
            math::Gemv<T, Context>(CblasNoTrans, outer_dim, inner_dim, 1.0,
                                   dX1data, BMul_data, 0.0, dX2data);
        }
    }

    if (Output(0)->name() != "ignore") {
        auto* X2data = Input(1).template data<T, Context>();
        auto* dX1data = Output(0)->template mutable_data<T, Context>();
        if (type == 0 || type == 1) {
            INIT_MULTIPLIER(bcast_multiplier, outer_dim);
            math::Gemm<T, Context>(CblasNoTrans, CblasNoTrans, outer_dim, inner_dim, 1,
                1.0, bcast_multiplier->template data<T, Context>(), X2data, 0.0, dX1data);
        } else if (type == 2) {
            INIT_MULTIPLIER(bcast_multiplier, inner_dim);
            math::Gemm<T, Context>(CblasNoTrans, CblasNoTrans, outer_dim, inner_dim, 1,
                1.0, X2data, bcast_multiplier->template data<T, Context>(), 0.0, dX1data);
        }
        math::Mul<T, Context>(Output(0)->count(), dYdata, dX1data, dX1data);
    }
}

template <class Context>
void MulGradientOp<Context>::RunOnDevice() {
    Output(0)->ReshapeLike(Input(0));
    Output(1)->ReshapeLike(Input(1));

    if (Input(0).dims() == Input(1).dims()) {
        if (Input(0).template IsType<float>()) EltwiseRunWithType<float>();
        else LOG(FATAL) << "Unsupported input types.";
    } 
    else if (Input(0).dim(0) == Input(1).dim(0) && Input(1).count(1) == 1) {
        if (Input(0).template IsType<float>()) BroadcastRunWithType<float>(2);
#ifdef WITH_CUDA_FP16
        else if (Input(0).template IsType<float16>()) BroadcastRunWithType<float16>(2);
#endif
        else LOG(FATAL) << "Unsupported input types.";
    }
    else if (Input(0).dim(-1) == Input(1).dim(-1) && 
             Input(1).count(0, Input(1).axis(-1)) == 1) {
        if (Input(0).template IsType<float>()) BroadcastRunWithType<float>(1);
#ifdef WITH_CUDA_FP16
        else if (Input(0).template IsType<float16>()) BroadcastRunWithType<float16>(1);
#endif
        else LOG(FATAL) << "Unsupported input types.";
    } 
    else if (Input(1).ndim() == 1 && Input(1).dim(0) == 1) {
        if (Input(0).template IsType<float>()) BroadcastRunWithType<float>(0);
#ifdef WITH_CUDA_FP16
        else if (Input(0).template IsType<float16>()) BroadcastRunWithType<float16>(0);
#endif
        else LOG(FATAL) << "Unsupported input types.";
    }
    else {
        LOG(FATAL) << "Could not be broadcast together with shapes "
                   << Input(0).dim_string() << "  " << Input(1).dim_string();
    }
}

template <class Context>
void MulGradientOp<Context>::ShareGradient() {
    for (int i = 0; i < OutputSize(); i++) {
        if (Output(i)->name() != "ignore") {
            Tensor* dX = ws()->GetBuffer("Grad");
            ws()->CreateAvatar(Output(i), dX);
            break;
        }
    }
}

DEPLOY_CPU(MulGradient);
#ifdef WITH_CUDA
DEPLOY_CUDA(MulGradient);
#endif
OPERATOR_SCHEMA(MulGradient).NumInputs(3).NumOutputs(2);

class GetMulGradient : public GradientMakerBase {
 public:
    GRADIENT_MAKER_CTOR(GetMulGradient);
    vector<OperatorDef> MakeDefs() override {
        return SingleDef(def.type() + "Gradient", "",
            vector<string> {I(0), I(1), GO(0)},
            vector<string> {GI(0), GI(1)});
    }
};
REGISTER_GRADIENT(Mul, GetMulGradient);

}    // namespace dragon
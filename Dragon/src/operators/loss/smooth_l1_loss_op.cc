#include "operators/loss/smooth_l1_loss_op.h"
#include "core/workspace.h"
#include "utils/math_functions.h"
#include "utils/op_kernel.h"

namespace dragon {

template <class Context> template <typename T>
void SmoothL1LossOp<Context>::RunWithType() {
    auto* X0data = Input(0).template data<T, Context>();
    auto* X1data = Input(1).template data<T, Context>();
    auto* diff_data = diff->template mutable_data<T, Context>();
    auto* error_data = error->template mutable_data<T, Context>();
    auto* Ydata = Output(0)->template mutable_data<T, CPUContext>();

    math::Sub<T, Context>(diff->count(), X0data, X1data, diff_data);
    if (InputSize() > 2) {
        auto* inside_w_data = Input(2).template data<T, Context>();
        math::Mul<T, Context>(diff->count(), inside_w_data, diff_data, diff_data);
    }
    kernel::SmoothL1<T, Context>(diff->count(), sigma2, diff_data, error_data);
    if (InputSize() > 3) {
        auto* outside_w_data = Input(3).template data<T, Context>();
        math::Mul<T, Context>(diff->count(), outside_w_data, error_data, error_data);
    }
    Ydata[0] = math::ASum<T, Context>(error->count(), error_data);

    T normalizer;
    if (normalization == "BATCH_SIZE") normalizer = Input(0).dim(0);
    else if (normalization == "FULL") normalizer = Input(0).count();
    else if (normalization == "NONE") normalizer = 1;
    Ydata[0] = Ydata[0] / normalizer;
}

template <class Context>
void SmoothL1LossOp<Context>::RunOnDevice() {
    CHECK(Input(0).dims() == Input(1).dims());
    if (InputSize() > 2) CHECK(Input(0).dims() == Input(2).dims());
    if (InputSize() > 3) CHECK(Input(0).dims() == Input(3).dims());
    Output(0)->Reshape(vector<TIndex>(1, 1));

    diff = ws()->CreateTensor("/mnt/" + Anchor() + "/smoothl1_loss/diff");
    error = ws()->CreateTensor("/share/smoothl1_loss_error");
    diff->ReshapeLike(Input(0));
    error->ReshapeLike(Input(0));

    if (Input(0).template IsType<float>()) RunWithType<float>();
    else LOG(FATAL) << "Unsupported input types.";
}

DEPLOY_CPU(SmoothL1Loss);
#ifdef WITH_CUDA
DEPLOY_CUDA(SmoothL1Loss);
#endif
OPERATOR_SCHEMA(SmoothL1Loss).NumInputs(2, 4).NumOutputs(1);

template <class Context> template <typename T>
void SmoothL1LossGradientOp<Context>::RunWithType() {
    auto* diff_data = diff->template mutable_data<T, Context>();
    auto* dYdata = Input(-1).template data<T, CPUContext>();
    kernel::SmoothL1Grad<T, Context>(diff->count(), sigma2, diff_data, diff_data);

    T alpha = dYdata[0], normalizer;
    if (normalization == "BATCH_SIZE") normalizer = Input(0).dim(0);
    else if (normalization == "FULL") normalizer = Input(0).count();
    else if (normalization == "NONE") normalizer = 1;
    alpha = alpha / normalizer;

    for (int i = 0; i < 2; i++) {
        if (Output(i)->name() == "ignore") continue;
        Output(i)->ReshapeLike(Input(i));
        auto* dXdata = Output(i)->template mutable_data<T, Context>();
        const T sign = (i == 0) ? 1 : -1;
        alpha *= sign;
        math::Axpby<T, Context>(Output(i)->count(), alpha, diff_data, 0, dXdata);
        if (InputSize() > 3) {
            auto* inside_w_data = Input(2).template data<T, Context>();
            math::Mul<T, Context>(Output(i)->count(), inside_w_data, dXdata, dXdata);
        }
        if (InputSize() > 4) {
            auto* outside_w_data = Input(3).template data<T, Context>();
            math::Mul<T, Context>(Output(i)->count(), outside_w_data, dXdata, dXdata);
        }
    }
}

template <class Context>
void SmoothL1LossGradientOp<Context>::RunOnDevice() {
    diff = ws()->GetTensor("/mnt/" + Anchor() + "/smoothl1_loss/diff");

    if (Input(0).template IsType<float>()) RunWithType<float>();
    else LOG(FATAL) << "Unsupported input types.";
}

DEPLOY_CPU(SmoothL1LossGradient);
#ifdef WITH_CUDA
DEPLOY_CUDA(SmoothL1LossGradient);
#endif
OPERATOR_SCHEMA(SmoothL1LossGradient).NumInputs(3, 5).NumOutputs(2);

class GetSmoothL1LossGradient final : public GradientMakerBase {
 public:
    GRADIENT_MAKER_CTOR(GetSmoothL1LossGradient);
    vector<OperatorDef> MakeDefs() override {
        vector<string> inputs;
        for (auto input : def.input()) inputs.push_back(input);
        inputs.push_back(GO(0));
        return SingleDef(def.type() + "Gradient", "",
                                              inputs,
                      vector<string> {GI(0), GI(1)});
    }
};
REGISTER_GRADIENT(SmoothL1Loss, GetSmoothL1LossGradient);

}    // namespace dragon

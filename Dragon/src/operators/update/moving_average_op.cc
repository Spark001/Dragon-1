#include "operators/update/moving_average_op.h"
#include "utils/math_functions.h"

namespace dragon {

template <class Context> template <typename T>
void MovingAverageOp<Context>::RunWithType() {
    auto* Xdata = Input(0).template data<T, Context>();
    auto* Ydata = Output(0)->template mutable_data<T, Context>();
    math::Axpby<T, Context>(Output(0)->count(), 1.0 - decay, Xdata, decay, Ydata);
}

template <class Context>
void MovingAverageOp<Context>::RunOnDevice() {
    CHECK(Input(0).count() == Output(0)->count());

    if (Input(0).template IsType<float>()) RunWithType<float>();
    else if (Input(0).template IsType<float16>()) RunWithType<float16>();
    else LOG(FATAL) << "Unsupported input types.";
}

DEPLOY_CPU(MovingAverage);
#ifdef WITH_CUDA
DEPLOY_CUDA(MovingAverage);
#endif
OPERATOR_SCHEMA(MovingAverage).NumInputs(1).NumOutputs(1);

NO_GRADIENT(MovingAverage);

}   // namespace dragon
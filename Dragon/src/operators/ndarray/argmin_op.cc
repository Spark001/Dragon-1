#include "operators/ndarray/argmin_op.h"
#include "utils/op_kernel.h"

namespace dragon {

template <class Context> template <typename T>
void ArgminOp<Context>::RunWithType() {
    if (top_k != 1) {
        //  it's difficult to implement device code when top_k > 1
        auto* Xdata = Input(0).template data<T, CPUContext>();
        auto* Ydata = Output(0)->template mutable_data<T, CPUContext>();
        kernel::Argmin<T, CPUContext>(count, axis_dim, inner_dim, top_k, Xdata, Ydata);
    } else {
        auto* Xdata = Input(0).template data<T, Context>();
        auto* Ydata = Output(0)->template mutable_data<T, Context>();
        kernel::Argmin<T, Context>(count, axis_dim, inner_dim, top_k, Xdata, Ydata);
    }
}

template <class Context>
void ArgminOp<Context>::RunOnDevice() {
    if (axis != -1) {
        axis_dim = Input(0).dim(axis);
        inner_dim = Input(0).count(axis) / axis_dim;
    } else {
        axis_dim = Input(0).count();
        inner_dim = 1;
    }
    count = Input(0).count() / axis_dim;
    vector<TIndex> dims = Input(0).dims();
    if (!keep_dims) {
        if (axis != -1) {
            if (top_k == 1) dims.erase(dims.begin() + axis);
            else dims[axis] = top_k;
        } else {
            dims = vector<TIndex>(1, top_k);
        }
    } else {
        if (axis == -1) dims = vector<TIndex>(Input(0).ndim(), 1);
        dims[axis] = top_k;
    }
    Output(0)->Reshape(dims);

    if (Input(0).template IsType<float>()) RunWithType<float>();
    else LOG(FATAL) << "Unsupported input types.";
}

DEPLOY_CPU(Argmin);
#ifdef WITH_CUDA
DEPLOY_CUDA(Argmin);
#endif
OPERATOR_SCHEMA(Argmin).NumInputs(1).NumOutputs(1);

NO_GRADIENT(Argmin);

}    // namespace dragon
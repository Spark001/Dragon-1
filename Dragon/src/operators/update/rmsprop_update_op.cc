#include "operators/update/rmsprop_update_op.h"
#include "core/workspace.h"
#include "utils/op_kernel.h"

namespace dragon {

template <class Context>
void RMSPropUpdateOp<Context>::ComputeRunWithFloat() {
    h = ws()->CreateTensor("/mnt/" + Slot() + "/rmsprop/h");
    tmp = ws()->CreateTensor("/mnt/" + Slot() + "/rmsprop/tmp");
    h->ReshapeLike(Input(0));

    lr = Param("base_lr") * this->lr_mult;
    auto* dXdata = Input(0).template mutable_data<float, Context>();
    auto* Hdata = h->template mutable_data<float, Context>();
    kernel::RMSPropUpdate<float, Context>(Input(0).count(),
                                                    dXdata,
                                                     Hdata,
                                                       tmp,
                                                     decay,
                                                       eps,
                                                       lr);
}

DEPLOY_CPU(RMSPropUpdate);
#ifdef WITH_CUDA
DEPLOY_CUDA(RMSPropUpdate);
#endif
OPERATOR_SCHEMA(RMSPropUpdate).NumInputs(1).NumOutputs(1);

NO_GRADIENT(RMSPropUpdate);
    
}    // namespace dragon
// ------------------------------------------------------------
// Copyright (c) 2017-preseent, SeetaTech, Co.,Ltd.
//
// Licensed under the BSD 2-Clause License.
// You should have received a copy of the BSD 2-Clause License
// along with the software. If not, See,
//
//      <https://opensource.org/licenses/BSD-2-Clause>
//
// -------------------------------------------------------------

#ifndef DRAGON_OPERATORS_NDARRAY_TILE_OP_H_
#define DRAGON_OPERATORS_NDARRAY_TILE_OP_H_

#include "core/operator.h"

namespace dragon {

template <class Context>
class TileOp : public Operator<Context> {
 public:
    TileOp(const OperatorDef& op_def, Workspace* ws)
        : Operator<Context>(op_def, ws) {
        GET_ARGUMENTS_WITH_DESC(int, multiples);
    }
    USE_OPERATOR_FUNCTIONS(Context);

    void RunOnDevice() override;
    template<typename T> void TileRunWithType();

 protected:
    DECLARE_ARGUMENTS_WITH_DESC(int, multiples);
    TIndex axis, multiple, outer_dim, ex_inner_dim;
    Tensor* dest, *source;
};

template <class Context>
class TileGradientOp : public Operator<Context> {
 public:
    TileGradientOp(const OperatorDef& op_def, Workspace* ws) 
        : Operator<Context>(op_def, ws) {
        GET_ARGUMENTS_WITH_DESC(int, multiples);
        DISABLE_SHARE_GRADIENT;
    }
    USE_OPERATOR_FUNCTIONS(Context);

    void RunOnDevice() override;
    template<typename T> void TileRunWithType();

 protected:
    DECLARE_ARGUMENTS_WITH_DESC(int, multiples);
    TIndex axis, multiple, outer_dim, ex_inner_dim;
    Tensor* dest, *source;
};

DEFINE_ARGUMENTS_WITH_DESC(int, TileOp, multiples);
DEFINE_ARGUMENTS_WITH_DESC(int, TileGradientOp, multiples);

}    // namespace dragon

#endif    // DRAGON_OPERATORS_NDARRAY_TILE_OP_H_
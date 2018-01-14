#ifndef GRB_BACKEND_APSPIE_ASSIGN_HPP
#define GRB_BACKEND_APSPIE_ASSIGN_HPP

#include <iostream>

#include "graphblas/backend/apspie/Descriptor.hpp"
#include "graphblas/backend/apspie/SparseMatrix.hpp"
#include "graphblas/backend/apspie/DenseMatrix.hpp"
#include "graphblas/backend/apspie/operations.hpp"
#include "graphblas/backend/apspie/kernels/spmspv.hpp"
#include "graphblas/backend/apspie/kernels/apply.hpp"
#include "graphblas/backend/apspie/kernels/util.hpp"

namespace graphblas
{
namespace backend
{

  template <typename W, typename T, typename M,
  Info assignDense( DenseVector<W>*           w,
                    const Vector<M>*          mask,
                    const BinaryOp<W,W,W>*    accum,
                    T                         val,
                    const std::vector<Index>* indices,
                    Index                     nindices,
                    Descriptor*               desc )
  {
    // Get descriptor parameters for SCMP, REPL, TRAN
    Desc_value scmp_mode, repl_mode;
    CHECK( desc->get(GrB_MASK, &scmp_mode) );
    CHECK( desc->get(GrB_OUTP, &repl_mode) );

    // TODO: add accum and replace support
    // -have masked variants as separate kernel
    // -accum and replace as parts in flow
    bool use_mask = (mask!=NULL);
    bool use_accum= (accum!=NULL);            //TODO
    bool use_scmp = (scmp_mode==GrB_SCMP);
    bool use_repl = (repl_mode==GrB_REPLACE); //TODO
    bool use_allowdupl; //TODO opt4
    bool use_struconly; //TODO opt5

    //printState( use_mask, use_accum, use_scmp, use_repl, use_tran );

    // temp_ind and temp_val need |V| memory for masked case, so just allocate 
    // this much memory for now. TODO: optimize for memory
    int size          = (float)A->nvals_*GrB_THRESHOLD+1;
    desc->resize((4*A_nrows+2*size)*max(sizeof(Index),sizeof(T)), "buffer");

    // Only difference between masked and unmasked versions if whether
    // eWiseMult() is called afterwards or not
    if( use_mask )
    {
      // temp_ind and temp_val need |V| memory
      Index* temp_ind   = (Index*) desc->d_buffer_;
      T*     temp_val   = (T*)     desc->d_buffer_+A_nrows;
      Index  temp_nvals = 0;
    
      if( spmspv_mode==GrB_APSPIE )
        spmspvApspie(
            temp_ind, temp_val, &temp_nvals, NULL, op->identity(),
            op->mul_, op->add_, A_nrows, A->nvals_,
            A_csrRowPtr, A_csrColInd, A_csrVal, 
            u->d_ind_, u->d_val_, &u->nvals_, desc );
      else if( spmspv_mode==GrB_APSPIELB )
        spmspvApspieLB(
            temp_ind, temp_val, &temp_nvals, NULL, op->identity(),
            //op->mul_, op->add_, A_nrows, A->nvals_,
            mgpu::multiplies<a>(), mgpu::plus<a>(), A_nrows, A->nvals_,
            A_csrRowPtr, A_csrColInd, A_csrVal, 
            u->d_ind_, u->d_val_, &u->nvals_, desc );
      else if( spmspv_mode==GrB_GUNROCKLB )
        spmspvGunrockLB(
            temp_ind, temp_val, &temp_nvals, NULL, op->identity(),
            op->mul_, op->add_, A_nrows, A->nvals_,
            A_csrRowPtr, A_csrColInd, A_csrVal, 
            u->d_ind_, u->d_val_, &u->nvals_, desc );
      else if( spmspv_mode==GrB_GUNROCKTWC )
        spmspvGunrockTWC(
            temp_ind, temp_val, &temp_nvals, NULL, op->identity(),
            op->mul_, op->add_, A_nrows, A->nvals_,
            A_csrRowPtr, A_csrColInd, A_csrVal, 
            u->d_ind_, u->d_val_, &u->nvals_, desc );

      // Get descriptor parameters for nthreads
      Desc_value nt_mode;
      CHECK( desc->get(GrB_NT, &nt_mode) );
      const int nt = static_cast<int>(nt_mode);
      dim3 NT, NB;
			NT.x = nt;
			NT.y = 1;
			NT.z = 1;
			NB.x = (temp_nvals+nt-1)/nt;
			NB.y = 1;
			NB.z = 1;

      // Mask type
      // 1) Dense mask
      // 2) Sparse mask (TODO)
      // 3) Uninitialized
      Storage mask_vec_type;
      CHECK( mask->getStorage(&mask_vec_type) );

      if( mask_vec_type==GrB_DENSE )
      {
        if( use_scmp )
          applyKernel<true><<<NB,NT>>>( temp_ind, temp_val, 
              (mask->dense_).d_val_, (void*)NULL, (M)-1.f, 
              mgpu::identity<Index>(), temp_nvals );
        else
          applyKernel<false><<<NB,NT>>>( temp_ind, temp_val,
              (mask->dense_).d_val_, (void*)NULL, (M)-1.f, 
              mgpu::identity<Index>(), temp_nvals );
      }
      else if( mask_vec_type==GrB_SPARSE )
      {
        std::cout << "Error: Feature not implemented yet!\n";
      }
      else
      {
        return GrB_UNINITIALIZED_OBJECT;
      }

      printDevice("mask", (mask->dense_).d_val_, A_nrows);
      printDevice("temp_ind", temp_ind, temp_nvals);
      printDevice("temp_val", temp_val, temp_nvals);

      Index* d_flag = (Index*) desc->d_buffer_+2*A_nrows;
      Index* d_scan = (Index*) desc->d_buffer_+3*A_nrows;

      updateFlagKernel<<<NB,NT>>>( d_flag, (Index)-1, temp_ind, temp_nvals );
      mgpu::Scan<mgpu::MgpuScanTypeExc>( d_flag, temp_nvals, (Index)0, 
          mgpu::plus<Index>(), (Index*)0, &w->nvals_, d_scan, 
          *(desc->d_context_) );
      printDevice("d_flag", d_flag, temp_nvals);
      printDevice("d_scan", d_scan, temp_nvals);

      streamCompactKernel<<<NB,NT>>>( w->d_ind_, w->d_val_, d_scan, (W)0, 
          temp_ind, temp_val, temp_nvals );
      printDevice("w_ind", w->d_ind_, w->nvals_);
      printDevice("w_val", w->d_val_, w->nvals_);
    }
    else
    {
      if( spmspv_mode==GrB_APSPIE )
        spmspvApspie(
            w->d_ind_, w->d_val_, &w->nvals_, NULL, op->identity(),
            op->mul_, op->add_, A_nrows, A->nvals_,
            A_csrRowPtr, A_csrColInd, A_csrVal, 
            u->d_ind_, u->d_val_, &u->nvals_, desc );
      else if( spmspv_mode==GrB_APSPIELB )
        spmspvApspieLB(
            w->d_ind_, w->d_val_, &w->nvals_, NULL, op->identity(),
            mgpu::multiplies<a>(), mgpu::plus<a>(), A_nrows, A->nvals_,
            A_csrRowPtr, A_csrColInd, A_csrVal, 
            u->d_ind_, u->d_val_, &u->nvals_, desc );
      else if( spmspv_mode==GrB_GUNROCKLB )
        spmspvGunrockLB(
            w->d_ind_, w->d_val_, &w->nvals_, NULL, op->identity(),
            op->mul_, op->add_, A_nrows, A->nvals_,
            A_csrRowPtr, A_csrColInd, A_csrVal, 
            u->d_ind_, u->d_val_, &u->nvals_, desc );
      else if( spmspv_mode==GrB_GUNROCKTWC )
        spmspvGunrockTWC(
            w->d_ind_, w->d_val_, &w->nvals_, NULL, op->identity(),
            op->mul_, op->add_, A_nrows, A->nvals_,
            A_csrRowPtr, A_csrColInd, A_csrVal, 
            u->d_ind_, u->d_val_, &u->nvals_, desc );
    }
    w->need_update_ = true;
    return GrB_SUCCESS;
  }

}  // backend
}  // graphblas

#endif  // GRB_BACKEND_APSPIE_ASSIGN_HPP

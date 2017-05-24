#ifndef GRB_BACKEND_SEQUENTIAL_SPGEMM_HPP
#define GRB_BACKEND_SEQUENTIAL_SPGEMM_HPP

#include <iostream>

#include <cuda.h>
#include <cusparse.h>

#include "graphblas/backend/sequential/SparseMatrix.hpp"
#include "graphblas/types.hpp"

//#define TA     32
//#define TB     32
//#define NT     64

namespace graphblas
{
namespace backend
{

  template<typename c, typename a, typename b>
  Info cusparse_spgemm( SparseMatrix<c>&       C,
                        const Semiring&        op,
                        const SparseMatrix<a>& A,
                        const SparseMatrix<b>& B )
  {
    Index A_nrows, A_ncols, A_nvals;
    Index B_nrows, B_ncols, B_nvals;
    Index C_nrows, C_ncols, C_nvals;

    A.nrows( A_nrows );
    A.ncols( A_ncols );
    A.nvals( A_nvals );
    B.nrows( B_nrows );
    B.ncols( B_ncols );
		B.nvals( B_nvals );
    C.nrows( C_nrows );
    C.ncols( C_ncols );

    // Dimension compatibility check
    if( (A_ncols != B_nrows) || (C_ncols != B_ncols) || (C_nrows != A_nrows ) )
    {
      std::cout << "Dim mismatch" << std::endl;
      std::cout << A_ncols << " " << B_nrows << std::endl;
      std::cout << C_ncols << " " << B_ncols << std::endl;
      std::cout << C_nrows << " " << A_nrows << std::endl;
      return GrB_DIMENSION_MISMATCH;
    }

    // Domain compatibility check
    // TODO: add domain compatibility check

    // SpGEMM Computation
    cusparseHandle_t handle;
    cusparseCreate( &handle );
    cusparseSetPointerMode( handle, CUSPARSE_POINTER_MODE_HOST );

    cusparseMatDescr_t descr;
    cusparseCreateMatDescr( &descr );

    cusparseSetMatType( descr, CUSPARSE_MATRIX_TYPE_GENERAL );
    cusparseSetMatIndexBase( descr, CUSPARSE_INDEX_BASE_ZERO );
    cusparseStatus_t status;

		int baseC;
		int *nnzTotalDevHostPtr = &(C_nvals);
		//if( C.d_csrRowPtr==NULL )
    //  CUDA_SAFE_CALL( cudaMalloc( &C.d_csrRowPtr, (A_nrows+1)*sizeof(Index) ));

		// Analyze
    status = cusparseXcsrgemmNnz( handle,
        CUSPARSE_OPERATION_NON_TRANSPOSE, CUSPARSE_OPERATION_NON_TRANSPOSE,
				A_nrows, B_ncols, A_ncols, 
				descr, A_nvals, A.d_csrRowPtr, A.d_csrColInd, 
				descr, B_nvals, B.d_csrRowPtr, B.d_csrColInd,
        descr, C.d_csrRowPtr, nnzTotalDevHostPtr );

    switch( status ) {
        case CUSPARSE_STATUS_SUCCESS:
            //std::cout << "SpMM successful!\n";
            break;
        case CUSPARSE_STATUS_NOT_INITIALIZED:
            std::cout << "Error: Library not initialized.\n";
            break;
        case CUSPARSE_STATUS_INVALID_VALUE:
            std::cout << "Error: Invalid parameters m, n, or nnz.\n";
            break;
        case CUSPARSE_STATUS_EXECUTION_FAILED:
            std::cout << "Error: Failed to launch GPU.\n";
            break;
        case CUSPARSE_STATUS_ALLOC_FAILED:
            std::cout << "Error: Resources could not be allocated.\n";
            break;
        case CUSPARSE_STATUS_ARCH_MISMATCH:
            std::cout << "Error: Device architecture does not support.\n";
            break;
        case CUSPARSE_STATUS_INTERNAL_ERROR:
            std::cout << "Error: An internal operation failed.\n";
            break;
        case CUSPARSE_STATUS_MATRIX_TYPE_NOT_SUPPORTED:
            std::cout << "Error: Matrix type not supported.\n";
    }

    if( nnzTotalDevHostPtr != NULL )
			C_nvals = *nnzTotalDevHostPtr;
    else {
      CUDA_SAFE_CALL( cudaMemcpy( &(C_nvals), C.d_csrRowPtr+A_nrows, 
					sizeof(Index), cudaMemcpyDeviceToHost ));
			CUDA_SAFE_CALL( cudaMemcpy( &(baseC), C.d_csrRowPtr, 
				  sizeof(Index), cudaMemcpyDeviceToHost ));
			C_nvals -= baseC;
		}

		if( C_nvals >= C.nvals_ ) {
			CUDA_SAFE_CALL( cudaFree( C.d_csrColInd ));
			CUDA_SAFE_CALL( cudaFree( C.d_csrVal    ));
		  CUDA_SAFE_CALL( cudaMalloc( (void**) &C.d_csrColInd, C_nvals*sizeof(c) ));
		  CUDA_SAFE_CALL( cudaMalloc( (void**) &C.d_csrVal,    C_nvals*sizeof(c) ));
		}

    // Compute
    status = cusparseScsrgemm( handle,
        CUSPARSE_OPERATION_NON_TRANSPOSE, CUSPARSE_OPERATION_NON_TRANSPOSE,
				A_nrows, B_ncols, A_ncols, 
				descr, A_nvals, A.d_csrVal, A.d_csrRowPtr, A.d_csrColInd, 
				descr, B_nvals, B.d_csrVal, B.d_csrRowPtr, B.d_csrColInd,
        descr,          C.d_csrVal, C.d_csrRowPtr, C.d_csrColInd );

    switch( status ) {
        case CUSPARSE_STATUS_SUCCESS:
            //std::cout << "SpMM successful!\n";
            break;
        case CUSPARSE_STATUS_NOT_INITIALIZED:
            std::cout << "Error: Library not initialized.\n";
            break;
        case CUSPARSE_STATUS_INVALID_VALUE:
            std::cout << "Error: Invalid parameters m, n, or nnz.\n";
            break;
        case CUSPARSE_STATUS_EXECUTION_FAILED:
            std::cout << "Error: Failed to launch GPU.\n";
            break;
        case CUSPARSE_STATUS_ALLOC_FAILED:
            std::cout << "Error: Resources could not be allocated.\n";
            break;
        case CUSPARSE_STATUS_ARCH_MISMATCH:
            std::cout << "Error: Device architecture does not support.\n";
            break;
        case CUSPARSE_STATUS_INTERNAL_ERROR:
            std::cout << "Error: An internal operation failed.\n";
            break;
        case CUSPARSE_STATUS_MATRIX_TYPE_NOT_SUPPORTED:
            std::cout << "Error: Matrix type not supported.\n";
    }

    C.need_update = true;  // Set flag that we need to copy data from GPU
    C.nvals_ = C_nvals;     // Update nnz count for C
		return GrB_SUCCESS;
  }

  template<typename c, typename a, typename b>
  Info cusparse_spgemm_analyze( SparseMatrix<c>&       C,
                                const Semiring&        op,
                                const SparseMatrix<a>& A,
                                const SparseMatrix<b>& B )
  {
    Index A_nrows, A_ncols, A_nvals;
    Index B_nrows, B_ncols, B_nvals;
    Index C_nrows, C_ncols, C_nvals;

    A.nrows( A_nrows );
    A.ncols( A_ncols );
    A.nvals( A_nvals );
    B.nrows( B_nrows );
    B.ncols( B_ncols );
		B.nvals( B_nvals );
    C.nrows( C_nrows );
    C.ncols( C_ncols );

    // Dimension compatibility check
    if( (A_ncols != B_nrows) || (C_ncols != B_ncols) || (C_nrows != A_nrows ) )
    {
      std::cout << "Dim mismatch" << std::endl;
      std::cout << A_ncols << " " << B_nrows << std::endl;
      std::cout << C_ncols << " " << B_ncols << std::endl;
      std::cout << C_nrows << " " << A_nrows << std::endl;
      return GrB_DIMENSION_MISMATCH;
    }

    // Domain compatibility check
    // TODO: add domain compatibility check

    // SpGEMM Computation
    cusparseHandle_t handle;
    cusparseCreate( &handle );
    cusparseSetPointerMode( handle, CUSPARSE_POINTER_MODE_HOST );

    cusparseMatDescr_t descr;
    cusparseCreateMatDescr( &descr );

    cusparseSetMatType( descr, CUSPARSE_MATRIX_TYPE_GENERAL );
    cusparseSetMatIndexBase( descr, CUSPARSE_INDEX_BASE_ZERO );
    cusparseStatus_t status;

		int baseC;
		int *nnzTotalDevHostPtr = &(C_nvals);
		//if( C.d_csrRowPtr==NULL )
    //  CUDA_SAFE_CALL( cudaMalloc( &C.d_csrRowPtr, (A_nrows+1)*sizeof(Index) ));

		// Analyze
    status = cusparseXcsrgemmNnz( handle,
        CUSPARSE_OPERATION_NON_TRANSPOSE, CUSPARSE_OPERATION_NON_TRANSPOSE,
				A_nrows, B_ncols, A_ncols, 
				descr, A_nvals, A.d_csrRowPtr, A.d_csrColInd, 
				descr, B_nvals, B.d_csrRowPtr, B.d_csrColInd,
        descr, C.d_csrRowPtr, nnzTotalDevHostPtr );

    switch( status ) {
        case CUSPARSE_STATUS_SUCCESS:
            //std::cout << "SpMM successful!\n";
            break;
        case CUSPARSE_STATUS_NOT_INITIALIZED:
            std::cout << "Error: Library not initialized.\n";
            break;
        case CUSPARSE_STATUS_INVALID_VALUE:
            std::cout << "Error: Invalid parameters m, n, or nnz.\n";
            break;
        case CUSPARSE_STATUS_EXECUTION_FAILED:
            std::cout << "Error: Failed to launch GPU.\n";
            break;
        case CUSPARSE_STATUS_ALLOC_FAILED:
            std::cout << "Error: Resources could not be allocated.\n";
            break;
        case CUSPARSE_STATUS_ARCH_MISMATCH:
            std::cout << "Error: Device architecture does not support.\n";
            break;
        case CUSPARSE_STATUS_INTERNAL_ERROR:
            std::cout << "Error: An internal operation failed.\n";
            break;
        case CUSPARSE_STATUS_MATRIX_TYPE_NOT_SUPPORTED:
            std::cout << "Error: Matrix type not supported.\n";
    }

    if( nnzTotalDevHostPtr != NULL )
			C_nvals = *nnzTotalDevHostPtr;
    else {
      CUDA_SAFE_CALL( cudaMemcpy( &(C_nvals), C.d_csrRowPtr+A_nrows, 
					sizeof(Index), cudaMemcpyDeviceToHost ));
			CUDA_SAFE_CALL( cudaMemcpy( &(baseC), C.d_csrRowPtr, 
				  sizeof(Index), cudaMemcpyDeviceToHost ));
			C_nvals -= baseC;
		}

		if( C_nvals >= C.nvals_ ) {
			CUDA_SAFE_CALL( cudaFree( C.d_csrColInd ));
			CUDA_SAFE_CALL( cudaFree( C.d_csrVal    ));
		  CUDA_SAFE_CALL( cudaMalloc( (void**) &C.d_csrColInd, C_nvals*sizeof(c) ));
		  CUDA_SAFE_CALL( cudaMalloc( (void**) &C.d_csrVal,    C_nvals*sizeof(c) ));
		}

    C.need_update = true;  // Set flag that we need to copy data from GPU
    C.nvals_ = C_nvals;     // Update nnz count for C
		return GrB_SUCCESS;
  }

  template<typename c, typename a, typename b>
  Info cusparse_spgemm_compute( SparseMatrix<c>&       C,
                                const Semiring&        op,
                                const SparseMatrix<a>& A,
                                const SparseMatrix<b>& B )
  {
    Index A_nrows, A_ncols, A_nvals;
    Index B_nrows, B_ncols, B_nvals;
    Index C_nrows, C_ncols;

    A.nrows( A_nrows );
    A.ncols( A_ncols );
    A.nvals( A_nvals );
    B.nrows( B_nrows );
    B.ncols( B_ncols );
		B.nvals( B_nvals );
    C.nrows( C_nrows );
    C.ncols( C_ncols );

    // Dimension compatibility check
    if( (A_ncols != B_nrows) || (C_ncols != B_ncols) || (C_nrows != A_nrows ) )
    {
      std::cout << "Dim mismatch" << std::endl;
      std::cout << A_ncols << " " << B_nrows << std::endl;
      std::cout << C_ncols << " " << B_ncols << std::endl;
      std::cout << C_nrows << " " << A_nrows << std::endl;
      return GrB_DIMENSION_MISMATCH;
    }

    // Domain compatibility check
    // TODO: add domain compatibility check

    // SpGEMM Computation
    cusparseHandle_t handle;
    cusparseCreate( &handle );
    cusparseSetPointerMode( handle, CUSPARSE_POINTER_MODE_HOST );

    cusparseMatDescr_t descr;
    cusparseCreateMatDescr( &descr );

    cusparseSetMatType( descr, CUSPARSE_MATRIX_TYPE_GENERAL );
    cusparseSetMatIndexBase( descr, CUSPARSE_INDEX_BASE_ZERO );
    cusparseStatus_t status;

    // Compute
    status = cusparseScsrgemm( handle,
        CUSPARSE_OPERATION_NON_TRANSPOSE, CUSPARSE_OPERATION_NON_TRANSPOSE,
				A_nrows, B_ncols, A_ncols, 
				descr, A_nvals, A.d_csrVal, A.d_csrRowPtr, A.d_csrColInd, 
				descr, B_nvals, B.d_csrVal, B.d_csrRowPtr, B.d_csrColInd,
        descr,          C.d_csrVal, C.d_csrRowPtr, C.d_csrColInd );

    switch( status ) {
        case CUSPARSE_STATUS_SUCCESS:
            //std::cout << "SpMM successful!\n";
            break;
        case CUSPARSE_STATUS_NOT_INITIALIZED:
            std::cout << "Error: Library not initialized.\n";
            break;
        case CUSPARSE_STATUS_INVALID_VALUE:
            std::cout << "Error: Invalid parameters m, n, or nnz.\n";
            break;
        case CUSPARSE_STATUS_EXECUTION_FAILED:
            std::cout << "Error: Failed to launch GPU.\n";
            break;
        case CUSPARSE_STATUS_ALLOC_FAILED:
            std::cout << "Error: Resources could not be allocated.\n";
            break;
        case CUSPARSE_STATUS_ARCH_MISMATCH:
            std::cout << "Error: Device architecture does not support.\n";
            break;
        case CUSPARSE_STATUS_INTERNAL_ERROR:
            std::cout << "Error: An internal operation failed.\n";
            break;
        case CUSPARSE_STATUS_MATRIX_TYPE_NOT_SUPPORTED:
            std::cout << "Error: Matrix type not supported.\n";
    }

    C.need_update = true;  // Set flag that we need to copy data from GPU
		return GrB_SUCCESS;
  }
}  // backend
}  // graphblas

#endif  // GRB_BACKEND_SEQUENTIAL_SPGEMM_HPP
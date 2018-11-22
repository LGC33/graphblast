#ifndef GRB_ALGORITHM_GC_HPP
#define GRB_ALGORITHM_GC_HPP

#include "graphblas/algorithm/testGc.hpp"
#include "graphblas/backend/cuda/util.hpp" // GpuTimer

namespace graphblas
{
  template <typename T_in1, typename T_out=T_in1>
  struct set_random
  {
    set_random()
    {
      seed_ = getEnv("GRB_SEED", 0);
      srand(seed_);
    }

    inline GRB_HOST_DEVICE T_out operator()(T_in1 lhs)
    { return rand(); }

    int seed_;
  };

namespace algorithm
{
  // Use float for now for both v and A
  float gc( Vector<float>*       v,
            const Matrix<float>* A,
            int                  seed,
            Descriptor*          desc )
  {
    Index A_nrows;
    CHECK( A->nrows(&A_nrows) );

    // Visited vector (use float for now)
    CHECK( v->fill(0.f) );

    // Frontier vectors (use float for now)
    Vector<float> q1(A_nrows);
    Vector<float> q2(A_nrows);

    Desc_value desc_value;
    CHECK( desc->get(GrB_MXVMODE, &desc_value) );
    CHECK( q1.fill(0.f) );

    // Weight vectors (use float for now)
    Vector<float> w(A_nrows);
    CHECK( w.fill(0.f) );

    // Set seed
    setEnv("GRB_SEED", seed);

    desc->set(GrB_BACKEND, GrB_SEQUENTIAL);
    apply<float,float,float>(&w, GrB_NULL, GrB_NULL, set_random<float>(), &w, desc);
    CHECK( w.print() );
    desc->set(GrB_BACKEND, GrB_CUDA);

    float iter = 1;
    float succ = 0.f;
    Index unvisited = A_nrows;
    backend::GpuTimer gpu_tight;
    if( desc->descriptor_.timing_>0 )
      gpu_tight.Start();
    do
    {
      if( desc->descriptor_.debug() )
      {
        std::cout << "=====Iteration " << iter - 1 << "=====\n";
        v->print();
        q1.print();
      }
      if( desc->descriptor_.timing_==2 )
      {
        gpu_tight.Stop();
        if (iter > 1)
        {
          std::string vxm_mode = (desc->descriptor_.lastmxv_ == GrB_PUSHONLY) ?
              "push" : "pull";
          std::cout << iter - 1 << ", " << succ << "/" << A_nrows << ", "
              << unvisited << ", " << vxm_mode << ", "
              << gpu_tight.ElapsedMillis() << "\n";
        }
        unvisited -= (int)succ;
        gpu_tight.Start();
      }
      assign<float,float>(v, &q1, GrB_NULL, iter, GrB_ALL, A_nrows, desc);
      CHECK( desc->toggle(GrB_MASK) );
      vxm<float,float,float,float>(&q2, v, GrB_NULL, 
          LogicalOrAndSemiring<float>(), &q1, A, desc);
      //vxm<float,float,float,float>(&q2, v, GrB_NULL, 
      //    PlusMultipliesSemiring<float>(), &q1, A, desc);
      CHECK( desc->toggle(GrB_MASK) );
      if (desc->descriptor_.debug())
        q2.print();
      CHECK( q2.swap(&q1) );
      if (desc->descriptor_.debug())
        q1.print();
      reduce<float,float>(&succ, GrB_NULL, PlusMonoid<float>(), &q1, desc);

      iter++;
      if (desc->descriptor_.debug())
        std::cout << "succ: " << succ << " " << (int)succ << std::endl;
      if (iter > desc->descriptor_.max_niter_)
        break;
    } while (succ > 0);
    if( desc->descriptor_.timing_>0 )
    {
      gpu_tight.Stop();
      std::string vxm_mode = (desc->descriptor_.lastmxv_ == GrB_PUSHONLY) ?
          "push" : "pull";
      std::cout << iter - 1 << ", " << succ << "/" << A_nrows << ", "
          << unvisited << ", " << vxm_mode << ", "
          << gpu_tight.ElapsedMillis() << "\n";
      return gpu_tight.ElapsedMillis();
    }
    return 0.f;
  }

  template <typename T, typename a>
  int gcCpu( Index        source,
             Matrix<a>*   A,
             T*           h_bfs_cpu,
             Index        depth,
             bool         transpose=false )
  {
    Index* reference_check_preds = NULL;
    int max_depth;

    if( transpose )
      max_depth = SimpleReferenceGc<T>( A->matrix_.nrows_, 
          A->matrix_.sparse_.h_cscColPtr_, A->matrix_.sparse_.h_cscRowInd_, 
          h_bfs_cpu, reference_check_preds, source, depth);
    else
      max_depth = SimpleReferenceGc<T>( A->matrix_.nrows_, 
          A->matrix_.sparse_.h_csrRowPtr_, A->matrix_.sparse_.h_csrColInd_, 
          h_bfs_cpu, reference_check_preds, source, depth);

    return max_depth; 
  }

}  // algorithm
}  // graphblas

#endif  // GRB_ALGORITHM_GC_HPP

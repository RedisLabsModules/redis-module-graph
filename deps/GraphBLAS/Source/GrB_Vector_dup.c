//------------------------------------------------------------------------------
// GrB_Vector_dup: make a deep copy of a sparse vector
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2019, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

// w = u, making a deep copy

// parallel: not here, but in GB_dup.

#include "GB.h"

GrB_Info GrB_Vector_dup     // make an exact copy of a vector
(
    GrB_Vector *w,          // handle of output vector to create
    const GrB_Vector u      // input vector to copy
)
{ 

    //--------------------------------------------------------------------------
    // check inputs
    //--------------------------------------------------------------------------

    GB_WHERE ("GrB_Vector_dup (&w, u)") ;
    GB_RETURN_IF_NULL (w) ;
    GB_RETURN_IF_NULL_OR_FAULTY (u) ;
    Context->nthreads = GxB_DEFAULT ;   // no descriptor, so use default rule
    ASSERT (GB_VECTOR_OK (u)) ;

    //--------------------------------------------------------------------------
    // duplicate the vector
    //--------------------------------------------------------------------------

    return (GB_dup ((GrB_Matrix *) w, (GrB_Matrix) u, Context)) ;
}


#pragma once 
#include "wfc.h"


void Sorted_gvecs_idxs(const ND_int npw, ELPH_float * gvecs, ND_int * indices);


void find_gvecs_idxs(const ND_int nsearchs, ELPH_float * search_gvecs, \
            const ND_int npw, ELPH_float * gvecs, ND_int * restrict out_idx);



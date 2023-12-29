#include "symmetries.h"
#include "../wfc/wfc.h"

#define bool2int(bval) (bval ? 1 : 0)
/*
Note that the bool2int() is not needed in practice, the standard 
anyways mandate that true = 1 and false = 0. this is 
just for readability. Any decent compiler will remove this.
*/

#define XOR(a,b) ((bool2int(a) + bool2int(b))%2)



void electronic_reps(const struct WFC * wfcs, const struct Lattice * lattice, \
    const ELPH_float * Rsym_mat,  const ELPH_float * Rsym_v, \
    const bool tim_revR, const ND_int ikBZ, ELPH_cmplx * Dkmn_rep, MPI_Comm commK)
{
    /*
    This is a function to compute representation matrices for the given symmetry and k point
    Dkmn_rep (<b Rk |U(R) | k,a>): (nspin, b_{bnd} , a_{bnd}) 
    */
    // compute the Rk vector and find it in the list of k-points
    ELPH_float Rk_vec[3] = {0,0,0};

    // start of small scope
    { 
    ELPH_float Rk_tmp[3] = {0,0,0};
    MatVec3f(Rsym_mat, lattice->kpt_fullBZ->data + 3*ikBZ, false, Rk_tmp);
    // convert to crystal coordinates
    MatVec3f(lattice->alat_vec->data,Rk_tmp, true, Rk_vec);
    } 
    // end of scope
    
    // now find the index of the rotated k point
    ND_int iRkBZ = -1;
    // find the index
    for (ND_int ik = 0; ik < lattice->kpt_fullBZ_crys->dims[0]; ++ik)
    {
        ELPH_float * ik_vec_tmp = lattice->kpt_fullBZ_crys->data + 3*ik ;
        ELPH_float sum = 0;
        for (int i = 0; i < 3 ; ++i)
        {   
            ELPH_float diff_tmp = ik_vec_tmp[i]-Rk_vec[i];
            diff_tmp = diff_tmp-rint(diff_tmp);
            sum += diff_tmp*diff_tmp;
        }
        sum = sqrt(sum);
        if (sum < ELPH_EPS)
        {
            iRkBZ = ik;
            break;
        }
    }

    if (iRkBZ < 0) error_msg("Rotated k point not found. Either Wrong Phonon symmetry or using non-uniform kgrid");

    

    /*
    $\langle Rk,b |U(R) | k,a \rangle = \langle Sym_2*k_2,b | \big(U(R) U(Sym_1) | k_1,a\rangle\big) $
    where Rk = Sym_2*k_2  and k = Sym_1*k_1  (upto to a G-vector)
    
    => $ \langle Rk,b |U(R) | k,a \rangle  = \Big(\big(\langle k_1, a |U^\dagger(Sym_1) U^\dagger(R)\big) | Sym_2*k_2 ,b \rangle\Big)^* \\
    = \Big(\langle k_1, a |\big( U^\dagger(Sym_1) U^\dagger(R) | Sym_2*k_2 ,b \rangle\big)\Big)^{1+C(Sym_1) + C(R)}$
    where C(A) = 1 if A is time reversal symmetry else 0
    if  {1+C(Sym_1) + C(R)} = odd implies conjugation of bracket else (in case of even) does nothing
    */

    /*
    Note that we apply all the symmetry operations to k2 and leave k1 wavefunction untouched. 
    This will avoid expensive memory copies and memory usage
    */

    // First get the corresponding iBZ point and symmetry for k and Rk
    const int ik1      = *(lattice->kmap->data + ikBZ*2)      ;
    const int iSym1     = *(lattice->kmap->data + ikBZ*2 + 1)  ;

    const int ik2      = *(lattice->kmap->data + iRkBZ*2)      ;
    const int iSym2     = *(lattice->kmap->data + iRkBZ*2 + 1)  ;

    const ELPH_float * Sym1  = lattice->sym_mat->data + 9*iSym1;
    const ELPH_float * Sym2  = lattice->sym_mat->data + 9*iSym2;

    const bool tr1 = lattice->time_rev_array[iSym1];
    const bool tr2 = lattice->time_rev_array[iSym2];

    const ELPH_float * tau1  = ND_function(ele,Nd_floatS)(lattice->frac_trans, nd_idx{iSym1,0});
    const ELPH_float * tau2  = ND_function(ele,Nd_floatS)(lattice->frac_trans, nd_idx{iSym2,0});

    // Get the wfcs in iBZ
    //
    const ELPH_cmplx * wfc_k1 = (wfcs+ik1)->wfc->data ;
    const ELPH_cmplx * wfc_k2 = (wfcs+ik2)->wfc->data ;

    const ELPH_float * gvecs_k1 = (wfcs+ik1)->gvec->data ;
    const ELPH_float * gvecs_k2 = (wfcs+ik2)->gvec->data ;

    const ND_int npw_k1_loc = (wfcs+ik1)->npw_loc ;
    const ND_int npw_k2_loc = (wfcs+ik2)->npw_loc ;

    const ND_int npw_k1_total = (wfcs+ik1)->npw_total ;
    const ND_int npw_k2_total = (wfcs+ik2)->npw_total ;


    /*
    U^\dagger(Sym_1) U^\dagger(R) | Sym_2*k_2\rangle  = U^\dagger(Sym_1) U^\dagger(R) U(Sym_2)|k_2\rangle 

    If we write time reversal as T = UK where K is complex conjugation operator and U is unitary
    then
    U^\dagger(Sym_1) U^\dagger(R) U(Sym_2) = U^\dagger(Sym_1) K(Sym_1) U^\dagger(R) K(R) K(Sym_2) U(Sym_2)
    where K(A) = K if A is time reversal else ignored

    It is important to note that the conjugation is passed to right side for the inverse operators

    => We rotate the G-vectors with symmtry matrix 
        Sym123= Sym_1^{-1}@R^{-1}@Sym_2
    and the fractional translation part is 
        tau_123 = -Sym_1^{-1}(tau_1)- Sym_1^{-1}@R^{-1}*(tau_R)*(-1)^{T(Sym_1)} 
            + Sym_1^{-1}@R^{-1}*(tau_2)*(-1)^{T(Sym_1) + T(R) + T(Sym_2)} 
    
    C_G = cong(C_G) if t(Sym_1) + t(R) + t(Sym_2) is odd
    where t(A) = 1 for time reversal else 0
    */
    
    //  R or Sym1 is time reversal then we must conjugate i.e Dmats = conj(Dmats)
    // {1+C(Sym_1) + C(R)} 
    const bool conj_dmat = ( bool2int(tr1) + bool2int(tim_revR) + 1)%2;
    
    // t(Sym_1) + t(R) + t(Sym_2) 
    const bool conj_123 =  (bool2int(tr1) + bool2int(tim_revR) + bool2int(tr2))%2 ; 


    ELPH_cmplx SU2_mat123[4]={1,0,0,1};  // SU^\dagger(S1)@SU^\dagger(R)@SU(S2)

    // initiate to I_2x2. if nspinor == 1 then only first element in considered
    // compute SU(2) mats
    if (lattice->nspinor == 2)
    { 
        ELPH_cmplx SU2_tempS1[4]={1,0,0,1}; //SU^dagger(S1)
        ELPH_cmplx SU2_tempR[4]={1,0,0,1}; //SU^dagger(R)
        ELPH_cmplx SU2_tempS2[4]={1,0,0,1}; //SU(S2)

        ELPH_cmplx SU2_temp[4]={1,0,0,1}; // temp
        
        // Note the U^-1 from time reversal is include in SU2 mats
        SU2mat(Sym1, lattice->nspinor, true, tr1, SU2_tempS1); //SU^dagger(S1) 
        SU2mat(Rsym_mat, lattice->nspinor, true, tim_revR, SU2_tempR); //SU^dagger(R)
        SU2mat(Sym2, lattice->nspinor, false, tr2, SU2_tempS2); //SU(S2)

        // conjugate if required (due to time reversal symmetries)
        for (ND_int i = 0; i < (lattice->nspinor*lattice->nspinor); ++i)
        {
            if (tr1) SU2_tempR[i] = conj(SU2_tempR[i]); // we apply conjugation from Sym_1^-1 
            if (conj_123) SU2_tempS2[i] = conj(SU2_tempS2[i]); 
            // we apply conjugation from Sym_1^-1, R^-1 and Sym_2 
        }
        // compute the product of the 3 su(2) mats i.e 
        // SU^\dagger(S1)@SU^\dagger(R)@SU(S2)
        matmul_Cmpl2x2(SU2_tempS1, SU2_tempR, SU2_temp); // SU^\dagger(S1)@SU^\dagger(R)
        matmul_Cmpl2x2(SU2_temp, SU2_tempS2, SU2_mat123); // SU^\dagger(S1)@SU^\dagger(R)@SU(S2)
    } 

    // Compute the total rotation matrix and frac trans
    ELPH_float Sym123[9] = {0,0,0,0,0,0,0,0,0}; // rotation matrix
    ELPH_float tau_123[3] = {0,0,0}; // frac trans

    { // create a small scope 11
        // 1st compute the rotational matrix
        ELPH_float Sym_1R_temp[9] = {0,0,0,0,0,0,0,0,0}; //Sym1^{-1}@R^{-1} i.e Sym1^{T}@R^{T}
        Gemm3x3f(Sym1, 'T', Rsym_mat,'T',Sym_1R_temp);
        Gemm3x3f(Sym_1R_temp, 'N', Sym2,'N',Sym123); //Sym1^{-1}@R^{-1}@Sym2 i.e,  Sym1^{T}@R^{T}@Sym2
        // fractional translation
        // v1 = tau_2, v2 = -R^{-1}Rsym_v, v3 = -Sym1^{-1}tau_1

        ELPH_float v1_tmp[3] = {0,0,0};
        ELPH_float v12_tmp[3] = {0,0,0};

        ELPH_float v1_fac = 1; 
        ELPH_float v2_fac = 1; 
        ELPH_float v3_fac = 1; 

        if (conj_123) v1_fac = -1; // (-1)^{T(A1) + T(A2) + T(A3)}
        if(tr1) v2_fac = -1; // (-1)^{ T(A2) + T(A3)}

        // v1_tmp = Sym1^{T}@R^{T}*(tau_2*v1_fac -Rsym_v*v2_fac )
        for (int i = 0 ; i<3 ; ++i) v1_tmp[i] = v1_fac*tau2[i]-Rsym_v[i]*v2_fac;
        MatVec3f(Sym_1R_temp, v1_tmp, false, v12_tmp);
        // -Sym1^{-1}(tau_1)*v3_fac}
        MatVec3f(Sym1, tau1, true, tau_123); // Sym1^{-1}*tau_1
        for (int i = 0 ; i<3 ; ++i) tau_123[i]  = tau_123[i]*(-1)*v3_fac + v12_tmp[i];
    } // end of scope 11

    // Now rotate the gvecs of ik2 and convert to crystal units
    
    ELPH_float * S1G_crys   = malloc(sizeof(ELPH_float)*3*npw_k1_loc); 
    // gvectors for k1 point in crystal coordinate
    ELPH_float * S123G_crys = malloc(sizeof(ELPH_float)*3*npw_k2_loc); 
    // gvectors for k2 point in crystal coordinate rotated with Sym1^{T}@R^{T}@Sym2

    { // create a small scope 12
        const ELPH_float Iden3x3[9] = {1,0,0,0,1,0,0,0,1};
        ELPH_float G0[3] = {0,0,0}; // ulmvec (in cart) G0 + Sym1^{T}@R^{T}@Sym2*k2 = k1 -> G0 = k1-Sym1^{T}@R^{T}@Sym2*k2
        // note C'_{G-G0} = C_G, so we need add -G0
        // get the iBZ kpoints k1 and k2
        const ELPH_float * k1_vec = lattice->kpt_iredBZ->data + 3*ik1;
        const ELPH_float * k2_vec = lattice->kpt_iredBZ->data + 3*ik2;
        MatVec3f(Sym123, k2_vec, false, G0); // Sym1^{T}@R^{T}@Sym2*k2
        for (int i = 0; i<3; ++i) G0[i] -= k1_vec[i];  //-G0 = Sym1^{T}@R^{T}@Sym2*k2-k1
        // convert gvecs of k1 to cystal coorinates
        rotateGvecs(gvecs_k1, Iden3x3, npw_k1_loc, lattice->alat_vec->data, false, true, NULL, S1G_crys);
        // convert gvecs to k2 -> Sym1^{T}@R^{T}@Sym2*k2
        rotateGvecs(gvecs_k2,  Sym123, npw_k2_loc, lattice->alat_vec->data, false, true, G0, S123G_crys);
    } // end of scope  12
	
    // Now we need to rearrange the wavefunctions. We need to find the indices of S123G_crys in S1G_crys
    int my_rank, Comm_size, mpi_error;
    mpi_error = MPI_Comm_size(commK, &Comm_size);
    mpi_error = MPI_Comm_rank(commK, &my_rank);

    // First get S123G_crys and S1G_crys on the root node
    ND_int * idx_arr = NULL ; // This is the array that maps S123G_crys to S1G_crys; (only allocated in root)
    ELPH_float * S123Gvecs_all = NULL; // all gvectors collected on root process
    ELPH_float * S1Gvecs_all = NULL; // all gvectors collected on root process

    // mpi buffers
    int * counts = NULL; 
    int * disp = NULL;

    int * counts2 = NULL; 
    int * disp2 = NULL;

    if (my_rank == 0)
    {   
        S1Gvecs_all      = malloc(sizeof(ELPH_float)*npw_k1_total*3);
        S123Gvecs_all    = malloc(sizeof(ELPH_float)*npw_k2_total*3);
        idx_arr          = malloc(sizeof(ND_int)*npw_k2_total);
        counts           = malloc(4*sizeof(int)*Comm_size);
        disp             = counts +   Comm_size ;
        counts2          = counts + 2*Comm_size ;
        disp2            = counts + 3*Comm_size ;
        

        if (counts   == NULL) error_msg("Failed to allocate comm array");
        if (S123Gvecs_all == NULL)   error_msg("Failed to allocate gvec123 array");
        if (S1Gvecs_all == NULL)   error_msg("Failed to allocate gvec1 array");
        if (idx_arr == NULL)     error_msg("Failed to allocate indices array");
    }
    

    int pw_loc_int = 3*npw_k1_loc;
    mpi_error = MPI_Gather(&pw_loc_int, 1, MPI_INT, counts, 1, MPI_INT, 0, commK);
    if (my_rank == 0)
    {
        int disp_tmp = 0;
        for (int i = 0 ; i<Comm_size; ++i)
        {
            disp[i] = disp_tmp;
            disp_tmp += counts[i];
        }
    }
    // gather the k1 gvecs
    MPI_Gatherv(S1G_crys,pw_loc_int,ELPH_MPI_float, \
        S1Gvecs_all,counts,disp,ELPH_MPI_float,0,commK);

    pw_loc_int = 3*npw_k2_loc;
    mpi_error = MPI_Gather(&pw_loc_int, 1, MPI_INT, counts, 1, MPI_INT, 0, commK);
    if (my_rank == 0)
    {
        int disp_tmp = 0;
        for (int i = 0 ; i<Comm_size; ++i)
        {
            disp[i] = disp_tmp;
            disp_tmp += counts[i];
        }
    }
    
    // gather the S1^-1@R^-1@S2*k2 gvecs
    MPI_Gatherv(S123G_crys,pw_loc_int,ELPH_MPI_float, \
        S123Gvecs_all,counts, disp, ELPH_MPI_float, 0, commK);
    
    // get the indices
    if (my_rank == 0)
    {   
        find_gvecs_idxs(npw_k2_total, S123Gvecs_all, npw_k1_total, S1Gvecs_all, idx_arr);
        // free some space
        free(S123Gvecs_all);
        free(S1Gvecs_all);
    }
    
    // now we map the wavefunctions
    // allocate memory for rearranged space
    ELPH_cmplx * wfc_k2_root = NULL ; // gather ik2 wavefunctions on root for sorting
    ELPH_cmplx * wfc_k2_sort_root = NULL ; // store sorted ik2 on root

    if (my_rank == 0)
    {   
        wfc_k2_root = malloc(sizeof(ELPH_cmplx)*npw_k2_total);
        wfc_k2_sort_root = malloc(sizeof(ELPH_cmplx)*npw_k1_total);
    }
    
    // wfc_k2_sort_root is scatter to the local buffers
    ELPH_cmplx *  wfc_k2_sorted = malloc(sizeof(ELPH_cmplx)*lattice->nspinor*npw_k1_loc); 
    if (wfc_k2_sorted == NULL) error_msg("Allocation of sorted local buffer failed");

    ND_int nsets = lattice->nspin*lattice->nbnds;

    pw_loc_int = npw_k1_loc;
    mpi_error = MPI_Gather(&pw_loc_int, 1, MPI_INT, counts, 1, MPI_INT, 0, commK);

    if (my_rank == 0)
    {
        int disp_tmp = 0;
        for (int i = 0 ; i<Comm_size; ++i)
        {
            disp[i] = disp_tmp;
            disp_tmp += counts[i];
        }
    }
    
    pw_loc_int = npw_k2_loc;
    mpi_error = MPI_Gather(&pw_loc_int, 1, MPI_INT, counts2, 1, MPI_INT, 0, commK);
    
    if (my_rank == 0)
    {
        int disp_tmp = 0;
        for (int i = 0 ; i<Comm_size; ++i)
        {
            disp2[i] = disp_tmp;
            disp_tmp += counts2[i];
        }
    }

    // create a tmp buffer
    ELPH_cmplx * Dkmn_rep_tmp = calloc(lattice->nbnds,sizeof(ELPH_cmplx));

    // Now rearrage the wavefunctin, and compute the sandwitch
    for (ND_int iset =0; iset<nsets; ++iset)
    {   
        ND_int ispin = iset/lattice->nbnds ;

        const ELPH_cmplx * wfc_k2_tmp = wfc_k2 + iset*lattice->nspinor*npw_k2_loc;
    
        for (ND_int ispinor=0; ispinor<lattice->nspinor; ++ispinor)
        {
            // gather the wfc on root process
            mpi_error = MPI_Gatherv(wfc_k2_tmp + ispinor*npw_k2_loc, npw_k2_loc, ELPH_MPI_cmplx, \
                            wfc_k2_root, counts2, disp2, ELPH_MPI_cmplx, 0, commK);
            if(my_rank == 0)
            {
                //rearrange
                // 
                for (ND_int ig = 0; ig<npw_k1_total; ++ig) wfc_k2_sort_root[ig] = 0;
                for (ND_int ii = 0; ii < npw_k2_total; ++ii)
                {
                    ND_int idx_tmp = idx_arr[ii];
                    if (idx_tmp < 0) continue; // set the missing ones to 0
                    wfc_k2_sort_root[idx_tmp] = wfc_k2_root[ii];
                }
            }
            // scatter back the wfc to each process
            mpi_error = MPI_Scatterv(wfc_k2_sort_root, counts, disp, ELPH_MPI_cmplx, \
                        wfc_k2_sorted + ispinor*npw_k1_loc, npw_k1_loc, ELPH_MPI_cmplx, 0, commK);
            
            // complex conjugate the C_G due to time reversal
            if (conj_123) 
            {   
                ELPH_cmplx * tmp_ptr = wfc_k2_sorted + ispinor*npw_k1_loc;
                for (ND_int ig = 0; ig<npw_k1_loc; ++ig) tmp_ptr[ig] = conj(tmp_ptr[ig]);
            }
        }
        // rotate the spinors
        su2rotate(lattice->nspinor, npw_k1_loc, 1, SU2_mat123, wfc_k2_sorted);

        ND_int g_pw_tmp = lattice->nspinor*npw_k1_loc;
        
        // compute the sandwitch
        ND_function(matmulX, Nd_cmplxS) ('N', 'C', wfc_k2_sorted, wfc_k1 + ispin*lattice->nbnds*g_pw_tmp, \
                Dkmn_rep_tmp, 1.0, 0.0, g_pw_tmp, g_pw_tmp, lattice->nbnds, 1, lattice->nbnds, g_pw_tmp);
        // (1,pw)@ (nbn,pw)^C

        // Conjuge D_mat if one of Sym1 or R is time rev
        if (conj_dmat)
        {
            for (ND_int ibnd=0; ibnd < lattice->nbnds; ++ibnd) Dkmn_rep_tmp[ibnd] = conj(Dkmn_rep_tmp[ibnd]);
        }
        // reduce to root node
        ELPH_cmplx * Dkmn_ptr = NULL;
        if (my_rank == 0) Dkmn_ptr = Dkmn_rep + iset*lattice->nbnds;
        MPI_Reduce(Dkmn_rep_tmp, Dkmn_ptr, lattice->nbnds, ELPH_MPI_cmplx, MPI_SUM, 0, commK);

    }

    free(Dkmn_rep_tmp);
    free(wfc_k2_sorted);
    free(S1G_crys);
    free(S123G_crys);

    if (my_rank == 0)
    {
        free(wfc_k2_root);
        free(wfc_k2_sort_root);
        free(counts);
        free(idx_arr);  
    }

}













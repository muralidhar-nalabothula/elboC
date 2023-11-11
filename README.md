The main prototype of the code is done but there are some minor issues. 


# TODO (Short term). 
1) Distribute wfcs across all the kpools (now data is duplicated)
2) Compute the representation matrices to rotate in q space
3) Rearrange non-local part (compute beta functions at the start)
4) Prepare test cases
5) Improve OPENMP

# TODOs (LONG TERM)
1) Prepare an interface 
2) read dvscf directlty from binary


# Some Uncertain improvements (should be addressed after return)
1) Improve IO performace(AION and IRIS FS report very bad file open times)
2) dVloc routines needs to be slightly rewritten to avoid multiple array allocations



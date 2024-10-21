## runs jobs
try:
    from configparser import ConfigParser
except ImportError:
    from ConfigParser import ConfigParser

from check_data import check_dmat_files, check_elph_files
from functools import reduce
from netCDF4 import Dataset
import os




def make_inp_file(dict_list,qpools=1,kpools=1):
    # pass the section to this function to create a letzelph input file
    ### create letzelph input file
    f = open("test_input.in", "w")
    f.write("## Input generated by LetzElPhC testsuite.\n")
    ## Write k and q pools
    f.write("nkpool = %d \n" %(int(kpools)))
    f.write("nqpool = %d \n" %(int(qpools)))
    # write the user provided data
    for i in dict_list:
        if i.lower().strip() == 'elph_db_ref' or i.lower().strip() == 'dmat_db_ref':
            continue
        else:
            f.write("%s = %s \n" %(i.strip(),dict_list[i]))
    f.close()

## find factors
def find_factors(n):
    ## Copied from stackoverflow: https://stackoverflow.com/a/6800214
    return list(set(reduce(list.__add__, ([i, n//i] for i in range(1, int(n**0.5) + 1) if n % i == 0))))

## find all possible three factors :
def get_triplet(n, nq_max,nk_max):
    n_factors = find_factors(n)
    triplet = []
    for ifac in n_factors:
        ifac_factors = find_factors(ifac)
        for jfac in ifac_factors:
            kfac = ifac//jfac
            if ifac <= nq_max and jfac <=nk_max :
                triplet.append([ifac,jfac])
            if ifac == jfac:
                continue
            if jfac <= nq_max and ifac <=nk_max:
                triplet.append([jfac,ifac])
    return triplet


def run_test(ini_file, lelphc_cmd='lelphc', mpirun_cmd="mpirun", ncpus = 1, test_name=''):
    # return [total_tests, total_passes, total_fails]
    config = ConfigParser()
    config.read(ini_file)
    # # structure of the input file 
    # [DEFAULT]
    # # Parameters used in this sections will be used in all tests
    # # give the names of the references file
    # # elph_db_ref = 'ndb.elph_ref' is default
    # # dmat_db_ref = 'ndb.Dmats_ref' is default
    # [test1]
    # # add parameters
    ntotal_tests = 0
    nfailed_tests = 0
    npassed_tests = 0
    sections = list(config.keys())

    ncpus_set = [1] # always do a serial run
    if ncpus >1 : ncpus_set.append(ncpus)

    ## clear any resudual files
    os.system('rm test_input.in > /dev/null 2>&1')
    os.system('rm testlog > /dev/null 2>&1')
    os.system('rm ndb.elph > /dev/null 2>&1')
    os.system('rm ndb.Dmats > /dev/null 2>&1')

    print('=> Running test job : %s' %(test_name))
    for i in sections:
        if i.strip() != 'DEFAULT':
            in_total = 0
            in_failed =0
            in_passed = 0

            spilt_key = i.strip().split(' ')
            ## first get the code info default is qe
            code = 'qe'
            if len(spilt_key)>1:
                code = spilt_key[-1].strip()

            ## default reference files
            elph_db_ref = 'ndb.elph_ref'
            dmat_db_ref = 'ndb.Dmats_ref'
            if 'elph_db_ref' in config[i]:
                elph_db_ref = config[i]['elph_db_ref']
            if 'dmat_db_ref' in config[i]:
                dmat_db_ref =  config[i]['dmat_db_ref']
            
            try :
                elph_ref_db  = Dataset(elph_db_ref,'r')
                nkpool_max  = elph_ref_db['kpoints'][...].data.shape[0]
                nqpool_max = elph_ref_db['qpoints_iBZ'][...].data.shape[0]
                elph_ref_db.close()
            except:
                print("[Warning] Issue with Reference file : skipping the test.")
                continue
            
            for icpu in ncpus_set:
                processor_sets = get_triplet(icpu,nqpool_max,nkpool_max)
                for iset in processor_sets:
                    make_inp_file(config[i],iset[0],iset[1])
                    os.system("%s -n %d %s --code=%s -F test_input.in &> testlog" \
                        %(mpirun_cmd, icpu, lelphc_cmd, code))
                    
                    test_pass = check_dmat_files('ndb.Dmats', dmat_db_ref) \
                        and check_elph_files('ndb.elph', elph_db_ref)
                    ## remove files
                    os.system('rm test_input.in > /dev/null 2>&1')
                    os.system('rm testlog > /dev/null 2>&1')
                    os.system('rm ndb.elph > /dev/null 2>&1')
                    os.system('rm ndb.Dmats > /dev/null 2>&1')

                    in_total += 1
                    if test_pass : in_passed += 1
                    else :  in_failed += 1 

            print('| %10s results : Total = %8d, Passed = %8d, Failed = %8d |'\
                %(i.strip(),in_total,in_passed,in_failed))
            
            ntotal_tests += in_total
            nfailed_tests += in_failed
            npassed_tests += in_passed
    
    print('## ************* Job %s Results *************' %(test_name))
    print('## Number of tests ran    : %8d' %(ntotal_tests))
    print('## Number of passed tests : %8d' %(npassed_tests))
    print('## Number of failed tests : %8d' %(nfailed_tests))
    return [ntotal_tests,npassed_tests,nfailed_tests]





run_test('test.ini', lelphc_cmd='./lelphc', mpirun_cmd="mpirun", ncpus = 4, test_name='Sample')
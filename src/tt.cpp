#include "wave3d.hpp"
#include "parallel.hpp"
#include <exception>

#define CLOCK_DIFF_SECS(ck1, ck0) (double(ck1-ck0) / CLOCKS_PER_SEC)

int optionsCreate(int argc, char** argv, map<string,string>& options)
{
    options.clear();
    for(int k=1; k<argc; k=k+2) {
        options[ string(argv[k]) ] = string(argv[k+1]);
    }
    return 0;
}

string findOption(map<string,string>& opts, string option)
{
    map<string,string>::iterator mi = opts.find(option);
    if (mi == opts.end()) {
	std::cerr << "Missing option " << option << std::endl;
        return "";
    }
    return mi->second;
}

int main(int argc, char** argv)
{
#ifndef RELEASE
    // When not in release mode, we catch all errors so that we can print the
    // manual call stack.
    try {
#endif
        MPI_Init(&argc, &argv);
	int mpirank, mpisize;
	getMPIInfo(&mpirank, &mpisize);

        //0. init and get options
        srand48(time(NULL));
	iA(argc % 1 == 0);
        clock_t ck0, ck1;
        time_t t0, t1;
        vector<int> all(1,1);
        map<string,string> opts;
	optionsCreate(argc, argv, opts);
        map<string,string>::iterator mi;
        string opt;

        //1. read data
        ParVec<int, Point3, PtPrtn> pos;
        opt = findOption(opts, "-posfile");
        if (opt.empty()) {
            return 0;
        }

	std::istringstream piss;
        iC( Separate_Read(opt, piss) );
        iC( deserialize(pos, piss, all) );
	std::vector<int>& tmpinfo = pos.prtn().ownerinfo();
	// LEXING: numpts CONTAINS THE TOTAL NUMBER OF POINTS
        int numpts = tmpinfo[tmpinfo.size()-1];
        if (mpirank==0) {
	    std::cerr << "Total number of points: " << numpts << std::endl;
	    std::cerr << "Done reading pos " << pos.lclmap().size() << " "
		      << pos.prtn().ownerinfo().size() << std::endl;
        }
        ParVec<int, cpx, PtPrtn> den;

        opt = findOption(opts, "-denfile");
        if (opt.empty()) {
            return 0;
        }

	std::istringstream diss; iC( Separate_Read(opt, diss) );
        iC( deserialize(den, diss, all) );
        if(mpirank==0) {
            std::cerr << "Done reading den " << den.lclmap().size() << " "
		 << den.prtn().ownerinfo().size() << std::endl;
        }
        ParVec<int, cpx, PtPrtn> val; //preset val to be the same as den
        val = den;
        if(mpirank==0) {
            std::cerr << "Done setting val " << val.lclmap().size() << " "
		 << val.prtn().ownerinfo().size() << std::endl;
        }
        Kernel3d knl(KNL_HELM);
        mi = opts.find("-knl");
        // TODO (Austin): change this to the other format for getting an option
        if (mi != opts.end()) {
	    std::istringstream ss(mi->second);
            int type;
            ss >> type;
            knl.type() = type;
        }
        Mlib3d mlib("mlib3d_");
        mlib.knl() = knl;
        mlib.NPQ() = 4;
        iC( mlib.setup(opts) );
        if (mpirank == 0) {
            std::cerr << "Done reading mlib " << mlib.w2ldmap().size() << " "
		 << mlib.w2hdmap().size() << std::endl;
        }
        IntNumTns geomprtn;
        opt = findOption(opts, "-geomprtn");
        if (opt.empty()) {
            return 0;
        }
  
	std::istringstream giss;  iC( Shared_Read(opt, giss) );
        iC( deserialize(geomprtn, giss, all) );
        if(mpirank==0) {
            std::cerr << "Done reading geomprtn " << geomprtn.m() << " "
		 << geomprtn.n() << " " << geomprtn.p() << std::endl
		 << geomprtn << std::endl;
        }
        Wave3d wave("wave3d_");
        wave.posptr() = &pos;
        wave.knl() = knl;
        wave.mlibptr() = &mlib;
        wave.geomprtn() = geomprtn;

        //2. setup
        t0 = time(0);
        iC( wave.setup(opts) );
        t1 = time(0);
        if (mpirank == 0) {
	    std::cout << "wave setup used " << difftime(t1,t0) << "secs " << std::endl;
        }

        //3. eval
        double time_eval;
        t0 = time(0);
	iC( wave.eval(den, val) );

	t1 = time(0);
	if (mpirank == 0) {
	    std::cout << "wave eval used " << difftime(t1,t0) << "secs " << std::endl;
	}
	time_eval = difftime(t1,t0);

        std::ostringstream oss;
	iC( serialize(val, oss, all) );

	opt = findOption(opts, "-valfile");
	if (opt.empty()) {
	    return 0;
	}
	iC( Separate_Write(opt, oss) );

	//4. check
	IntNumVec chkkeyvec;
	opt = findOption(opts, "-chkfile");
	if (opt.empty()) {
	    return 0;
	}
        std::istringstream iss;
	iC( Shared_Read(opt, iss) );
	iC( deserialize(chkkeyvec, iss, all) );
	int numchk = chkkeyvec.m();
	double relerr;
	ck0 = clock();
	iC( wave.check(den, val, chkkeyvec, relerr) );
	ck1 = clock();
	if(mpirank==0) {
	    std::cout << "wave check used " << CLOCK_DIFF_SECS(ck1, ck0)
                      << "secs " << std::endl;
	}

	//5. output results
	double time_drct = CLOCK_DIFF_SECS(ck1, ck0) * numpts / double(numchk);
	if(mpirank==0) {
	    printf("----------------------\n");
	    printf("RESULT\n");
	    printf("K  %.2e\n", wave.K());
	    printf("Ta %.2e\n", time_eval);
	    printf("Td %.2e\n", time_drct);
	    printf("Rt %.2e\n", time_drct / time_eval);
	    printf("Ea %.6e\n", relerr);
	    printf("----------------------\n");
	}
	//
	iC( MPI_Finalize() );
#ifndef RELEASE
    } catch( ... ) {
	int mpirank;
	MPI_Comm_rank(MPI_COMM_WORLD, &mpirank);
	std::cerr << "Process " << mpirank << " caught error." << std::endl;
	DumpCallStack();
    }
#endif
    return 0;
}

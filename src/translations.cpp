/* Distributed Directional Fast Multipole Method
   Copyright (C) 2014 Austin Benson, Lexing Ying, and Jack Poulson

 This file is part of DDFMM.

    DDFMM is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DDFMM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DDFMM.  If not, see <http://www.gnu.org/licenses/>. */
#include "wave3d.hpp"

#include <utility>
#include <vector>

// TODO(arbenson): this is a bit of hack.
#define DVMAX 400

int Wave3d::HighFrequencyM2L(double W, Index3 dir, BoxKey trgkey, BoxDat& trgdat,
                             DblNumMat& dcp, DblNumMat& uep) {
#ifndef RELEASE
    CallStackEntry entry("Wave3d::HighFrequencyM2L");
#endif
    CHECK_TRUE(HasPoints(trgdat));  // should have points
    Point3 trgctr = BoxCenter(trgkey);
    //1. mix
    //get target
    DblNumMat tmpdcp(dcp.m(), dcp.n());
    for (int k = 0; k < tmpdcp.n(); ++k) {
        for (int d = 0; d < 3; ++d) {
            tmpdcp(d, k) = dcp(d, k) + trgctr(d);
        }
    }
    HFBoxAndDirectionKey bndkey(trgkey, dir);
    HFBoxAndDirectionDat& bnddat = _bndvec.access(bndkey);
    CpxNumVec& dcv = bnddat.dirdnchkval();
    std::vector<BoxKey>& tmpvec = trgdat.fndeidxvec()[dir];
    for (int i = 0; i < tmpvec.size(); ++i) {
        BoxKey srckey = tmpvec[i];
        Point3 srcctr = BoxCenter(srckey);
        //difference vector
        Point3 diff = trgctr - srcctr;
        diff /= diff.l2(); //LEXING: see wave3d_setup.cpp
        CHECK_TRUE( nml2dir(diff, W) == dir );
        //get source
        DblNumMat tmpuep(uep.m(),uep.n());
        for (int k = 0; k < tmpuep.n(); ++k) {
            for (int d = 0; d < 3; ++d) {
                tmpuep(d, k) = uep(d, k) + srcctr(d);
            }
        }
        HFBoxAndDirectionKey bndkey(srckey, dir);
        HFBoxAndDirectionDat& bnddat = _bndvec.access(bndkey);
        CpxNumVec& ued = bnddat.dirupeqnden();
        //mateix
        CpxNumMat Mts;
        SAFE_FUNC_EVAL( _kernel.kernel(tmpdcp, tmpuep, tmpuep, Mts) );
        //allocate space if necessary
        if (dcv.m() == 0) {
            dcv.resize(Mts.m());
            setvalue(dcv, cpx(0,0)); //LEXING: CHECK
        }
        //SAFE_FUNC_EVAL( ued.m() != 0 );
        if (ued.m() == 0) {
            ued.resize(Mts.n());
            setvalue(ued, cpx(0, 0));
        }
        SAFE_FUNC_EVAL( zgemv(1.0, Mts, ued, 1.0, dcv) );
    }
    return 0;
}

int Wave3d::HighFrequencyL2L(double W, Index3 dir, BoxKey trgkey,
                             NumVec<CpxNumMat>& dc2de,
                             NumTns<CpxNumMat>& de2dc) {
    double eps = 1e-12;
    HFBoxAndDirectionKey bndkey(trgkey, dir);
    HFBoxAndDirectionDat& bnddat = _bndvec.access(bndkey);
    CpxNumVec& dnchkval = bnddat.dirdnchkval();
    CpxNumMat& E1 = dc2de(0);
    CpxNumMat& E2 = dc2de(1);
    CpxNumMat& E3 = dc2de(2);
    cpx dat0[DVMAX], dat1[DVMAX], dat2[DVMAX];
    CpxNumVec tmp0(E3.m(), false, dat0);
    CpxNumVec tmp1(E2.m(), false, dat1);
    CpxNumVec dneqnden(E1.m(), false, dat2);
    SAFE_FUNC_EVAL( zgemv(1.0, E3, dnchkval, 0.0, tmp0) );
    SAFE_FUNC_EVAL( zgemv(1.0, E2, tmp0, 0.0, tmp1) );
    SAFE_FUNC_EVAL( zgemv(1.0, E1, tmp1, 0.0, dneqnden) );
    dnchkval.resize(0); //LEXING: SAVE SPACE

    if (abs(W - 1) < eps) {
        for (int ind = 0; ind < NUM_CHILDREN; ++ind) {
            int a = CHILD_IND1(ind);
            int b = CHILD_IND2(ind);
            int c = CHILD_IND3(ind);             
            BoxKey key = ChildKey(trgkey, Index3(a,b,c));
            std::pair<bool, BoxDat&> data = _boxvec.contains(key);
            // If the box was empty, it will not be stored
            if (!data.first) {
                continue;
            }
            BoxDat& chddat = data.second;
            CpxNumVec& chddcv = chddat.dnchkval();
            if (chddcv.m() == 0) {
                chddcv.resize(de2dc(a,b,c).m());
                setvalue(chddcv,cpx(0,0));
            }
            SAFE_FUNC_EVAL( zgemv(1.0, de2dc(a,b,c), dneqnden, 1.0, chddcv) );
        }
    } else {
        Index3 pdir = ParentDir(dir); //LEXING: CHECK
        for (int ind = 0; ind < NUM_CHILDREN; ++ind) {
            int a = CHILD_IND1(ind);
            int b = CHILD_IND2(ind);
            int c = CHILD_IND3(ind);             
            BoxKey chdkey = ChildKey(trgkey, Index3(a,b,c));
            std::pair<bool, BoxDat&> data = _boxvec.contains(chdkey);
            // If the box was empty, it will not be stored
            if (!data.first) {
                continue;
            }
            BoxDat& chddat = data.second;
            HFBoxAndDirectionKey bndkey(chdkey, pdir);
            HFBoxAndDirectionDat& bnddat = _bndvec.access(bndkey);
            CpxNumVec& chddcv = bnddat.dirdnchkval();
            if (chddcv.m() == 0) {
                chddcv.resize(de2dc(a,b,c).m());
                setvalue(chddcv,cpx(0,0));
            }
            SAFE_FUNC_EVAL( zgemv(1.0, de2dc(a,b,c), dneqnden, 1.0, chddcv) );
        }
    }
    return 0;
}


int Wave3d::U_list_compute(BoxDat& trgdat) {
#ifndef RELEASE
    CallStackEntry entry("Wave3d::U_list_compute");
#endif
    for (std::vector<BoxKey>::iterator vi = trgdat.undeidxvec().begin();
        vi != trgdat.undeidxvec().end(); ++vi) {
        BoxKey neikey = (*vi);
        BoxDat& neidat = _boxvec.access(neikey);
        CHECK_TRUE(HasPoints(neidat));
        //mul
        CpxNumMat mat;
        SAFE_FUNC_EVAL( _kernel.kernel(trgdat.extpos(), neidat.extpos(), neidat.extpos(), mat) );
        SAFE_FUNC_EVAL( zgemv(1.0, mat, neidat.extden(), 1.0, trgdat.extval()) );
    }
    return 0;
}

int Wave3d::V_list_compute(BoxDat& trgdat, double W, int _P, Point3& trgctr, DblNumMat& uep,
                           DblNumMat& dcp, CpxNumVec& dnchkval, NumTns<CpxNumTns>& ue2dc) {
#ifndef RELEASE
    CallStackEntry entry("Wave3d::V_list_compute");
#endif
    double step = W / (_P - 1);
    setvalue(_valfft,cpx(0, 0));
    //LEXING: SPECIAL
    for (std::vector<BoxKey>::iterator vi = trgdat.vndeidxvec().begin();
         vi != trgdat.vndeidxvec().end(); ++vi) {
        BoxKey neikey = (*vi);
        BoxDat& neidat = _boxvec.access(neikey);
        CHECK_TRUE(HasPoints(neidat));
        //mul
        Point3 neictr = BoxCenter(neikey);
        Index3 idx;
        for (int d = 0; d < dim(); ++d) {
            idx(d) = int(round( (trgctr[d]-neictr[d]) / W )); //LEXING:CHECK
        }
        //create if it is missing
        if (neidat.fftcnt() == 0) {
            setvalue(_denfft, cpx(0,0));
            CpxNumVec& neiden = neidat.upeqnden();
            for (int k = 0; k < uep.n(); ++k) {
                int a = int( round((uep(0, k) + W / 2) / step) ) + _P;
                int b = int( round((uep(1, k) + W / 2) / step) ) + _P;
                int c = int( round((uep(2, k) + W / 2) / step) ) + _P;
                _denfft(a,b,c) = neiden(k);
            }
            fftw_execute(_fplan);
            neidat.upeqnden_fft() = _denfft; //COPY to the right place
        }
        CpxNumTns& neidenfft = neidat.upeqnden_fft();
        //TODO: LEXING GET THE INTERACTION TENSOR
        CpxNumTns& inttns = ue2dc(idx[0]+3,idx[1]+3,idx[2]+3);
        for (int a = 0; a < 2 * _P; ++a) {
            for (int b = 0; b < 2 * _P; ++b) {
                for (int c = 0; c < 2 * _P; ++c) {
                    _valfft(a, b, c) += (neidenfft(a, b, c) * inttns(a, b, c));
                }
            }
        }
        //clean if necessary
        neidat.fftcnt()++;
        if (neidat.fftcnt() == neidat.fftnum()) {
            neidat.upeqnden_fft().resize(0,0,0);
            neidat.fftcnt() = 0;//reset, LEXING
        }
    }
    fftw_execute(_bplan);
    //add back
    double coef = 1.0 / (2 * _P * 2 * _P * 2 * _P);
    for (int k = 0; k < dcp.n(); ++k) {
        int a = int( round((dcp(0, k) + W / 2) / step) ) + _P;
        int b = int( round((dcp(1, k) + W / 2) / step) ) + _P;
        int c = int( round((dcp(2, k) + W / 2) / step) ) + _P;
        dnchkval(k) += (_valfft(a, b, c) * coef); //LEXING: VERY IMPORTANT
    }
    return 0;
}

int Wave3d::X_list_compute(BoxDat& trgdat, DblNumMat& dcp, DblNumMat& dnchkpos,
                           CpxNumVec& dnchkval) {
#ifndef RELEASE
    CallStackEntry entry("Wave3d::X_list_compute");
#endif
    for (std::vector<BoxKey>::iterator vi = trgdat.xndeidxvec().begin();
        vi != trgdat.xndeidxvec().end(); ++vi) {
        BoxKey neikey = (*vi);
        BoxDat& neidat = _boxvec.access(neikey);
        CHECK_TRUE(HasPoints(neidat));
        Point3 neictr = BoxCenter(neikey);
        if(IsTerminal(trgdat) && trgdat.extpos().n() < dcp.n()) {
            CpxNumMat mat;
            SAFE_FUNC_EVAL( _kernel.kernel(trgdat.extpos(), neidat.extpos(), neidat.extpos(), mat) );
            SAFE_FUNC_EVAL( zgemv(1.0, mat, neidat.extden(), 1.0, trgdat.extval()) );
        } else {
            //mul
            CpxNumMat mat;
            SAFE_FUNC_EVAL( _kernel.kernel(dnchkpos, neidat.extpos(), neidat.extpos(), mat) );
            SAFE_FUNC_EVAL( zgemv(1.0, mat, neidat.extden(), 1.0, dnchkval) );
        }
    }
    return 0;
}

int Wave3d::W_list_compute(BoxDat& trgdat, double W, DblNumMat& uep) {
#ifndef RELEASE
    CallStackEntry entry("Wave3d::W_list_compute");
#endif
    for (std::vector<BoxKey>::iterator vi = trgdat.wndeidxvec().begin();
        vi != trgdat.wndeidxvec().end(); ++vi) {
        BoxKey neikey = (*vi);
        BoxDat& neidat = _boxvec.access(neikey);
        CHECK_TRUE(HasPoints(neidat));
        Point3 neictr = BoxCenter(neikey);
        //upchkpos
        if (IsTerminal(neidat) && neidat.extpos().n() < uep.n()) {
            CpxNumMat mat;
            SAFE_FUNC_EVAL( _kernel.kernel(trgdat.extpos(), neidat.extpos(), neidat.extpos(), mat) );
            SAFE_FUNC_EVAL( zgemv(1.0, mat, neidat.extden(), 1.0, trgdat.extval()) );
        } else {
            double coef = BoxWidth(neikey) / W; //LEXING: SUPER IMPORTANT
            DblNumMat upeqnpos(uep.m(), uep.n()); //local version
            for (int k = 0; k < uep.n(); ++k) {
                for (int d = 0; d < dim(); ++d) {
                    upeqnpos(d, k) = coef * uep(d, k) + neictr(d);
                }
            }
            //mul
            CpxNumMat mat;
            SAFE_FUNC_EVAL( _kernel.kernel(trgdat.extpos(), upeqnpos, upeqnpos, mat) );
            SAFE_FUNC_EVAL( zgemv(1.0, mat, neidat.upeqnden(), 1.0, trgdat.extval()) );
        }
    }
    return 0;
}
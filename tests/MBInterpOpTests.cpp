#include <gtest/gtest.h>
#include "Proto.H"
#include "Lambdas.H"
#include "MBLevelMap_Shear.H"
#include "MBLevelMap_CubeSphereShell.H"
#include "MBLevelMap_CubeSphereShellPolar.H"

using namespace Proto;
#if DIM == 2
TEST(MBInterpOp, ShearTest)
{
    int domainSize = 4;
    int boxSize = 4;
    Array<double, DIM> exp{4,4,0,0,0,0};
    Array<double, DIM> offset{0,0,0.3,0,0,0};
    HDF5Handler h5;
    Array<Point, DIM+1> ghost;
    ghost.fill(Point::Ones(4));
    ghost[0] = Point::Ones(1);
    std::vector<Point> footprint;
    for (auto pi : Box::Kernel(3))
    {
        if (pi.abs().sum() <= 2)
        {
            footprint.push_back(pi);
        }
    }
    int N = 3;
    double err[N];
    for (int nn = 0; nn < N; nn++)
    {
        err[nn] = 0.0;
        auto domain = buildShear(domainSize);
        Point boxSizeVect = Point::Ones(boxSize);
        MBDisjointBoxLayout layout(domain, boxSizeVect);

        ghost[0] = Point::Ones(2);
        MBLevelMap_Shear<HOST> map;
        map.define(layout, ghost);

        ghost[0] = Point::Ones(1);
        MBLevelBoxData<double, 1, HOST> hostSrc(layout, ghost);
        MBLevelBoxData<double, 1, HOST> hostDst(layout, ghost);
        MBLevelBoxData<double, 1, HOST> hostErr(layout, ghost);

        for (auto iter : layout)
        {
            auto& src_i = hostSrc[iter];
            auto& x_i = map.map()[iter];
            auto block = layout.block(iter);
            BoxData<double, 1> x_pow = forall_p<double, 1>(f_polyM, block, x_i, exp, offset);
            src_i |= Stencil<double>::CornersToCells(4)(x_pow);
        }

        //hostSrc.initialize(f_polyM, map.map(), dx, exp, offset);
        hostSrc.exchange();
        hostSrc.fillBoundaries();
        hostDst.setVal(0);
        hostErr.setVal(0);
        

        auto blockDomainBox = Box::Cube(domainSize);
        for (auto iter : layout)
        {
            auto block = layout.block(iter);
            Box patchBox = layout[iter];
            for (auto dir : Box::Kernel(1))
            {
                auto bounds = hostSrc.bounds(iter, dir);
                for (auto bound : bounds)
                {
                    Box boundBox = patchBox.adjacent(ghost[0]*dir);
                    if (blockDomainBox.contains(boundBox)) { continue; }
                    for (auto bi : boundBox)
                    {
                        MBDataPoint dstDataPoint(iter, bi, layout);
                        MBPointInterpOp op(dstDataPoint, ghost[0], map, footprint, 4);
                        op.apply(hostDst, hostSrc);
#if PR_VERBOSE > 1
                        pout() << "Coefs at point " << bi << std::endl;
                        auto coefs = op.coefs(hostSrc);
                        coefs.print("%10.2e");
#endif
                        double interpValue = hostDst[dstDataPoint](0);
                        double exactValue =  hostSrc[dstDataPoint](0);
                        double errorValue = interpValue - exactValue;
                        err[nn] = max(abs(errorValue), err[nn]);
                        hostErr[dstDataPoint](0) = std::abs(errorValue);
                    }
                }
            }
        }
#if PR_VERBOSE > 0
        if (procID() == 0)
        {
            std::cout << "Error (Max Norm): " << err[nn] << std::endl;
        }
        h5.writeMBLevel({"err"}, map, hostErr, "MBInterpOpTests_Shear_Err_%i", nn);
        h5.writeMBLevel({"phi"}, map, hostSrc, "MBInterpOpTests_Shear_Src_%i", nn);
        h5.writeMBLevel({"phi"}, map, hostDst, "MBInterpOpTests_Shear_Dst_%i", nn);
#endif
        domainSize *= 2;
        boxSize *= 2;
    }

    for (int ii = 1; ii < N; ii++)
    {
        double rate = log(err[ii-1]/err[ii])/log(2.0);
#if PR_VERBOSE > 0
        std::cout << "Convergence Rate: " << rate << std::endl;
#endif
    }
}
#endif
#if DIM == 3
#if 0
TEST(MBInterpOp, CubeSphereShellTest)
{
    HDF5Handler h5;
    int domainSize = 16;
    int boxSize = 8;
    int thickness = 1;
    bool cullRadialGhost = true;
    bool use2DFootprint = true;
    int radialDir = CUBE_SPHERE_SHELL_RADIAL_COORD;
    Array<double, DIM> exp{4,4,4,0,0,0};
    Array<double, DIM> offset{0.1,0.2,0.3,0,0,0};
    Array<Point, DIM+1> ghost;
    ghost.fill(Point::Ones(4));
    ghost[0] = Point::Ones(1);
    if (cullRadialGhost) { ghost[0][radialDir] = 0;}
    std::vector<Point> footprint;
    for (auto pi : Box::Kernel(3))
    {
        if (pi.abs().sum() <= 2)
        {
            if (use2DFootprint && (pi[radialDir] != 0)) { continue; }
            footprint.push_back(pi);
        }
    }
    int N = 2;
    double err[N];
    double errL1[N];
    for (int nn = 0; nn < N; nn++)
    {
        err[nn] = 0.0;
        auto domain = buildCubeSphereShell(domainSize, thickness, radialDir);
        Point boxSizeVect = Point::Ones(boxSize);
        boxSizeVect[radialDir] = thickness;
        MBDisjointBoxLayout layout(domain, boxSizeVect);

        ghost[0] = Point::Ones(3);
        if (cullRadialGhost) { ghost[0][radialDir] = 0; }
        MBLevelMap_CubeSphereShell<HOST> map;
        map.define(layout, ghost);
        MBLevelMap_CubeSphereShellPolar<HOST> polarMaps[6];
        for (int bi = 0; bi < 6; bi++)
        {
            polarMaps[bi].define(layout, ghost, bi);
        }
        for (auto iter : layout)
        {
            for (auto dir : Box::Kernel(1))
            {
                std::cout << "dir: " << dir << " | bounds: " << polarMaps[0].map().bounds(iter, dir).size() << std::endl;
            }
        }

        ghost[0] = Point::Ones(1);
        if (cullRadialGhost) { ghost[0][radialDir] = 0; }

        MBLevelBoxData<double, 1, HOST> hostSrc(layout, ghost);
        MBLevelBoxData<double, 1, HOST> hostDst(layout, ghost);
        MBLevelBoxData<double, 1, HOST> hostErr(layout, ghost);

        auto C2C = Stencil<double>::CornersToCells(4);
        for (auto iter : layout)
        {
            auto block = layout.block(iter);
            auto& src_i = hostSrc[iter];
            Box b_i = C2C.domain(layout[iter]).grow(ghost[0]);
            BoxData<double, DIM> x_i(b_i.grow(Point::Ones()));
            BoxData<double, 1> J_i(layout[iter].grow(Point::Ones() + ghost[0]));
            FluxBoxData<double, DIM> NT(layout[iter]);
            map.apply(x_i, J_i, NT, block);
            BoxData<double, 1> x_pow = forall_p<double, 1>(f_polyM, block, x_i, exp, offset);
            BoxData<double, 1> x_pow_avg = C2C(x_pow);
            J_i.setVal(1.0);
            Operator::cellProduct(src_i, J_i, x_pow_avg);
        }

        hostSrc.exchange();
        hostSrc.fillBoundaries();
        hostDst.setVal(0);
        hostErr.setVal(0);
        
        auto blockDomainBox = Box::Cube(domainSize);
        for (auto iter : layout)
        {
            auto block = layout.block(iter);
            Box patchBox = layout[iter];
            for (auto dir : Box::Kernel(1))
            {
                auto bounds = hostSrc.bounds(iter, dir);
                auto mapBounds = polarMaps[block].map().bounds(iter, dir);
                EXPECT_EQ(bounds.size(), mapBounds.size());
                for (auto bound : bounds)
                {
                    Box boundBox = patchBox.adjacent(ghost[0]*dir);
                    if (blockDomainBox.contains(boundBox)) { continue; }
                    for (auto bi : boundBox)
                    {
                        MBDataPoint dstDataPoint(iter, bi, layout);
                        MBPointInterpOp op(
                                dstDataPoint, ghost[0], polarMaps[block], footprint, 4);
                        op.apply(hostDst, hostSrc);
                        double interpValue = hostDst[dstDataPoint](0);
                        double exactValue =  hostSrc[dstDataPoint](0);
                        double errorValue = abs(interpValue - exactValue);
                        err[nn] = max(errorValue, err[nn]);
                        errL1[nn] += errorValue;
                        hostErr[dstDataPoint](0) = errorValue;
                    }
                }
            }
        }
        Reduction<double, Max> rxn;
        rxn.reduce(&err[nn], 1);
        err[nn] = rxn.fetch();
        rxn.reset();
        rxn.reduce(&errL1[nn], 1);
        errL1[nn] = rxn.fetch() / domainSize;
#if PR_VERBOSE > 0
        if (procID() == 0)
        {
            std::cout << "Error (Max Norm): " << err[nn];
            std::cout << " | Error (L1 Norm): " << errL1[nn] << std::endl;
        }
        h5.writeMBLevel({"err"}, map, hostErr, "MBInterpOpTests_CubeSphereShell_Err_%i", nn);
        h5.writeMBLevel({"phi"}, map, hostSrc, "MBInterpOpTests_CubeSphereShell_Src_%i", nn);
        h5.writeMBLevel({"phi"}, map, hostDst, "MBInterpOpTests_CubeSphereShell_Dst_%i", nn);
#endif
        domainSize *= 2;
        boxSize *= 2;
    }

    for (int ii = 1; ii < N; ii++)
    {
        double rate = log(err[ii-1]/err[ii])/log(2.0);
        double rateL1 = log(errL1[ii-1]/errL1[ii])/log(2.0);
        EXPECT_GT(rate, 3.5);
        EXPECT_GT(rateL1, 3.5);
#if PR_VERBOSE > 0
        if (procID() == 0)
        {
            std::cout << "Convergence Rate (Max Norm): " << rate;
            std::cout << " | (L1 Norm): " << rateL1 << std::endl;
        }
#endif
    }
}
#endif
#if 1
TEST(MBInterpOp, CubeSphereShellTest_Full)
{
    HDF5Handler h5;
    int domainSize = 16;
    int boxSize = 16;
    int thickness = 1;
    bool cullRadialGhost = true;
    bool use2DFootprint = true;
    int radialDir = CUBE_SPHERE_SHELL_RADIAL_COORD;
    Array<double, DIM> exp{4,4,4,0,0,0};
    Array<double, DIM> offset{0.1,0.2,0.3,0,0,0};
    Array<Point, DIM+1> ghost;
    ghost.fill(Point::Ones(4));
    ghost[0] = Point::Ones(1);
    if (cullRadialGhost) { ghost[0][radialDir] = 0;}
    std::vector<Point> footprint;
    for (auto pi : Box::Kernel(3))
    {
        if (pi.abs().sum() <= 2)
        {
            if (use2DFootprint && (pi[radialDir] != 0)) { continue; }
            footprint.push_back(pi);
        }
    }
    int N = 3;
    double err[N];
    double errL1[N];
    for (int nn = 0; nn < N; nn++)
    {
        err[nn] = 0.0;
        auto domain = buildCubeSphereShell(domainSize, thickness, radialDir);
        Point boxSizeVect = Point::Ones(boxSize);
        boxSizeVect[radialDir] = thickness;
        MBDisjointBoxLayout layout(domain, boxSizeVect);

        ghost[0] = Point::Ones(3);
        if (cullRadialGhost) { ghost[0][radialDir] = 0; }
        MBLevelMap_CubeSphereShell<HOST> map;
        map.define(layout, ghost);
        MBLevelMap_CubeSphereShellPolar<HOST> polarMaps[6];
        for (int bi = 0; bi < 6; bi++)
        {
            polarMaps[bi].define(layout, ghost, bi);
        }

        ghost[0] = Point::Ones(1);
        if (cullRadialGhost) { ghost[0][radialDir] = 0; }

        MBLevelBoxData<double, 1, HOST> hostSrc(layout, ghost);
        MBLevelBoxData<double, 1, HOST> hostDst(layout, ghost);
        MBLevelBoxData<double, 1, HOST> hostErr(layout, ghost);
        auto C2C = Stencil<double>::CornersToCells(4);
        for (auto iter : layout)
        {
            auto block = layout.block(iter);
            auto& src_i = hostSrc[iter];
            Box b_i = C2C.domain(layout[iter]).grow(ghost[0]);
            BoxData<double, DIM> x_i(b_i.grow(Point::Ones()));
            BoxData<double, 1> J_i(layout[iter].grow(Point::Ones() + ghost[0]));
            FluxBoxData<double, DIM> NT(layout[iter]);
            map.apply(x_i, J_i, NT, block);
            BoxData<double, 1> x_pow = forall_p<double, 1>(f_polyM, block, x_i, exp, offset);
            BoxData<double, 1> x_pow_avg = C2C(x_pow);
            J_i.setVal(1.0);
            Operator::cellProduct(src_i, J_i, x_pow_avg);
        }

        hostSrc.exchange();
        hostSrc.fillBoundaries();
        hostDst.setVal(0);
        hostErr.setVal(0);
    
        MBInterpOp op(ghost[0], 4);
        for (int bi = 0; bi < layout.numBlocks(); bi++)
        {
            op.define(polarMaps[bi], footprint, bi);
        }
        op.apply(hostDst, hostSrc);

        
        for (auto iter : layout)
        {
            auto& err_i = hostErr[iter];
            auto& dst_i = hostDst[iter];
            auto& src_i = hostSrc[iter];

            BoxData<double, 1> tmp(layout[iter]);
            src_i.copyTo(tmp);
            dst_i.copyTo(err_i);
            err_i -= src_i;
            err_i += tmp;
            err[nn] = max(err_i.absMax(), err[nn]);
            errL1[nn] += err_i.sum();
        }
        Reduction<double, Max> rxn;
        rxn.reduce(&err[nn], 1);
        err[nn] = rxn.fetch();
        rxn.reset();
        rxn.reduce(&errL1[nn], 1);
        errL1[nn] = rxn.fetch() / domainSize;

#if PR_VERBOSE > 0
        if (procID() == 0)
        {
            std::cout << "Error (Max Norm): " << err[nn];
            std::cout << " | Error (L1 Norm): " << errL1[nn] << std::endl;
        }
        h5.writeMBLevel({"err"}, map, hostErr, "MBInterpOpTests_CubeSphereShell_Err_%i", nn);
        h5.writeMBLevel({"phi"}, map, hostSrc, "MBInterpOpTests_CubeSphereShell_Src_%i", nn);
        h5.writeMBLevel({"phi"}, map, hostDst, "MBInterpOpTests_CubeSphereShell_Dst_%i", nn);
#endif
        domainSize *= 2;
        boxSize *= 2;
    }
    for (int ii = 1; ii < N; ii++)
    {
        double rate = log(err[ii-1]/err[ii])/log(2.0);
        double rateL1 = log(errL1[ii-1]/errL1[ii])/log(2.0);
        EXPECT_GT(rate, 3.5);
        EXPECT_GT(rateL1, 3.5);
#if PR_VERBOSE > 0
        if (procID() == 0)
        {
            std::cout << "Convergence Rate (Max Norm): " << rate;
            std::cout << " | (L1 Norm): " << rateL1 << std::endl;
        }
#endif
    }
}
#endif
#endif
int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
#ifdef PR_MPI
    MPI_Init(&argc, &argv);
#endif
    PR_TIMER_SETFILE("MBInterpOpTests_DIM" + to_string(DIM) + "_NProc" + to_string(numProc())
            + ".time.table");
    int result = RUN_ALL_TESTS();
    PR_TIMER_REPORT();
#ifdef PR_MPI
    MPI_Finalize();
#endif
    return result;
}

#include "Proto.H"
#include "InputParser.H"
#include "BoxOp_Laplace.H"
#include "AMRSolver_FASMultigrid.H"

using namespace Proto;

PROTO_KERNEL_START void f_force_0(const Point& a_pt, Var<double> a_data, double a_dx)
{
    double x[DIM];
    
    a_data(0) = 1.0;
    for (int dir = 0; dir < DIM; dir++)
    {
        x[dir] = a_pt[dir]*a_dx + a_dx/2.0;
        a_data(0) = a_data(0)*sin(M_PI*2*(x[dir] + .125));
    }
}
PROTO_KERNEL_END(f_force_0, f_force);

PROTO_KERNEL_START void f_force_avg_0(const Point& a_pt, Var<double> a_data, double a_dx)
{
    double x0[DIM];
    double x1[DIM];
    
    double a = 0.125;
    for (int dir = 0; dir < DIM; dir++)
    {
        x0[dir] = a_pt[dir]*a_dx + a;
        x1[dir] = x0[dir] + a_dx;
    }
    
    double k = M_PI*2;
    a_data(0) = + cos(k*x1[0])*cos(k*x1[1])
                - cos(k*x0[0])*cos(k*x1[1])
                - cos(k*x1[0])*cos(k*x0[1])
                + cos(k*x0[0])*cos(k*x0[1]);
    a_data(0) *= 1.0/(k*k*a_dx*a_dx);
}
PROTO_KERNEL_END(f_force_avg_0, f_force_avg);

PROTO_KERNEL_START void f_soln_0(const Point& a_pt, Var<double> a_data, double a_dx)
{
    double x[DIM];
    
    a_data(0) = -1/(DIM*pow(2.0*M_PI, 2));
    for (int dir = 0; dir < DIM; dir++)
    {
        x[dir] = a_pt[dir]*a_dx + a_dx/2.0;
        a_data(0) = a_data(0)*sin(M_PI*2*(x[dir] + .125));
    }
}
PROTO_KERNEL_END(f_soln_0, f_soln);

PROTO_KERNEL_START void f_soln_avg_0(const Point& a_pt, Var<double> a_data, double a_dx)
{
    double x0[DIM];
    double x1[DIM];
    
    double a = 0.125;
    for (int dir = 0; dir < DIM; dir++)
    {
        x0[dir] = a_pt[dir]*a_dx + a;
        x1[dir] = x0[dir] + a_dx;
    }
    
    double k = M_PI*2;
    a_data(0) = + cos(k*x1[0])*cos(k*x1[1])
                - cos(k*x0[0])*cos(k*x1[1])
                - cos(k*x1[0])*cos(k*x0[1])
                + cos(k*x0[0])*cos(k*x0[1]);
    a_data(0) *= 1.0/(k*k*a_dx*a_dx);
    a_data(0) *= -1.0/(DIM*pow(k, 2.0));
}
PROTO_KERNEL_END(f_soln_avg_0, f_soln_avg);

int AMR_LEVEL = 0;
int MG_LEVEL = 0;
int AMR_SOLVE_ITER = 0;
int MG_SOLVE_ITER = 0;
int RELAX_ITER = 0;
int AMR_RELAX_STAGE = 0;

int main(int argc, char** argv)
{
    #ifdef PR_MPI
    MPI_Init(&argc, &argv);
    #endif

    // SETUP
    HDF5Handler h5;
    InputArgs args;
    args.parse();
    args.print();

    int domainSize = 64;
    int boxSize = 16;
    int numIter = 3;
    int numLevels = 2;
    int ghostSize = 1;
    int solveIter = 10;
    double tolerance = 1e-10;
    double k = 1;
    double physDomainSize = 1;
    int refRatio = 4;
    std::array<bool, DIM> periodicity;
    for (int dir = 0; dir < DIM; dir++) { periodicity[dir] = true; }

    args.set("domainSize", &domainSize);
    args.set("boxSize",    &boxSize);
    args.set("numIter",    &numIter);
    args.set("numLevels",  &numLevels);
    args.set("solveIter",  &solveIter);
    args.set("tolerance",  &tolerance);
    args.set("refRatio",   &refRatio);
    args.set("periodic_x", &periodicity[0]);
    args.set("periodic_y", &periodicity[1]);

    typedef BoxOp_Laplace<double> OP;
    double err[numIter];
    for (int nn = 0; nn < numIter; nn++)
    {
        // GEOMETRY
        double dx = physDomainSize / domainSize;
        
        std::vector<Point> refRatios;
        refRatios.resize(numLevels-1, Point::Ones(refRatio));
        
        std::vector<DisjointBoxLayout> layouts;
        layouts.resize(numLevels);
                
        Point boxSizeV = Point::Ones(boxSize);
        Box domainBox = Box::Cube(domainSize);
        ProblemDomain domain(domainBox, periodicity);
        layouts[0].define(domain, boxSizeV);

        Box refinedRegion = domainBox;
        for (int lvl = 1; lvl < numLevels; lvl++)
        {
            Point prevSize = refinedRegion.high() - refinedRegion.low() + Point::Ones();
            refinedRegion = refinedRegion.grow(-prevSize / 4).refine(refRatios[lvl-1]);
            Box refinedRegionPatches = refinedRegion.coarsen(boxSizeV);
            std::vector<Point> fineLayoutPatches;
            for (auto iter = refinedRegionPatches.begin(); iter.ok(); ++iter)
            {
                fineLayoutPatches.push_back(*iter);
            }
            ProblemDomain fineDomain = layouts[lvl-1].domain().refine(refRatios[lvl-1]);
            layouts[lvl].define(fineDomain, fineLayoutPatches, boxSizeV);
        }
        AMRGrid grid(layouts, refRatios, numLevels);
        
        // SOLVER
        AMRSolver_FASMultigrid<BoxOp_Laplace, double> solver(grid, dx);
        AMROp<BoxOp_Laplace, double> op(grid, dx);

        // DATA
        AMRData<double> Phi(grid,    OP::ghost()); 
        AMRData<double> G(grid,      Point::Zeros());
        AMRData<double> PhiSln(grid, OP::ghost()); 
        AMRData<double> PhiErr(grid, Point::Zeros()); 
        AMRData<double> Res(grid,    Point::Zeros());

        Phi.setToZero();
        //Phi.initConvolve(dx, f_soln);
        G.initialize(dx, f_force_avg);
        PhiSln.initialize(dx, f_soln_avg);
        //op(G, PhiSln);
        h5.writeAMRData(dx, PhiSln, "SLN_N%i", nn);
        h5.writeAMRData(dx, PhiSln, "RHS_N%i", nn);
       
        // SOLVE
        std::cout << "Integral of RHS: " << G.integrate(dx) << std::endl;
        solver.solve(Phi, G, solveIter, tolerance);
        Phi.averageDown();

        // COMPUTE ERROR
        for (int lvl = 0; lvl < numLevels; lvl++)
        {
            for (auto iter = grid[lvl].begin(); iter.ok(); ++iter)
            {
                auto& phi_i = Phi[lvl][*iter];
                auto& sln_i = PhiSln[lvl][*iter];
                auto& err_i = PhiErr[lvl][*iter];

                phi_i.copyTo(err_i);
                err_i -= sln_i;
            }
        }
        h5.writeAMRData(dx, PhiErr, "ERR_N%i", nn);
        PhiErr.averageDown();
        err[nn] = PhiErr.integrateAbs(dx);

        pout() << "Error: " << err[nn] << std::endl;
        domainSize *= 2;
    }
        
    for (int ii = 1; ii < numIter; ii++)
    {
        pout() << "Convergence Rate: " << log(err[ii-1] / err[ii]) / log(2.0) << std::endl;
    }
    #ifdef PR_MPI
    MPI_Finalize();
    #endif
    return 0;
}


#include "LevelData.H"
#include "Multigrid.H"
#include "AMRFAS.H"
#include "Proto.H"
#include "TestOp.H" //definition of OP

//using namespace Proto;
using namespace std;

int main(int argc, char** argv)
{
    int TEST;
    if (argc == 1)
    {
      cout << "Please choose a test to run:" << endl;
      cout << "\tTest 1: AMRFAS (Incomplete)" << endl;
      cout << "\tTest 2: Multigrid" << endl;
      return 0;
    } else if (argc == 2) 
    {
      TEST = atoi(argv[1]);
    }

    typedef Proto::BoxData<Real, NUMCOMPS> BD;
    typedef TestOp<FArrayBox> OP;
    typedef FArrayBox DATA;
    
    //====================================================================
    if (TEST == 1)
    {
      int domainSize = 32;
      Real dx = 2.0*M_PI/domainSize;
      
      Box domainBoxC = Proto::Box::Cube(domainSize);
      Box domainBoxF = Proto::Box::Cube(domainSize/2).shift(Proto::Point::Ones(domainSize/4));
      domainBoxF = domainBoxF.refine(AMR_REFRATIO);
      DisjointBoxLayout coarseLayout, fineLayout;
      buildLayout(coarseLayout, domainBoxC);
      buildLayout(fineLayout, domainBoxF, Proto::Point::Zeros());

      LevelData<DATA> LDF(fineLayout, 1, Proto::Point::Ones());
      LevelData<DATA> LDC(coarseLayout, 1, Proto::Point::Ones());

      auto fiter = fineLayout.dataIterator();
      auto citer = coarseLayout.dataIterator();

      cout << "Coarse Layout" << endl;
      for (citer.begin(); citer.ok(); ++citer)
      {
        cout << Proto::Box(coarseLayout[citer]) << endl;
      }

      cout << "Fine Layout" << endl;
      for (fiter.begin(); fiter.ok(); ++fiter)
      {
        cout << Proto::Box(fineLayout[fiter]) << endl;
      }

      for (fiter.begin(); fiter.ok(); ++fiter)
      {
        Proto::BoxData<double> bd = LDF[fiter];
        bd.setVal(1337);
      }
      for (citer.begin(); citer.ok(); ++citer)
      {
        Proto::BoxData<double> bd = LDC[citer];
        bd.setVal(17);
      }
     
      TestOp<DATA> op(fineLayout,dx);
      op.interpBoundary(LDF, LDC);
    } // End AMRFAS test 
    //====================================================================
    else if (TEST == 2)
    {
      int domainSize = 64;
      int numLevels = 5;
      Real dx = 2.0*M_PI/domainSize;
      
      Box domainBox = Proto::Box::Cube(domainSize);
      DisjointBoxLayout layout;
      buildLayout(layout, domainBox);

      LevelData<DATA> U(layout, NUMCOMPS, IntVect::Unit);
      LevelData<DATA> F(layout, NUMCOMPS, IntVect::Zero);
      LevelData<DATA> R(layout, NUMCOMPS, IntVect::Zero);
      LevelData<DATA> S(layout, NUMCOMPS, IntVect::Zero);
      
      OP::initialCondition(U,dx);
      OP::forcing(F,dx);
      OP::solution(S,dx);
      
      Multigrid<OP, DATA> mg(layout, dx, numLevels-1);
      int numIter = 20;
      double resnorm = 0.0;
      char fileName[100];
      char fileNameU[100];
      TestOp<FArrayBox> op(layout,dx);
      fileNum = 0;
      for (int ii = 0; ii < numIter; ii++)
      {
          mg.vcycle(U,F); 
          resnorm = op.residual(R,U,F);
          std::cout << scientific << "iteration number = " << ii << ", Residual norm: " << resnorm << std::endl;
          //sprintf(fileName,"ResV.%i.hdf5",fileNum);
          //sprintf(fileNameU,"ResU.%i.hdf5",fileNum);
          //writeLevelname(&R,fileName);
          //writeLevelname(&U,fileNameU);
          //fileNum++;
      }
      auto iter = layout.dataIterator();
      double umax = 0.0;
      double umin = 0.0;
      double error = 0.0;
      for (iter.begin(); iter.ok(); ++iter)
      {
          Proto::BoxData<double> s = S[iter()];
          Proto::BoxData<double> u = U[iter()];
          umax = max(umax,u.max());
          umin = min(umin,u.min());
          s -= u;
          error = max(s.absMax(),error);
      }
      cout << "Error: " << error << endl;
      cout << "Max: " << umax << " Min: " << umin << endl;
    } // End Multigrid test
}

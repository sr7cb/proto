//#include <gtest/gtest.h>
#include "Proto.H"
#include "Lambdas.H"
//#include "MBLevelMap_Shear.H"
//#include "MBLevelMap_XPointRigid.H"
#include "Proto_CubedSphereShell.H"
#include "BoxOp_EulerCubedSphere.H"
 
void GetCmdLineArgumenti(int argc, const char** argv, const char* name, int* rtn)
{
    size_t len = strlen(name);
    for(int i=1; i<argc; i+=2)
    {
        if(strcmp(argv[i]+1,name) ==0)
        {
          *rtn = atoi(argv[i+1]);
            std::cout<<name<<"="<<" "<<*rtn<<std::endl;
            break;
        }
    }
}
#if 1
template<typename T, MemType MEM>
PROTO_KERNEL_START
void f_initPatchData_(
                      Point                           a_pt,
                      Var<T,NUMCOMPS,MEM>&            a_U,
                      Var<T>&                         a_radius,
                      Var<T,DIM,MEM,DIM>&             a_A,
                      T                               a_dx,
                      T                               a_dxi0,
                      T                               a_gamma,
                      int                             a_nradius)
{
  // Compute Cartesian conserved quantitities from spherical data.
  T p0 = a_gamma;
  T rho0 = 1.0;
  T eps = .01;
  T amplitude;
  T arg = (a_pt[0] - a_nradius/2)/(a_nradius*1.0);
  if (abs(arg) < .5)
    {
      amplitude = eps*pow(cos(M_PI*arg),6);
    }else
    {
      amplitude = 0.;
    }
  T rho = rho0*(1.0 + amplitude);
  T p = p0*pow(rho/rho0,a_gamma);
  Array<T,DIM> vSpherical = {1.,0.,0.};
  vSpherical[0] *= amplitude;
  Array<T,DIM> vcart;
  T ke;
  for (int dim1 = 0; dim1 < DIM; dim1++)
    {
      vcart[dim1] = 0.;
      // Can set velocities here.
      for (int dim2 = 0; dim2 < DIM; dim2++)
        {
          vcart[dim1] += vSpherical[dim2]*a_A(dim1,dim2);
        }
      a_U(dim1 + 1) = vcart[dim1]*rho;
      ke += .5*vcart[dim1]*vcart[dim1];
    }
  a_U(0) = rho;
  a_U(NUMCOMPS-1) = p/(a_gamma-1) + rho*ke;
}
PROTO_KERNEL_END(f_initPatchData_, f_initPatchData)
#endif
int main(int argc, char* argv[])
{   
  HDF5Handler h5;
  int domainSize = 32;
  int boxSize = 16;
  int thickness = 8;
  Array<double,DIM> offset = {0.,0.,0.};
  Array<double,DIM> exp = {1.,1.,1.};
  PR_TIMER_SETFILE(to_string(domainSize) 
                   + "_DIM" + to_string(DIM) //+ "_NProc" + to_string(numProc())
                   + "_CubeSphereTest.time.table");
  PR_TIMERS("MMBEuler");
  
  bool cullRadialGhost = true;
  bool use2DFootprint = true;
  int radialDir = CUBED_SPHERE_SHELL_RADIAL_COORD;
  Array<Point, DIM+1> ghost;
  ghost.fill(Point::Ones(4));
  ghost[0] = Point::Ones(1);
  ghost[0][radialDir] = 0;
   Array<Array<uint,DIM>,6> permute = {{2,1,0},{2,1,0},{1,0,2},{0,1,2},{1,0,2},{0,1,2}};
  Array<Array<int,DIM>,6> sign = {{-1,1,1},{1,1,-1},{-1,1,1},{1,1,1},{1,-1,1},{-1,-1,1}};     
  auto domain =
  CubedSphereShell::Domain(domainSize, thickness, radialDir);
  Point boxSizeVect = Point::Ones(boxSize);
  boxSizeVect[radialDir] = thickness;
  MBDisjointBoxLayout layout(domain, boxSizeVect);

  // initialize data and map

  MBLevelBoxData<double, NUMCOMPS, HOST> U(layout, ghost);
  MBLevelBoxData<double, NUMCOMPS, HOST> rhs(layout, Point::Zeros());

  auto map = CubedSphereShell::Map(layout,ghost);
  auto eulerOp = CubedSphereShell::Operator<BoxOp_EulerCubedSphere, double, HOST>(map);
  
  double dx = 1.0/domainSize;
    
  auto C2C = Stencil<double>::CornersToCells(4);
  for (auto dit : layout)
    {
      auto block = layout.block(dit);
      auto& U_i = U[dit];
      BoxData<double, DIM, HOST, DIM> A(U_i.box());
      BoxData<double,1,HOST> detA(U_i.box());
      Point lo = U_i.box().low();
      Point hi = U_i.box().high();
      hi[0] = lo[0];
      Box basebx(lo,hi);
      forallInPlace_p(f_geomA,basebx,A,detA,
                      permute[block],sign[block],dx,0);
      
      Operator::spreadSlice(A);
      Operator::spreadSlice(detA);
      BoxData<double> radius(layout[dit].grow(ghost[0]));
      BoxData<double> rdot(layout[dit].grow(ghost[0]));
      radiusRDot(radius,rdot,thickness);
      int r_dir = CUBED_SPHERE_SHELL_RADIAL_COORD;
      double r0 = CUBED_SPHERE_SHELL_R0;
      double r1 = CUBED_SPHERE_SHELL_R1;
      double dr = (r1-r0)/thickness;
      double gamma = 5.0/3.0;
      forallInPlace_p(f_initPatchData,U_i,radius,A,dx,dr,gamma,thickness);
      Box b_i = C2C.domain(layout[dit]).grow(ghost[0]);
      BoxData<double, DIM> x_i(b_i.grow(Point::Ones()));

      BoxData<double, 1> J_i(layout[dit].grow(Point::Ones() + ghost[0]));
      FluxBoxData<double, DIM> NT(layout[dit]);
      map.apply(x_i, J_i, NT,block);     
    }
  cout << "Setup done" << endl; 
  U.exchange(); // fill boundary data
  CubedSphereShell::InterpBoundaries(U);
  
  for (auto dit :U.layout())
    {
      auto& rhs_i = rhs[dit];
      auto& U_i = U[dit];
      BoxData<double> radius(layout[dit].grow(ghost[0]));
      BoxData<double> rdot(layout[dit].grow(ghost[0]));
      radiusRDot(radius,rdot,thickness);
      int block_i = layout.block(dit);
      Array<BoxData<double,NUMCOMPS>, DIM> fluxes;     
      double dx = .5*M_PI/domainSize;      
      eulerOp[dit](rhs_i,fluxes,U_i,radius,rdot,block_i,dx,1.0);
    } 
  h5.writeMBLevel({"rhs"}, map, rhs, "MBEulerCubedSphereRhs");
  PR_TIMER_REPORT();
}

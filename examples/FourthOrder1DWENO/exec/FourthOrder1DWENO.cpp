#include <cstdio>
#include <cstring>
#include <cassert>
#include <cmath>

#include <vector>
#include <memory>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "Proto.H"
#include "Proto_WriteBoxData.H"
#include "Proto_Timer.H"
#include "AdvectionRK4.H"

using namespace std;
using namespace Proto;

//PROTO_KERNEL_START
//void evaluatePhi_p_temp(Point& a_p,
//                        Var<double>& phi,
//                        const double& time,
//                        const double& vel,
//                        const double& dx)
//{
//  //phi(0)=1.0;
//  double xp=(a_p[0]+0.5)*dx;
//  double xm=(a_p[0]-0.5)*dx;
//  phi(0)=(cos(2*M_PI*(xm-vel*time))-cos(2*M_PI*(xp-vel*time)))/(2*M_PI*dx);
//  //double R=std::abs(x-vel*time-0.5);
//  //double R0=0.15;
//  //double pi_div_2=1.57079632679;
//  //if(R<=R0)
//  //  phi(0)=pow(cos(pi_div_2*(R/R0)),8);
//  //else
//  //  phi(0)=0.0;
//}
//PROTO_KERNEL_END(evaluatePhi_p_temp,evaluatePhi_p)
PROTO_KERNEL_START
void evaluatePhiCent_p_temp(Point& a_p,
                            Var<double>& phi,
                            const double& time,
                            const double& vel,
                            const double& dx)
{
  //phi(0)=1.0;
  double x=a_p[0]*dx;
  //phi(0)=sin(2*M_PI*(x-vel*time));
  double R=std::abs(x-vel*time-0.5);
  double R0=0.15;
  double pi_div_2=1.57079632679;
  if(R<=R0)
    phi(0)=pow(cos(pi_div_2*(R/R0)),8);
  else
    phi(0)=0.0;
}
PROTO_KERNEL_END(evaluatePhiCent_p_temp,evaluatePhiCent_p)

void ComputePhiFaceAverages(BoxData<double>& phi, const double vel, const double dx, const double time)
{
  Stencil<double> Lap2nd=Stencil<double>::Laplacian();
  BoxData<double> phi_cent(phi.box().grow(1));
  forallInPlace_p(evaluatePhiCent_p,phi_cent,time,vel,dx);
  phi.setVal(0.0);
  phi|=Lap2nd(phi_cent,phi.box(),1.0/24);
  phi+=phi_cent;
}

int main(int argc, char* argv[])
{
  double vel=1.0;
  double init_time=0.0;
  double init_Ncells=32;
  //double tStop=0.125;
  double tStop=1.0;
  int maxStep=10000;
  //int maxStep=1;
  double L=1.0;
  for(int ilev=0; ilev<3; ilev++)
    {
      //Change to powers of 2^{ilev-1}
      int Ncells=init_Ncells*std::pow(2,ilev);
      double dt=0.5*std::abs(vel)/Ncells;
      AdvectionState state(L,Ncells,vel);
      AdvectionOp ad_op;

      double time=init_time;
      //forallInPlace_p(evaluatePhi_p,state.m_phi,time,state.m_vel,state.m_dx);
      ComputePhiFaceAverages(state.m_phi,state.m_vel, state.m_dx, time);
      
      //std::cout << "Max initial phi: " << state.m_phi.absMax() << std::endl;
      RK4<AdvectionState,AdvectionOp,AdvectionDX> rk4_timestepper;
      for(int k=0; k<maxStep && time<tStop; k++)
        {
          //rk4_timestepper.advance(time,dt,state);
          ad_op.advance(time,dt,state);
          time+=dt;
          //std::cout << "Time,max phi: " << time << ", " << state.m_phi.absMax() << std::endl;
        }
      //std::string comp_file="compute_soln.curve";
      //WriteBoxData(state.m_phi);
      BoxData<double> exact_solution(state.m_phi.box());
      ComputePhiFaceAverages(exact_solution, state.m_vel, state.m_dx, time);
      //exact_solution.setVal(0.0);
      //forallInPlace_p(evaluatePhi_p,exact_solution,time,state.m_vel,state.m_dx);
      //BoxData<double> exactPhiCell(state.m_phi.box());
      //exactPhiCell.setVal(0.0);
      //forallInPlace_p(evaluatePhiCent_p,exactPhiCell,time,state.m_vel,state.m_dx);
      BoxData<double> error(state.m_phi.box());
      state.m_phi.copyTo(error);
      error-=exact_solution;
      std::cout << "================================" << std::endl;
      std::cout << "Ncells: " << Ncells << std::endl;
      std::cout << "End time: " << time << std::endl;
      std::cout << "Abs max computed solution: " << state.m_phi.absMax() << std::endl;
      std::cout << "Abs max exact solution: " << exact_solution.absMax() << std::endl;
      std::cout << "Max error: " << error.absMax() << std::endl;
      error.print();
    }


  return 0;
}

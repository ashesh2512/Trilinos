// @HEADER
// ****************************************************************************
//                Tempus: Copyright (2017) Sandia Corporation
//
// Distributed under BSD 3-clause license (See accompanying file Copyright.txt)
// ****************************************************************************
// @HEADER

#include "Teuchos_UnitTestHarness.hpp"
#include "Teuchos_XMLParameterListHelpers.hpp"
#include "Teuchos_TimeMonitor.hpp"
#include "Teuchos_DefaultComm.hpp"

#include "Tempus_config.hpp"
#include "Tempus_IntegratorBasic.hpp"
#include "Tempus_StepperTrapezoidal.hpp"

#include "../TestModels/SinCosModel.hpp"
#include "../TestModels/VanDerPolModel.hpp"
#include "../TestUtils/Tempus_ConvergenceTestUtils.hpp"

#include "Stratimikos_DefaultLinearSolverBuilder.hpp"
#include "Thyra_LinearOpWithSolveFactoryHelpers.hpp"

#ifdef Tempus_ENABLE_MPI
#include "Epetra_MpiComm.h"
#else
#include "Epetra_SerialComm.h"
#endif

#include <vector>
#include <fstream>
#include <sstream>
#include <limits>

namespace Tempus_Test {

using Teuchos::RCP;
using Teuchos::ParameterList;
using Teuchos::sublist;
using Teuchos::getParametersFromXmlFile;

using Tempus::IntegratorBasic;
using Tempus::SolutionHistory;
using Tempus::SolutionState;


// ************************************************************
// ************************************************************
TEUCHOS_UNIT_TEST(Trapezoidal, ParameterList)
{
  // Read params from .xml file
  RCP<ParameterList> pList =
    getParametersFromXmlFile("Tempus_Trapezoidal_SinCos.xml");

  // Setup the SinCosModel
  RCP<ParameterList> scm_pl = sublist(pList, "SinCosModel", true);
  RCP<SinCosModel<double> > model =
    Teuchos::rcp(new SinCosModel<double> (scm_pl));

  RCP<ParameterList> tempusPL  = sublist(pList, "Tempus", true);

  // Test constructor IntegratorBasic(tempusPL, model)
  {
    RCP<Tempus::IntegratorBasic<double> > integrator =
      Tempus::integratorBasic<double>(tempusPL, model);

    RCP<ParameterList> stepperPL = sublist(tempusPL, "Default Stepper", true);
    RCP<ParameterList> defaultPL =
      integrator->getStepper()->getDefaultParameters();
    TEST_ASSERT(haveSameValues(*stepperPL, *defaultPL, true))
  }

  // Test constructor IntegratorBasic(model, stepperType)
  {
    RCP<Tempus::IntegratorBasic<double> > integrator =
      Tempus::integratorBasic<double>(model, "Trapezoidal Method");

    RCP<ParameterList> stepperPL = sublist(tempusPL, "Default Stepper", true);
    RCP<ParameterList> defaultPL =
      integrator->getStepper()->getDefaultParameters();

    TEST_ASSERT(haveSameValues(*stepperPL, *defaultPL, true))
  }
}


// ************************************************************
// ************************************************************
TEUCHOS_UNIT_TEST(Trapezoidal, ConstructingFromDefaults)
{
  double dt = 0.1;

  // Read params from .xml file
  RCP<ParameterList> pList =
    getParametersFromXmlFile("Tempus_Trapezoidal_SinCos.xml");
  RCP<ParameterList> pl = sublist(pList, "Tempus", true);

  // Setup the SinCosModel
  RCP<ParameterList> scm_pl = sublist(pList, "SinCosModel", true);
  //RCP<SinCosModel<double> > model = sineCosineModel(scm_pl);
  RCP<SinCosModel<double> > model =
    Teuchos::rcp(new SinCosModel<double>(scm_pl));

  // Setup Stepper for field solve ----------------------------
  RCP<Tempus::StepperTrapezoidal<double> > stepper =
    Teuchos::rcp(new Tempus::StepperTrapezoidal<double>(model));
  //{
  //  // Setup a linear NOX solve
  //  RCP<ParameterList> sPL = stepper->getNonconstParameterList();
  //  std::string solverName = sPL->get<std::string>("Solver Name");
  //  RCP<ParameterList> solverPL = Teuchos::sublist(sPL, solverName, true);
  //  stepper->setSolver(solverPL);
  //  stepper->initialize();
  //}

  // Setup TimeStepControl ------------------------------------
  RCP<Tempus::TimeStepControl<double> > timeStepControl =
    Teuchos::rcp(new Tempus::TimeStepControl<double>());
  ParameterList tscPL = pl->sublist("Default Integrator")
                           .sublist("Time Step Control");
  timeStepControl->setStepType (tscPL.get<std::string>("Integrator Step Type"));
  timeStepControl->setInitIndex(tscPL.get<int>   ("Initial Time Index"));
  timeStepControl->setInitTime (tscPL.get<double>("Initial Time"));
  timeStepControl->setFinalTime(tscPL.get<double>("Final Time"));
  timeStepControl->setInitTimeStep(dt);

  // Setup initial condition SolutionState --------------------
  Thyra::ModelEvaluatorBase::InArgs<double> inArgsIC =
    stepper->getModel()->getNominalValues();
  RCP<Thyra::VectorBase<double> > icSoln =
    Teuchos::rcp_const_cast<Thyra::VectorBase<double> > (inArgsIC.get_x());
  RCP<Thyra::VectorBase<double> > icSolnDot =
    Teuchos::rcp_const_cast<Thyra::VectorBase<double> > (inArgsIC.get_x_dot());
  RCP<Tempus::SolutionState<double> > icState =
      Teuchos::rcp(new Tempus::SolutionState<double>(icSoln,icSolnDot));
  icState->setTime    (timeStepControl->getInitTime());
  icState->setIndex   (timeStepControl->getInitIndex());
  icState->setTimeStep(0.0);
  icState->setOrder   (stepper->getOrder());
  icState->setSolutionStatus(Tempus::Status::PASSED);  // ICs are passing.

  // Setup SolutionHistory ------------------------------------
  RCP<Tempus::SolutionHistory<double> > solutionHistory =
    Teuchos::rcp(new Tempus::SolutionHistory<double>());
  solutionHistory->setName("Forward States");
  solutionHistory->setStorageType(Tempus::STORAGE_TYPE_STATIC);
  solutionHistory->setStorageLimit(2);
  solutionHistory->addState(icState);

  // Setup Integrator -----------------------------------------
  RCP<Tempus::IntegratorBasic<double> > integrator =
    Tempus::integratorBasic<double>();
  integrator->setStepperWStepper(stepper);
  integrator->setTimeStepControl(timeStepControl);
  integrator->setSolutionHistory(solutionHistory);
  //integrator->setObserver(...);
  integrator->initialize();


  // Integrate to timeMax
  bool integratorStatus = integrator->advanceTime();
  TEST_ASSERT(integratorStatus)


  // Test if at 'Final Time'
  double time = integrator->getTime();
  double timeFinal =pl->sublist("Default Integrator")
     .sublist("Time Step Control").get<double>("Final Time");
  TEST_FLOATING_EQUALITY(time, timeFinal, 1.0e-14);

  // Time-integrated solution and the exact solution
  RCP<Thyra::VectorBase<double> > x = integrator->getX();
  RCP<const Thyra::VectorBase<double> > x_exact =
    model->getExactSolution(time).get_x();

  // Calculate the error
  RCP<Thyra::VectorBase<double> > xdiff = x->clone_v();
  Thyra::V_StVpStV(xdiff.ptr(), 1.0, *x_exact, -1.0, *(x));

  // Check the order and intercept
  std::cout << "  Stepper = Trapezoidal" << std::endl;
  std::cout << "  =========================" << std::endl;
  std::cout << "  Exact solution   : " << get_ele(*(x_exact), 0) << "   "
                                       << get_ele(*(x_exact), 1) << std::endl;
  std::cout << "  Computed solution: " << get_ele(*(x      ), 0) << "   "
                                       << get_ele(*(x      ), 1) << std::endl;
  std::cout << "  Difference       : " << get_ele(*(xdiff  ), 0) << "   "
                                       << get_ele(*(xdiff  ), 1) << std::endl;
  std::cout << "  =========================" << std::endl;
  TEST_FLOATING_EQUALITY(get_ele(*(x), 0), 0.841021, 1.0e-4 );
  TEST_FLOATING_EQUALITY(get_ele(*(x), 1), 0.541002, 1.0e-4 );
}


// ************************************************************
// ************************************************************
TEUCHOS_UNIT_TEST(Trapezoidal, SinCos)
{
  std::vector<double> StepSize;
  std::vector<double> ErrorNorm;
  const int nTimeStepSizes = 7;
  double dt = 0.2;
  double order = 0.0;
  for (int n=0; n<nTimeStepSizes; n++) {

    // Read params from .xml file
    RCP<ParameterList> pList =
      getParametersFromXmlFile("Tempus_Trapezoidal_SinCos.xml");

    //std::ofstream ftmp("PL.txt");
    //pList->print(ftmp);
    //ftmp.close();

    // Setup the SinCosModel
    RCP<ParameterList> scm_pl = sublist(pList, "SinCosModel", true);
    //RCP<SinCosModel<double> > model = sineCosineModel(scm_pl);
    RCP<SinCosModel<double> > model =
      Teuchos::rcp(new SinCosModel<double>(scm_pl));

    dt /= 2;

    // Setup the Integrator and reset initial time step
    RCP<ParameterList> pl = sublist(pList, "Tempus", true);
    pl->sublist("Default Integrator")
       .sublist("Time Step Control").set("Initial Time Step", dt);
    RCP<Tempus::IntegratorBasic<double> > integrator =
      Tempus::integratorBasic<double>(pl, model);
    order = integrator->getStepper()->getOrder();

    // Initial Conditions
    // During the Integrator construction, the initial SolutionState
    // is set by default to model->getNominalVales().get_x().  However,
    // the application can set it also by integrator->setInitialState.
    {
      RCP<Thyra::VectorBase<double> > x0 =
        model->getNominalValues().get_x()->clone_v();
      RCP<Thyra::VectorBase<double> > xdot0 =
        model->getNominalValues().get_x_dot()->clone_v();
      integrator->setInitialState(0.0, x0, xdot0);
    }

    // Integrate to timeMax
    bool integratorStatus = integrator->advanceTime();
    TEST_ASSERT(integratorStatus)

    // Test if at 'Final Time'
    double time = integrator->getTime();
    double timeFinal =pl->sublist("Default Integrator")
       .sublist("Time Step Control").get<double>("Final Time");
    TEST_FLOATING_EQUALITY(time, timeFinal, 1.0e-14);

    // Time-integrated solution and the exact solution
    RCP<Thyra::VectorBase<double> > x = integrator->getX();
    RCP<const Thyra::VectorBase<double> > x_exact =
      model->getExactSolution(time).get_x();

    // Plot sample solution and exact solution
    if (n == 0) {
      std::ofstream ftmp("Tempus_Trapezoidal_SinCos.dat");
      RCP<const SolutionHistory<double> > solutionHistory =
        integrator->getSolutionHistory();
      RCP<const Thyra::VectorBase<double> > x_exact_plot;
      for (int i=0; i<solutionHistory->getNumStates(); i++) {
        RCP<const SolutionState<double> > solutionState = (*solutionHistory)[i];
        double time = solutionState->getTime();
        RCP<const Thyra::VectorBase<double> > x_plot = solutionState->getX();
        x_exact_plot = model->getExactSolution(time).get_x();
        ftmp << time << "   "
             << get_ele(*(x_plot), 0) << "   "
             << get_ele(*(x_plot), 1) << "   "
             << get_ele(*(x_exact_plot), 0) << "   "
             << get_ele(*(x_exact_plot), 1) << std::endl;
      }
      ftmp.close();
    }

    // Calculate the error
    RCP<Thyra::VectorBase<double> > xdiff = x->clone_v();
    Thyra::V_StVpStV(xdiff.ptr(), 1.0, *x_exact, -1.0, *(x));
    StepSize.push_back(dt);
    const double L2norm = Thyra::norm_2(*xdiff);
    ErrorNorm.push_back(L2norm);
  }

  // Check the order and intercept
  double slope = computeLinearRegressionLogLog<double>(StepSize, ErrorNorm);
  std::cout << "  Stepper = Trapezoidal" << std::endl;
  std::cout << "  =========================" << std::endl;
  std::cout << "  Expected order: " << order << std::endl;
  std::cout << "  Observed order: " << slope << std::endl;
  std::cout << "  =========================" << std::endl;
  TEST_FLOATING_EQUALITY( slope, order, 0.01 );
  TEST_FLOATING_EQUALITY( ErrorNorm[0], 0.000832086, 1.0e-4 );

  std::ofstream ftmp("Tempus_Trapezoidal_SinCos-Error.dat");
  double error0 = 0.8*ErrorNorm[0];
  for (int n=0; n<nTimeStepSizes; n++) {
    ftmp << StepSize[n]  << "   " << ErrorNorm[n] << "   "
         << error0*(pow(StepSize[n]/StepSize[0],order)) << std::endl;
  }
  ftmp.close();
}


// ************************************************************
// ************************************************************
TEUCHOS_UNIT_TEST(Trapezoidal, VanDerPol)
{
  std::vector<RCP<Thyra::VectorBase<double>>> solutions;
  std::vector<double> StepSize;
  std::vector<double> ErrorNorm;
  const int nTimeStepSizes = 4;
  double dt = 0.05;
  double order = 0.0;
  for (int n=0; n<nTimeStepSizes; n++) {

    // Read params from .xml file
    RCP<ParameterList> pList =
      getParametersFromXmlFile("Tempus_Trapezoidal_VanDerPol.xml");

    // Setup the VanDerPolModel
    RCP<ParameterList> vdpm_pl = sublist(pList, "VanDerPolModel", true);
    RCP<VanDerPolModel<double> > model =
      Teuchos::rcp(new VanDerPolModel<double>(vdpm_pl));

    // Set the step size
    dt /= 2;
    if (n == nTimeStepSizes-1) dt /= 10.0;

    // Setup the Integrator and reset initial time step
    RCP<ParameterList> pl = sublist(pList, "Tempus", true);
    pl->sublist("Demo Integrator")
       .sublist("Time Step Control").set("Initial Time Step", dt);
    RCP<Tempus::IntegratorBasic<double> > integrator =
      Tempus::integratorBasic<double>(pl, model);
    order = integrator->getStepper()->getOrder();

    // Integrate to timeMax
    bool integratorStatus = integrator->advanceTime();
    TEST_ASSERT(integratorStatus)

    // Test if at 'Final Time'
    double time = integrator->getTime();
    double timeFinal =pl->sublist("Demo Integrator")
      .sublist("Time Step Control").get<double>("Final Time");
    double tol = 100.0 * std::numeric_limits<double>::epsilon();
    TEST_FLOATING_EQUALITY(time, timeFinal, tol);

    // Store off the final solution and step size
    auto solution = Thyra::createMember(model->get_x_space());
    Thyra::copy(*(integrator->getX()),solution.ptr());
    solutions.push_back(solution);
    StepSize.push_back(dt);

    // Output finest temporal solution for plotting
    // This only works for ONE MPI process
    if ((n == 0) or (n == nTimeStepSizes-1)) {
      std::string fname = "Tempus_Trapezoidal_VanDerPol-Ref.dat";
      if (n == 0) fname = "Tempus_Trapezoidal_VanDerPol.dat";
      std::ofstream ftmp(fname);
      RCP<const SolutionHistory<double> > solutionHistory =
        integrator->getSolutionHistory();
      int nStates = solutionHistory->getNumStates();
      for (int i=0; i<nStates; i++) {
        RCP<const SolutionState<double> > solutionState = (*solutionHistory)[i];
        RCP<const Thyra::VectorBase<double> > x = solutionState->getX();
        double ttime = solutionState->getTime();
        ftmp << ttime << "   " << get_ele(*x, 0) << "   " << get_ele(*x, 1)
             << std::endl;
      }
      ftmp.close();
    }
  }

  // Calculate the error - use the most temporally refined mesh for
  // the reference solution.
  auto ref_solution = solutions[solutions.size()-1];
  std::vector<double> StepSizeCheck;
  for (std::size_t i=0; i < (solutions.size()-1); ++i) {
    auto tmp = solutions[i];
    Thyra::Vp_StV(tmp.ptr(), -1.0, *ref_solution);
    const double L2norm = Thyra::norm_2(*tmp);
    StepSizeCheck.push_back(StepSize[i]);
    ErrorNorm.push_back(L2norm);
  }

  // Check the order and intercept
  double slope = computeLinearRegressionLogLog<double>(StepSizeCheck,ErrorNorm);
  std::cout << "  Stepper = Trapezoidal" << std::endl;
  std::cout << "  =========================" << std::endl;
  std::cout << "  Expected order: " << order << std::endl;
  std::cout << "  Observed order: " << slope << std::endl;
  std::cout << "  =========================" << std::endl;
  TEST_FLOATING_EQUALITY( slope, order, 0.10 );
  out << "\n\n ** Slope on Trapezoidal Method = " << slope
      << "\n" << std::endl;

  // Write error data
  {
    std::ofstream ftmp("Tempus_Trapezoidal_VanDerPol-Error.dat");
    double error0 = 0.8*ErrorNorm[0];
    for (std::size_t n = 0; n < StepSizeCheck.size(); n++) {
      ftmp << StepSizeCheck[n]  << "   " << ErrorNorm[n] << "   "
           << error0*(pow(StepSize[n]/StepSize[0],order)) << std::endl;
    }
    ftmp.close();
  }

  Teuchos::TimeMonitor::summarize();
}


} // namespace Tempus_Test

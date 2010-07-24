#define H2D_REPORT_INFO
#define H2D_REPORT_FILE "application.log"
#include "hermes2d.h"

//  This example shows how to save visualization data if you are working 
//  on a distant computer and cannot use ScalarView, OrderView, and 
//  related classes. This is demonstrated in the context of the 
//  time-dependent tutorial example 09-timedep. 

int OUTPUT_FREQUENCY = 20;                        // Number of time steps between saving data.

const int P_INIT = 4;                             // Polynomial degree of all mesh elements.
const int INIT_REF_NUM = 1;                       // Number of initial uniform mesh refinements.
const int INIT_REF_NUM_BDY = 1;                   // Number of initial uniform mesh refinements towards the boundary.
const double TAU = 300.0;                         // Time step in seconds.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_UMFPACK, SOLVER_PETSC,
                                                  // SOLVER_MUMPS, and more are coming.

// Problem parameters.
const double T_INIT = 10;        // Temperature of the ground (also initial temperature).
const double ALPHA = 10;         // Heat flux coefficient for Newton's boundary condition.
const double LAMBDA = 1e5;       // Thermal conductivity of the material.
const double HEATCAP = 1e6;      // Heat capacity.
const double RHO = 3000;         // Material density.
const double FINAL_TIME = 18000; // Length of time interval (24 hours) in seconds.

// Global time variable.
double TIME = 0;

// Time-dependent exterior temperature.
double temp_ext(double t) {
  return T_INIT + 10. * sin(2*M_PI*t/FINAL_TIME);
}

// Boundary markers.
int bdy_ground = 1;
int bdy_air = 2;

// Boundary condition types.
BCType bc_types(int marker)
{
  if (marker == bdy_ground) return BC_ESSENTIAL;
  else return BC_NATURAL;
}

// Essential (Dirichlet) boundary condition values.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
  return T_INIT;
}

// Weak forms.
#include "forms.cpp"

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh mesh;
  H2DReader mloader;
  mloader.load("cathedral.mesh", &mesh);

  // Perform initial mesh refinements.
  for(int i = 0; i < INIT_REF_NUM; i++) mesh.refine_all_elements();
  mesh.refine_towards_boundary(bdy_air, INIT_REF_NUM_BDY);

  // Initialize an H1 space with default shepeset.
  H1Space space(&mesh, bc_types, essential_bc_values, P_INIT);
  int ndof = get_num_dofs(&space);
  info("ndof = %d.", ndof);

  // Set initial condition.
  Solution tsln;
  tsln.set_const(&mesh, T_INIT);

  // Initialize weak formulation.
  WeakForm wf;
  wf.add_matrix_form(bilinear_form<double, double>, bilinear_form<Ord, Ord>);
  wf.add_matrix_form_surf(bilinear_form_surf<double, double>, bilinear_form_surf<Ord, Ord>, bdy_air);
  wf.add_vector_form(linear_form<double, double>, linear_form<Ord, Ord>, H2D_ANY, &tsln);
  wf.add_vector_form_surf(linear_form_surf<double, double>, linear_form_surf<Ord, Ord>, bdy_air);

  // Initialize the linear problem.
  LinearProblem lp(&wf, &space);

  // Initialize matrix solver.
  Matrix* mat; Vector* rhs; CommonSolver* solver;  
  init_matrix_solver(matrix_solver, ndof, mat, rhs, solver);

  // Time stepping:
  int nsteps = (int)(FINAL_TIME/TAU + 0.5);
  bool rhsonly = false;
  for(int ts = 1; ts <= nsteps; ts++)
  {
    info("---- Time step %d, time %3.5f, ext_temp %g", ts, TIME, temp_ext(TIME));

    // Assemble stiffness matrix and rhs.
    lp.assemble(mat, rhs, rhsonly);
    rhsonly = true;

    // Solve the matrix problem.
    if (!solver->solve(mat, rhs)) error ("Matrix solver failed.\n");

    // Update tsln.
    tsln.set_fe_solution(&space, rhs);

    if (ts % OUTPUT_FREQUENCY == 0) {
      Linearizer lin;
      int item = H2D_FN_VAL_0;
      double eps = H2D_EPS_NORMAL;
      double max_abs = -1.0;
      MeshFunction* xdisp = NULL; 
      MeshFunction* ydisp = NULL;
      double dmult = 1.0;
      lin.process_solution(&tsln, item, eps, max_abs, xdisp, ydisp, dmult);
      char* filename = new char[100];
      sprintf(filename, "tsln_%d.lin", ts);
      lin.save_data(filename);
      info("Linearizer data saved to file %s.", filename);
    }

    // Update the time variable.
    TIME += TAU;
  }

  info("Let's assume that the remote computation has finished and you fetched the *.lin files.");

  info("Visualizing data from file tsln_60.lin.");

  // Use ScalarView to read and show the data.
  WinGeom* sln_win_geom = new WinGeom{0, 0, 450, 600};
  ScalarView Tview("Temperature", sln_win_geom);
  Tview.lin.load_data("tsln_60.lin");
  Tview.set_min_max_range(0,20);
  Tview.fix_scale_width(3);
  Tview.show_linearizer_data();

  info("Press 's' to save screenshot as bitmap file.");

  // Wait for the view to be closed.
  View::wait();
  return 0;
}

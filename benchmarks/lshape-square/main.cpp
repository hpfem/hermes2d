#include "hermes2d.h"
#include "solver_umfpack.h"

//  This is the L-Shape domain problem whose exact solution is extended by zero into the 
//  re-entrant corner quarter. Right-hand side is zero everywhere. This example cannot 
//  be solved with any known adaptivity algorithm - be the first who solves it!
//
//  PDE: -Laplace u = 0
//
//  Known exact solution, see functions fn() and fndd()
//
//  Domain: Square (-1, 1)^2, see the file domain.mesh
//
//  BC:  Dirichlet, given by exact solution
//
//  The following parameters can be changed:

const int P_INIT = 1;             // Initial polynomial degree of all mesh elements.
const int INIT_REF_NUM = 0;       // Number of initial uniform refinements
const double THRESHOLD = 0.3;     // This is a quantitative parameter of the adapt(...) function and
                                  // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 0;           // Adaptive strategy:
                                  // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                  //   error is processed. If more elements have similar errors, refine
                                  //   all to keep the mesh symmetric.
                                  // STRATEGY = 1 ... refine all elements whose error is larger
                                  //   than THRESHOLD times maximum element error.
                                  // STRATEGY = 2 ... refine all elements whose error is larger
                                  //   than THRESHOLD.
                                  // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const int ADAPT_TYPE = 0;         // Type of automatic adaptivity:
                                  // ADAPT_TYPE = 0 ... adaptive hp-FEM (default),
                                  // ADAPT_TYPE = 1 ... adaptive h-FEM,
                                  // ADAPT_TYPE = 2 ... adaptive p-FEM.
const bool ISO_ONLY = false;      // Isotropic refinement flag (concerns quadrilateral elements only).
                                  // ISO_ONLY = false ... anisotropic refinement of quad elements
                                  // is allowed (default),
                                  // ISO_ONLY = true ... only isotropic refinements of quad elements
                                  // are allowed.
const int MESH_REGULARITY = -1;   // Maximum allowed level of hanging nodes:
                                  // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                  // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                  // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                  // Note that regular meshes are not supported, this is due to
                                  // their notoriously bad performance.
const double ERR_STOP = 1e-8;     // Stopping criterion for adaptivity (rel. error tolerance between the
                                  // fine mesh and coarse mesh solution in percent).
const int NDOF_STOP = 40000;      // Adaptivity process stops when the number of degrees of freedom grows
                                  // over this limit. This is to prevent h-adaptivity to go on forever.

// exact solution
static double fn(double x, double y)
{
  if (x < 0 && y < 0) return 0;
  double r = sqrt(x*x + y*y);
  double a = atan2(x, y);
  return pow(r, 2.0/3.0) * sin(2.0*a/3.0 + M_PI/3);
}

static double fndd(double x, double y, double& dx, double& dy)
{
  if (x < 0 && y < 0) {
    dx = 0;
    dy = 0;
    return 0;
  }
  double t1 = 2.0/3.0*atan2(x, y) + M_PI/3;
  double t2 = pow(x*x + y*y, 1.0/3.0);
  double t3 = x*x * ((y*y)/(x*x) + 1);
  dx = 2.0/3.0*x*sin(t1)/(t2*t2) + 2.0/3.0*y*t2*cos(t1)/t3;
  dy = 2.0/3.0*y*sin(t1)/(t2*t2) - 2.0/3.0*x*t2*cos(t1)/t3;
  return fn(x, y);
}

// boundary condition types
int bc_types(int marker)
{
  return BC_ESSENTIAL;
}

// Dirichlet boundary condition values
scalar bc_values(int marker, double x, double y)
{
  return fn(x, y);
}

// bilinear form corresponding to the Laplace equation
template<typename Real, typename Scalar>
Scalar bilinear_form(int n, double *wt, Func<Real> *u, Func<Real> *v, Geom<Real> *e, ExtData<Scalar> *ext)
{
  return int_grad_u_grad_v<Real, Scalar>(n, wt, u, v);
}

int main(int argc, char* argv[])
{
  // load the mesh
  Mesh mesh;
  H2DReader mloader;
  mloader.load("domain.mesh", &mesh);

  // uniform initial refinements
  for (int i=0; i<INIT_REF_NUM; i++) mesh.refine_all_elements();

  // refine towards vertex #4 (center)
  //mesh.refine_towards_vertex(4, 10);

  // initialize the shapeset and the cache
  H1ShapesetOrtho shapeset;
  PrecalcShapeset pss(&shapeset);

  // create finite element space
  H1Space space(&mesh, &shapeset);
  space.set_bc_types(bc_types);
  space.set_bc_values(bc_values);
  space.set_uniform_order(P_INIT);

  // enumerate basis functions
  space.assign_dofs();

  // initialize the weak formulation
  WeakForm wf(1);
  wf.add_biform(0, 0, callback(bilinear_form), SYM);

  // visualize solution and mesh
  ScalarView sview("Coarse solution", 0, 0, 500, 400);
  OrderView  oview("Polynomial orders", 505, 0, 500, 400);

  // matrix solver
  UmfpackSolver solver;

  // DOF and CPU convergence graphs
  SimpleGraph graph_dof_est, graph_dof_exact, graph_cpu_est, graph_cpu_exact;

  // adaptivity loop
  int it = 1, ndofs;
  bool done = false;
  double cpu = 0.0;
  Solution sln_coarse, sln_fine;
  do
  {
    info("\n---- Adaptivity step %d ---------------------------------------------\n", it++);

    // time measurement
    begin_time();

    // solve the coarse mesh problem
    LinSystem ls(&wf, &solver);
    ls.set_spaces(1, &space);
    ls.set_pss(1, &pss);
    ls.assemble();
    ls.solve(1, &sln_coarse);

    // time measurement
    cpu += end_time();

    // calculate error wrt. exact solution
    ExactSolution exact(&mesh, fndd);
    double error = h1_error(&sln_coarse, &exact) * 100;
    info("\nExact solution error: %g%%", error);

    // view the solution
    sview.show(&sln_coarse);
    oview.show(&space);

    // time measurement
    begin_time();

    // solve the fine mesh problem
    RefSystem rs(&ls);
    rs.assemble();
    rs.solve(1, &sln_fine);

    // calculate error estimate wrt. fine mesh solution
    H1OrthoHP hp(1, &space);
    double err_est = hp.calc_error(&sln_coarse, &sln_fine) * 100;
    info("Estimate of error: %g%%", err_est);

    // add entries to DOF convergence graphs
    graph_dof_exact.add_values(space.get_num_dofs(), error);
    graph_dof_exact.save("conv_dof_exact.dat");
    graph_dof_est.add_values(space.get_num_dofs(), err_est);
    graph_dof_est.save("conv_dof_est.dat");

    // add entries to CPU convergence graphs
    graph_cpu_exact.add_values(cpu, error);
    graph_cpu_exact.save("conv_cpu_exact.dat");
    graph_cpu_est.add_values(cpu, err_est);
    graph_cpu_est.save("conv_cpu_est.dat");

    // if err_est too large, adapt the mesh
    if (err_est < ERR_STOP) done = true;
    else {
      hp.adapt(THRESHOLD, STRATEGY, ADAPT_TYPE, ISO_ONLY, MESH_REGULARITY);
      ndofs = space.assign_dofs();
      if (ndofs >= NDOF_STOP) done = true;
    }

    // time measurement
    cpu += end_time();
  }
  while (done == false);
  verbose("Total running time: %g sec", cpu);

  // show the fine solution - this is the final result
  sview.set_title("Final solution");
  sview.show(&sln_fine);

  // wait for keyboard or mouse input
  View::wait("Waiting for all views to be closed.");
  return 0;
}
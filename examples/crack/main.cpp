#include "hermes2d.h"
#include "solver_umfpack.h"

// This example employs the multimesh adaptive hp-FEM for a simple
// problem of crack mechanics. Each displacement components is
// approximated using an individual mesh.
//
// PDE: Lame equations of linear elasticity
//
// BC: u_1 = u_2 = 0 on Gamma_1 (left edge)
//     du_2/dn = f on Gamma_2 (upper edge)
//     du_1/dn = du_2/dn = 0 elsewhere, including two horizontal
//               cracks inside the domain. The width of the cracks
//               is currently zero, it can be set in the mesh file
//               via the parameter 'w'.
//
// The following parameters can be changed:

const int P_INIT = 1;                // Initial polynomial degree of all mesh elements.
const bool MULTI = true;             // true = use multi-mesh, false = use single-mesh.
                                     // Note: in the single mesh option, the meshes are
                                     // forced to be geometrically the same but the
                                     // polynomial degrees can still vary.
const bool SAME_ORDERS = true;       // true = when single mesh is used it forces same pol.
                                     // orders for components
                                     // when multi mesh used, parameter is ignored
const double THRESHOLD_MULTI = 0.35; // error threshold for element refinement (multi-mesh)
const double THRESHOLD_SINGLE = 0.7; // error threshold for element refinement (single-mesh)
const int STRATEGY = 0;              // Adaptive strategy:
                                     // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                     //   error is processed. If more elements have similar errors, refine
                                     //   all to keep the mesh symmetric.
                                     // STRATEGY = 1 ... refine all elements whose error is larger
                                     //   than THRESHOLD times maximum element error.
                                     // STRATEGY = 2 ... refine all elements whose error is larger
                                     //   than THRESHOLD.
                                     // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const int ADAPT_TYPE = 0;            // Type of automatic adaptivity:
                                     // ADAPT_TYPE = 0 ... adaptive hp-FEM (default),
                                     // ADAPT_TYPE = 1 ... adaptive h-FEM,
                                     // ADAPT_TYPE = 2 ... adaptive p-FEM.
const bool ISO_ONLY = false;         // Isotropic refinement flag (concerns quadrilateral elements only).
                                     // ISO_ONLY = false ... anisotropic refinement of quad elements
                                     // is allowed (default),
                                     // ISO_ONLY = true ... only isotropic refinements of quad elements
                                     // are allowed.
const int MESH_REGULARITY = -1;      // Maximum allowed level of hanging nodes:
                                     // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                     // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                     // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                     // Note that regular meshes are not supported, this is due to
                                     // their notoriously bad performance.
const int MAX_ORDER = 10;            // Maximum polynomial order used during adaptivity.
const double ERR_STOP = 1e-1;        // Stopping criterion for adaptivity (rel. error tolerance between the
                                     // fine mesh and coarse mesh solution in percent).
const int NDOF_STOP = 40000;         // Adaptivity process stops when the number of degrees of freedom grows

// problem constants
const double E  = 200e9;  // Young modulus for steel: 200 GPa
const double nu = 0.3;    // Poisson ratio
const double f  = 1e3;    // load force: 10^5 N
const double lambda = (E * nu) / ((1 + nu) * (1 - 2*nu));
const double mu = E / (2*(1 + nu));

// boundary markers
const int marker_left = 1;
const int marker_top = 2;

// boundary conditions
int bc_types_xy(int marker)
  { return (marker == marker_left) ? BC_ESSENTIAL : BC_NATURAL; }

// linear and bilinear forms
scalar bilinear_form_0_0(RealFunction* fu, RealFunction* fv, RefMap* ru, RefMap* rv)
  { return int_a_dudx_dvdx_b_dudy_dvdy(lambda+2*mu, fu, mu, fv, ru, rv); }

scalar bilinear_form_0_1(RealFunction* fu, RealFunction* fv, RefMap* ru, RefMap* rv)
  { return int_a_dudx_dvdy_b_dudy_dvdx(lambda, fv, mu, fu, rv, ru); }

scalar bilinear_form_1_0(RealFunction* fu, RealFunction* fv, RefMap* ru, RefMap* rv)
  { return int_a_dudx_dvdy_b_dudy_dvdx(lambda, fu, mu, fv, ru, rv); }

scalar bilinear_form_1_1(RealFunction* fu, RealFunction* fv, RefMap* ru, RefMap* rv)
  { return int_a_dudx_dvdx_b_dudy_dvdy(mu, fu, lambda+2*mu, fv, ru, rv); }

scalar linear_form_1_surf_top(RealFunction* fv, RefMap* rv, EdgePos* ep)
  { return -f * surf_int_v(fv, rv, ep); }

int main(int argc, char* argv[])
{
  // load the mesh
  Mesh xmesh, ymesh;
  xmesh.load("crack-2.mesh");
  ymesh.copy(&xmesh);          // this defines the common master mesh for
                               // both displacement fields

  // initialize the shapeset and the cache
  H1Shapeset shapeset;
  PrecalcShapeset xpss(&shapeset);
  PrecalcShapeset ypss(&shapeset);

  // create the x displacement space
  H1Space xdisp(&xmesh, &shapeset);
  xdisp.set_bc_types(bc_types_xy);
  xdisp.set_uniform_order(P_INIT);

  // create the y displacement space
  H1Space ydisp(MULTI ? &ymesh : &xmesh, &shapeset);
  ydisp.set_bc_types(bc_types_xy);
  ydisp.set_uniform_order(P_INIT);

  // enumerate basis functions
  int ndofs = xdisp.assign_dofs();
  ndofs += ydisp.assign_dofs(ndofs);
  printf("xdof=%d, ydof=%d\n", xdisp.get_num_dofs(), ydisp.get_num_dofs());

  // initialize the weak formulation
  WeakForm wf(2);
  wf.add_biform(0, 0, bilinear_form_0_0, SYM);
  wf.add_biform(0, 1, bilinear_form_0_1, SYM);
  wf.add_biform(1, 1, bilinear_form_1_1, SYM);
  wf.add_liform_surf(1, linear_form_1_surf_top, marker_top);

  // visualize solution and mesh
  ScalarView sview("Von Mises stress [Pa]", 0, 355, 800, 300);
  OrderView  xoview("X polynomial orders", 0, 0, 800, 300);
  OrderView  yoview("Y polynomial orders", 810, 0, 800, 300);

  // matrix solver
  UmfpackSolver solver;

  // convergence graph wrt. the number of degrees of freedom
  GnuplotGraph graph;
  graph.set_captions("", "Degrees of Freedom", "Error Estimate [%]");
  graph.add_row(MULTI ? "multi-mesh" : "single-mesh", "k", "-", "O");
  graph.set_log_y();

  // convergence graph wrt. CPU time
  GnuplotGraph graph_cpu;
  graph_cpu.set_captions("", "CPU", "Error Estimate [%]");
  graph_cpu.set_log_y();
  graph_cpu.add_row(MULTI ? "multi-mesh" : "single-mesh", "k", "-", "o");

  // adaptivity loop
  int it = 1;
  bool done = false;
  double cpu = 0.0;
  Solution sln_x_coarse, sln_y_coarse, sln_x_fine, sln_y_fine;
  do
  {
    info("\n---- Adaptivity step %d ---------------------------------------------\n", it++);

    // time measurement
    begin_time();

    // solve the coarse mesh problem
    LinSystem ls(&wf, &solver);
    ls.set_spaces(2, &xdisp, &ydisp);
    ls.set_pss(2, &xpss, &ypss);
    ls.assemble();
    ls.solve(2, &sln_x_coarse, &sln_y_coarse);

    // time measurement
    cpu += end_time();

    // visualize the solution
    VonMisesFilter stress(&sln_x_coarse, &sln_y_coarse, mu, lambda);
    //sview.set_min_max_range(0, 3e4);
    sview.show(&stress, EPS_HIGH);
    xoview.show(&xdisp);
    yoview.show(&ydisp);

    // time measurement
    begin_time();

    // solve the fine (reference) problem
    RefSystem rs(&ls);
    rs.assemble();
    rs.solve(2, &sln_x_fine, &sln_y_fine);

    // calculate error estimate wrt. fine mesh solution
    H1OrthoHP hp(2, &xdisp, &ydisp);
    double err_est = hp.calc_energy_error_2(&sln_x_coarse, &sln_y_coarse, &sln_x_fine, &sln_y_fine,
                                          bilinear_form_0_0, bilinear_form_0_1,
                                          bilinear_form_1_0, bilinear_form_1_1) * 100;
    info("Error estimate: %g \%", err_est);

    // add entry to DOF and CPU convergence graphs
    graph.add_values(0, xdisp.get_num_dofs() + ydisp.get_num_dofs(), err_est);
    graph.save(MULTI ? "conv_dof_m.gp" : "conv_dof_s.gp");
    graph_cpu.add_values(0, cpu, err_est);
    graph_cpu.save(MULTI ? "conv_cpu_m.gp" : "conv_cpu_s.gp");

    // if err_est too large, adapt the mesh
    if (err_est < ERR_STOP || xdisp.get_num_dofs() + ydisp.get_num_dofs() >= NDOF_STOP) done = true;
    else {
      hp.adapt(MULTI ? THRESHOLD_MULTI : THRESHOLD_SINGLE, STRATEGY, ADAPT_TYPE, ISO_ONLY, MESH_REGULARITY, MAX_ORDER, SAME_ORDERS);
      ndofs = xdisp.assign_dofs();
      ndofs += ydisp.assign_dofs(ndofs);
      printf("xdof=%d, ydof=%d\n", xdisp.get_num_dofs(), ydisp.get_num_dofs());
      if (ndofs >= NDOF_STOP) done = true;
    }

    // time measurement
    cpu += end_time();
  }
  while (!done);
  verbose("Total running time: %g sec", cpu);

  // wait for keypress or mouse input
  printf("Waiting for keyboard or mouse input.\n");
  View::wait();
  return 0;
}
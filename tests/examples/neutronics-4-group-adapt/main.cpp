#include "hermes2d.h"

using namespace RefinementSelectors;

// TODO : header

/** \addtogroup t_bench_neutronics_4g Benchmarks/Neutronics 2-group
 *  \{
 *  \brief This test makes sure that the benchmark "neutronics-2-group-adapt" works correctly.
 *
 *  \section s_params Parameters
 *  - P_INIT=1 for both solution components
 *  - INIT_REF_NUM=1 for both solution components
 *  - THERSHOLD=0.3
 *  - STRATEGY=0
 *  - CAND_LIST=HP_ANISO_P
 *  - MESH_REGULARITY=-1
 *  - ERR_STOP=4
 *  - CONV_EXP=1.0
 *  - NDOF_STOP=40000
 *  - ERROR_WEIGHTS=default values
 *
 *  \section s_res Expected results
 *  - DOFs: 331, 2876   (for the two solution components)
 *  - Iterations: 19    (the last iteration at which ERR_STOP is fulfilled)
 *  - Error:  4.33036%  (H1 norm of error with respect to the exact solution)
 *  - Negatives: 0      (number of negative values)
 */

const int INIT_REF_NUM[4] = {1, 1, 1, 1};// Initial uniform mesh refinement for the individual solution components.
const int P_INIT[4] = {1, 1, 1, 1};      // Initial polynomial orders for the individual solution components.
const double THRESHOLD = 0.3;            // This is a quantitative parameter of the adapt(...) function and
                                         // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 1;                  // Adaptive strategy:
                                         // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                         //   error is processed. If more elements have similar errors, refine
                                         //   all to keep the mesh symmetric.
                                         // STRATEGY = 1 ... refine all elements whose error is larger
                                         //   than THRESHOLD times maximum element error.
                                         // STRATEGY = 2 ... refine all elements whose error is larger
                                         //   than THRESHOLD.
                                         // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO; // Predefined list of element refinement candidates. Possible values are
                                         // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
                                         // H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
                                         // See User Documentation for details.
const int MESH_REGULARITY = -1;          // Maximum allowed level of hanging nodes:
                                         // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                         // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                         // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                         // Note that regular meshes are not supported, this is due to
                                         // their notoriously bad performance.
const double CONV_EXP = 1.0;             // Default value is 1.0. This parameter influences the selection of
                                         // candidates in hp-adaptivity. See get_optimal_refinement() for details.
const double ERR_STOP = 0.05;            // Stopping criterion for adaptivity (rel. error tolerance between the
                                         // fine mesh and coarse mesh solution in percent).
const int NDOF_STOP = 60000;             // Adaptivity process stops when the number of degrees of freedom grows over
                                         // this limit. This is mainly to prevent h-adaptivity to go on forever.


// Macros for simpler definition of tuples used in projections.
#define callback_pairs(a)      std::make_pair(callback(a)), std::make_pair(callback(a)), std::make_pair(callback(a)), std::make_pair(callback(a))

// Element markers.
const int marker_reflector = 1;
const int marker_core = 2;

// Boundary markers.
const int bc_vacuum = 1;
const int bc_sym = 2;

// Boundary condition types.
BCType bc_types(int marker)
{
  return BC_NATURAL;
}

// Essential (Dirichlet) boundary condition values.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
  return 0;
}

// Reflector properties (0), core properties (1).
const double D[2][4] = {{0.0164, 0.0085, 0.00832, 0.00821},
                        {0.0235, 0.0121, 0.0119, 0.0116}};
const double Sa[2][4] = {{0.00139, 0.000218, 0.00197, 0.0106},
                         {0.00977, 0.162, 0.156, 0.535}};
const double Sr[2][4] = {{1.77139, 0.533218, 3.31197, 0.0106},
                         {1.23977, 0.529, 2.436, 0.535}};
const double Sf[2][4] = {{0.0, 0.0, 0.0, 0.0}, {0.00395, 0.0262, 0.0718, 0.346}};
const double nu[2][4] = {{0.0, 0.0, 0.0, 0.0}, {2.49, 2.43, 2.42, 2.42}};
const double chi[2][4] = {{0.0, 0.0, 0.0, 0.0}, {0.9675, 0.03250, 0.0, 0.0}};
const double Ss[2][4][4] = {{{ 0.0,   0.0,  0.0, 0.0},
                             {1.77,   0.0,  0.0, 0.0},
                             { 0.0, 0.533,  0.0, 0.0},
                             { 0.0,   0.0, 3.31, 0.0}},
                            {{ 0.0,   0.0,  0.0, 0.0},
                             {1.23,   0.0,  0.0, 0.0},
                             { 0.0, 0.367,  0.0, 0.0},
                             { 0.0,   0.0, 2.28, 0.0}}};

// Power iteration control.
double k_eff = 1.0;			// Initial eigenvalue approximation.
double TOL_PIT_CM = 5e-5;		// Tolerance for eigenvalue convergence when solving on coarse mesh.
double TOL_PIT_RM = 5e-7;		// Tolerance for eigenvalue convergence when solving on reference mesh.

// Weak forms.
#include "forms.cpp"
// Norms in the axisymmetric coordinate system.
#include "norms.cpp"

// Source function.
void source_fn(int n, Tuple<scalar*> values, scalar* out)
{
  for (int i = 0; i < n; i++)
  {
    out[i] = (nu[1][0] * Sf[1][0] * values.at(0)[i] +
        nu[1][1] * Sf[1][1] * values.at(1)[i] +
        nu[1][2] * Sf[1][2] * values.at(2)[i] +
        nu[1][3] * Sf[1][3] * values.at(3)[i]);
  }
}

// Integral over the active core.
double integrate(MeshFunction* sln, int marker)
{
  Quad2D* quad = &g_quad_2d_std;
  sln->set_quad_2d(quad);

  double integral = 0.0;
  Element* e;
  Mesh* mesh = sln->get_mesh();

  for_all_active_elements(e, mesh)
  {
    if (e->marker == marker)
    {
      update_limit_table(e->get_mode());
      sln->set_active_element(e);
      RefMap* ru = sln->get_refmap();
      int o = sln->get_fn_order() + ru->get_inv_ref_order();
      limit_order(o);
      sln->set_quad_order(o, H2D_FN_VAL);
      scalar *uval = sln->get_fn_values();
      double* x = ru->get_phys_x(o);
      double result = 0.0;
      h1_integrate_expression(x[i] * uval[i]);
      integral += result;
    }
  }

  return 2.0 * M_PI * integral;
}

// Calculate number of negative solution values.
int get_num_of_neg(MeshFunction *sln)
{
	Quad2D* quad = &g_quad_2d_std;
  sln->set_quad_2d(quad);
  Element* e;
  Mesh* mesh = sln->get_mesh();

  int n = 0;

  for_all_active_elements(e, mesh)
  {
    update_limit_table(e->get_mode());
    sln->set_active_element(e);
    RefMap* ru = sln->get_refmap();
    int o = sln->get_fn_order() + ru->get_inv_ref_order();
    limit_order(o);
    sln->set_quad_order(o, H2D_FN_VAL);
    scalar *uval = sln->get_fn_values();
    int np = quad->get_num_points(o);

		for (int i = 0; i < np; i++)
			if (uval[i] < -1e-12)
				n++;
  }

  return n;
}

// Power iteration using "ls" as the linear system associated with the generalized
// eigenvalue problem and "iter_X" as the initial guess for eigenvector; "ls"
// is assumed to be already assembled and only rhs updates are performed; converged
// eigenvectors are stored in "sln_X" and the eigenvalue in global variable "k_eff".
void power_iteration(Solution *sln1, Solution *sln2, Solution *sln3, Solution *sln4,
		     Solution *iter1, Solution *iter2, Solution *iter3, Solution *iter4,
		     LinSystem *ls, double tol)
{
  bool eigen_done = false;
  // TODO: remove
  int it = 0;
  // TODO: remove
  info("%.8g, %.8g, %.8g, %.8g", h1_norm_axisym(iter1), h1_norm_axisym(iter2), h1_norm_axisym(iter3), h1_norm_axisym(iter4));
  do {
    // Solve for new eigenvectors.
    ls->solve(Tuple<Solution*>(sln1, sln2, sln3, sln4));
    // TODO: remove
    if (it++ == 0) info("%.8g, %.8g, %.8g, %.8g", h1_norm_axisym(sln1), h1_norm_axisym(sln2), h1_norm_axisym(sln3), h1_norm_axisym(sln4));
    // Update fission sources.
    SimpleFilter source(source_fn, Tuple<MeshFunction*>(sln1, sln2, sln3, sln4));
    SimpleFilter source_prev(source_fn, Tuple<MeshFunction*>(iter1, iter2, iter3, iter4));

    // Compute eigenvalue.
    double k_new = k_eff * (integrate(&source, marker_core) / integrate(&source_prev, marker_core));

    info("      dominant eigenvalue (est): %g, rel error: %g", k_new, fabs((k_eff - k_new) / k_new));

    // Stopping criterion.
    if (fabs((k_eff - k_new) / k_new) < tol) eigen_done = true;

    // Store eigenvectors for next iteration.
    iter1->copy(sln1);
    iter2->copy(sln2);
    iter3->copy(sln3);
    iter4->copy(sln4);

    // Update eigenvalue.
    k_eff = k_new;

    if (!eigen_done)
      // Update rhs of the system considering the updated eigenpair approximation.
      ls->assemble(true);
  }
  while (!eigen_done);
}

int main(int argc, char* argv[])
{
  // Time measurement.
  TimePeriod cpu_time;
  cpu_time.tick();

  // Load the mesh.
  Mesh mesh1, mesh2, mesh3, mesh4;
  H2DReader mloader;
  mloader.load("reactor.mesh", &mesh1);

  // Obtain meshes for the 2nd to 4th group by cloning the mesh loaded for the 1st group.
  // This initializes the multimesh hp-FEM.
  mesh2.copy(&mesh1);
  mesh3.copy(&mesh1);
  mesh4.copy(&mesh1);

  // Initial uniform refinements.
  info("Setting initial conditions.");
  for (int i = 0; i < INIT_REF_NUM[0]; i++) mesh1.refine_all_elements();
  for (int i = 0; i < INIT_REF_NUM[1]; i++) mesh2.refine_all_elements();
  for (int i = 0; i < INIT_REF_NUM[2]; i++) mesh3.refine_all_elements();
  for (int i = 0; i < INIT_REF_NUM[3]; i++) mesh4.refine_all_elements();

  // Solution variables.
  Solution iter1, iter2, iter3, iter4,                          // Previous iterations.
           sln1_coarse, sln2_coarse, sln3_coarse, sln4_coarse,	// Current iterations on coarse meshes.
	         sln1_fine, sln2_fine, sln3_fine, sln4_fine;	        // Current iterations on fine meshes.

  // Set initial conditions for the Newton's method.
  iter1.set_const(&mesh1, 1.00);
  iter2.set_const(&mesh2, 1.00);
  iter3.set_const(&mesh3, 1.00);
  iter4.set_const(&mesh4, 1.00);

  // Create H1 spaces with default shapesets.
  H1Space space1(&mesh1, bc_types, essential_bc_values, P_INIT[0]);
  H1Space space2(&mesh2, bc_types, essential_bc_values, P_INIT[1]);
  H1Space space3(&mesh3, bc_types, essential_bc_values, P_INIT[2]);
  H1Space space4(&mesh4, bc_types, essential_bc_values, P_INIT[3]);

  // Initialize the weak formulation.
  WeakForm wf(4);
  wf.add_matrix_form(0, 0, callback(biform_0_0), H2D_SYM);
  wf.add_matrix_form(1, 1, callback(biform_1_1), H2D_SYM);
  wf.add_matrix_form(1, 0, callback(biform_1_0));
  wf.add_matrix_form(2, 2, callback(biform_2_2), H2D_SYM);
  wf.add_matrix_form(2, 1, callback(biform_2_1));
  wf.add_matrix_form(3, 3, callback(biform_3_3), H2D_SYM);
  wf.add_matrix_form(3, 2, callback(biform_3_2));
  wf.add_vector_form(0, callback(liform_0), marker_core, Tuple<MeshFunction*>(&iter1, &iter2, &iter3, &iter4));
  wf.add_vector_form(1, callback(liform_1), marker_core, Tuple<MeshFunction*>(&iter1, &iter2, &iter3, &iter4));
  wf.add_vector_form(2, callback(liform_2), marker_core, Tuple<MeshFunction*>(&iter1, &iter2, &iter3, &iter4));
  wf.add_vector_form(3, callback(liform_3), marker_core, Tuple<MeshFunction*>(&iter1, &iter2, &iter3, &iter4));
  wf.add_matrix_form_surf(0, 0, callback(biform_surf_0_0), bc_vacuum);
  wf.add_matrix_form_surf(1, 1, callback(biform_surf_1_1), bc_vacuum);
  wf.add_matrix_form_surf(2, 2, callback(biform_surf_2_2), bc_vacuum);
  wf.add_matrix_form_surf(3, 3, callback(biform_surf_3_3), bc_vacuum);

  // Initialize and solve coarse mesh problem.
  LinSystem ls(&wf, Tuple<Space*>(&space1, &space2, &space3, &space4));
  ls.assemble();
  info("Coarse mesh power iteration, %d + %d + %d + %d = %d ndof:",
  ls.get_num_dofs(0), ls.get_num_dofs(1), ls.get_num_dofs(2), ls.get_num_dofs(3), ls.get_num_dofs());
  power_iteration(&sln1_coarse, &sln2_coarse, &sln3_coarse, &sln4_coarse,
	          &iter1, &iter2, &iter3, &iter4,
		  			&ls, TOL_PIT_CM);

  // Initialize refinement selector.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);

  // Adaptivity loop:
  int as = 1; bool done = false;
  int order_increase = 1;
  do {

    info("---- Adaptivity step %d:", as);

    // Initialize reference problem.
    RefSystem rs(&ls, order_increase);
    if (order_increase > 1)  order_increase--;

    // First time project coarse mesh solutions on fine meshes.
    if (as == 1)
      rs.project_global(Tuple<MeshFunction*>(&sln1_coarse, &sln2_coarse, &sln3_coarse, &sln4_coarse),
                       	Tuple<Solution*>(&iter1, &iter2, &iter3, &iter4),
                       	matrix_forms_tuple_t(callback_pairs(projection_biform)), vector_forms_tuple_t(callback_pairs(projection_liform)));

    // Solve the fine mesh problem.
    rs.assemble();
    power_iteration(&sln1_fine, &sln2_fine, &sln3_fine, &sln4_fine,
		          &iter1, &iter2, &iter3, &iter4,
		          &rs, TOL_PIT_RM);

    // Either solve on coarse mesh or project the fine mesh solution on the coarse mesh.
    if (SOLVE_ON_COARSE_MESH) {
      if (as > 1) {
        ls.assemble();

        power_iteration(&sln1_coarse, &sln2_coarse, &sln3_coarse, &sln4_coarse,
	  	        &iter1, &iter2, &iter3, &iter4,
		        	&ls, TOL_PIT_CM);
      }
    }
    else {
      ls.project_global(Tuple<MeshFunction*>(&sln1_fine, &sln2_fine, &sln3_fine, &sln4_fine),
                        Tuple<Solution*>(&sln1_coarse, &sln2_coarse, &sln3_coarse, &sln4_coarse),
                        matrix_forms_tuple_t(callback_pairs(projection_biform)), vector_forms_tuple_t(callback_pairs(projection_liform)));
    }

    // Time measurement.
    cpu_time.tick();

    // Skip visualization time.
    cpu_time.tick(H2D_SKIP);

    // Report the number of negative eigenfunction values.
    info("Num. of negative values: %d, %d, %d, %d",
          get_num_of_neg(&sln1_coarse), get_num_of_neg(&sln2_coarse),
          get_num_of_neg(&sln3_coarse), get_num_of_neg(&sln4_coarse));

    // Calculate element errors and total error estimate.
    H1Adapt hp(&ls);
    hp.set_error_form(0, 0, callback(biform_0_0));
    hp.set_error_form(1, 1, callback(biform_1_1));
    hp.set_error_form(1, 0, callback(biform_1_0));
    hp.set_error_form(2, 2, callback(biform_2_2));
    hp.set_error_form(2, 1, callback(biform_2_1));
    hp.set_error_form(3, 3, callback(biform_3_3));
    hp.set_error_form(3, 2, callback(biform_3_2));

    // Calculate element errors and error estimate for adaptivity.
    Tuple<Solution*> slns_coarse(&sln1_coarse, &sln2_coarse, &sln3_coarse, &sln4_coarse);
    Tuple<Solution*> slns_fine(&sln1_fine, &sln2_fine, &sln3_fine, &sln4_fine);
    hp.set_solutions(slns_coarse, slns_fine);

    double energy_err_est = hp.calc_error(H2D_TOTAL_ERROR_REL | H2D_ELEMENT_ERROR_REL) * 100;

    // Time measurement.
    cpu_time.tick();
    double cta = cpu_time.accumulated();

    // Report results.
    info("ndof_coarse: %d + %d + %d + %d = %d", ls.get_num_dofs(0), ls.get_num_dofs(1), ls.get_num_dofs(2),
	 ls.get_num_dofs(3), ls.get_num_dofs());

    // eigenvalue error w.r.t. solution obtained on a 3x uniformly refined mesh
  	// with uniform distribution of polynomial degrees (=4), converged to within
  	// tolerance of 5e-11; in units of percent-milli (pcm)
  	double keff_err = 1e5*fabs(k_eff - 1.1409144)/1.1409144;

  	info("total err_est_coarse (energy): %g%%", energy_err_est);
  	info("k_eff err: %g%%", keff_err);

    cpu_time.tick(H2D_SKIP);

    // If err_est too large, adapt the mesh.
    if (energy_err_est < ERR_STOP) break;
    else {
      done = hp.adapt(&selector, THRESHOLD, STRATEGY, MESH_REGULARITY);
      if (ls.get_num_dofs() >= NDOF_STOP) done = true;
    }

    as++;
  }
  while(done == false);

  info("Total running time: %g s", cta);

#define ERROR_SUCCESS                               0
#define ERROR_FAILURE                               -1
  int n_dof_1 = space1.get_num_dofs(),
      n_dof_2 = space2.get_num_dofs();
      n_dof_3 = space3.get_num_dofs();
      n_dof_4 = space4.get_num_dofs();

  int n_dof_1_allowed = 400,
      n_dof_2_allowed = 2900;
      n_dof_3_allowed = 2900;
      n_dof_4_allowed = 2900;

  int n_neg = get_num_of_neg(&sln1) + get_num_of_neg(&sln2) + get_num_of_neg(&sln3) + get_num_of_neg(&sln4);
  int n_neg_allowed = 0;

  int n_iter = iadapt;
  int n_iter_allowed = 20;

  double error = error_h1;
  double error_allowed = 4.5;

  printf("n_dof_actual  = %d,%d\n", n_dof_1, n_dof_2);
  printf("n_dof_allowed = %d,%d\n", n_dof_1_allowed, n_dof_2_allowed);
  printf("n_iter_actual = %d\n", n_iter);
  printf("n_iter_allowed= %d\n", n_iter_allowed);
  printf("n_neg_actual  = %d\n", n_neg);
  printf("n_neg_allowed = %d\n", n_neg_allowed);
  printf("error_actual  = %g\n", error);
  printf("error_allowed = %g\n", error_allowed);

  if (   n_dof_1 <= n_dof_1_allowed && n_dof_2 <= n_dof_2_allowed
      && n_neg <= n_neg_allowed
      && n_iter <= n_iter_allowed
      && error <= error_allowed )   {
    printf("Success!\n");
    return ERROR_SUCCESS;
  }
  else {
    printf("Failure!\n");
    return ERROR_FAILURE;
  }
}

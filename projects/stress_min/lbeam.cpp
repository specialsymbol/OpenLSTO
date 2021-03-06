#include "M2DO_FEA.h"
#include "M2DO_LSM.h"

#include <fstream>

using namespace std;
using namespace Eigen;

namespace FEA = M2DO_FEA;
namespace LSM = M2DO_LSM;

typedef unsigned int uint;

///////////////////////////////////////////////////////////////////////////////
// DESCRIPTION: In this project, we will minimize the stress of a L beam     //
// subject to a volume constraint (70%) with a point load applied at the     //
// tip.                                                                      //
///////////////////////////////////////////////////////////////////////////////

int main () {

  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  //                 SETTINGS FOR THE FINITE ELEMENT ANALYSIS                 //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////

  /*
    Dimensionality of problem
  */

  const int space_dim = 2;  // spacedim = dim = dimensions; 2D or 3D

  /*
    FEA mesh and level set mesh parameters
   */

  // mesh size
  const uint num_elem_x = 100;
  const uint num_elem_y = 100;

  // Create the FEA mesh
  FEA::Mesh fea_mesh(space_dim);

  MatrixXd fea_box(4, 2);

  fea_box << 0, 0,
    num_elem_x, 0,
    num_elem_x, num_elem_y,
             0, num_elem_y;

  vector<int> num_elem = {num_elem_x, num_elem_y};

  int element_order = 2;
  fea_mesh.MeshSolidHyperRectangle(num_elem, fea_box, element_order, false);
  fea_mesh.is_structured = true;
  fea_mesh.AssignDof();


  /*
    Define material properties
  */

  // Linear elastic properties
  double E   = 1;   // Young's modulus [N/m]
  double nu  = 0.3; // poisson's ratio
  double rho = 1.0; // density         [kg/m^3]

  // Add material properties
  fea_mesh.solid_materials.push_back(FEA::SolidMaterial(space_dim, E, nu, rho));


  /*
    Add a homogeneous Dirichlet boundary condition (fix nodes at the root).
  */

  vector<double> coord; // coordinate where BC is placed
  vector<double> tol;   // area of affect of the BC, where coord is the origin

  // Clamped condition at the top of the beam
  coord = {0.0, num_elem_y};
  tol   = {num_elem_x + 0.1, 0.1};

  vector<int> fixed_nodes = fea_mesh.GetNodesByCoordinates(coord, tol);

  // Get degrees of freedom associated with the fixed nodes
  vector<int> fixed_dof = fea_mesh.dof(fixed_nodes);

  // Apply magnitude of displacement at the BC
  vector<double> amplitude(fixed_dof.size(), 0.0); // Values equal to zero.


  /*
    Create loads
  */

  // Mechanical load(s)
  vector<double> load_coord = {num_elem_x, double(num_elem_y)*2.0/5.0};
  vector<double> load_tol   = {1.1, 0.1};

  // Get vector of associated loads and degrees of freedom
  vector<int> load_nodes = fea_mesh.GetNodesByCoordinates(load_coord, load_tol);
  vector<int> load_dof  = fea_mesh.dof(load_nodes);

  // Assign magnitudes to the point load
  uint num_load_nodes = load_nodes.size();
  vector<double> load_values(load_nodes.size() * 2);
  for (uint i = 0; i < num_load_nodes; i++) {

    load_values[2*i]   =  0.0;                          // x component
    load_values[2*i+1] = -3.0 / double(num_load_nodes); // y component

  }

  FEA::PointValues point_load(load_dof, load_values);


  /*
    Next we specify that we will undertake a stationary study, which takes the
    form [K]{u} = {f}.
  */

  FEA::StationaryStudy fea_study(fea_mesh);

  // Add boundary condition
  fea_study.AddBoundaryConditions(FEA::DirichletBoundaryConditions(fixed_dof, amplitude, fea_mesh.n_dof));

  // END OF SETTINGS FOR THE FINITE ELEMENT ANALYSIS



  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  //                   SETTINGS FOR THE SENSITIVITY ANALYSIS                  //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////

  /*
    Sensitivity analysis setup
   */

  int sens_type = 1; // type of sensitivity being calcuated. 0 is compliance, 1
                     //is stress
  double least_sq_radius = 2.0; //setting least square calculation to 2.0 grid
                                //spaces

  // Initialize sensitvity analysis
  FEA::SensitivityAnalysis sens(fea_study);

  // END OF SETTINGS FOR THE SENSITIVITY ANALYSIS



  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  //                     SETTINGS FOR THE LEVEL SET METHOD                    //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////

  /*
    Define LSM parameters
  */

  double move_limit = 0.5;      // CFL limit
  double band_width = 6;        // the width of the narrow band
  bool is_fixed_domain = false; // whether the domain boundary is fixed
  bool is_periodic = false;
  double num_reinit = 0;        // Num of cycles since last sign dist reinit

  /*
    LSM mesh
   */

  // Initialize the level set mesh
  LSM::Mesh lsm_mesh(num_elem_x, num_elem_y, is_periodic);

  // Define vectors of points for L-beam internal edges
  vector<LSM::Coord> vertical_edge(2), horizontal_edge(2);
  double inner_corner = double(num_elem_x)*2/5;

  // Define a rectangular region containing the points for the vertical edge
  vertical_edge[0] = LSM::Coord({inner_corner - 0.01, inner_corner - 0.01});
  vertical_edge[1] = LSM::Coord({inner_corner + 0.01, num_elem_y + 0.01});

  // Define a rectangular region containing the points for the horizontal edge
  horizontal_edge[0] = LSM::Coord({inner_corner - 0.01, inner_corner - 0.01});
  horizontal_edge[1] = LSM::Coord({num_elem_x + 0.01, inner_corner + 0.01});

  // Define mesh boundary
  lsm_mesh.createMeshBoundary(vertical_edge);
  lsm_mesh.createMeshBoundary(horizontal_edge);


  /*
    Seed initial holes
   */

  vector<LSM::Hole> holes;

  // Define radius of holes (uniform)
  double hole_radius = 10;

  // Add holes

  holes.push_back(LSM::Hole(/*x coord*/ 20, /*y coord*/ 20, hole_radius));
  holes.push_back(LSM::Hole(20, 50, hole_radius));
  holes.push_back(LSM::Hole(20, 80, hole_radius));
  holes.push_back(LSM::Hole(50, 20, hole_radius));
  holes.push_back(LSM::Hole(80, 20, hole_radius));


  /*
    Set up level set and boundary objects
   */

  // Initialize the level set object
  LSM::LevelSet level_set(lsm_mesh, holes, move_limit,
			  band_width, is_fixed_domain);

  // Kill level set nodes that aren't in L-beam region
  vector<LSM::Coord> kill_region(2);
  kill_region[0] = LSM::Coord({inner_corner + 0.01, inner_corner + 0.01});
  kill_region[1] = LSM::Coord({num_elem_x + 0.01, num_elem_y + 0.01});
  level_set.killNodes(kill_region);

  // Define level set boundary (L-beam inner edges)
  level_set.createLevelSetBoundary(vertical_edge);
  level_set.createLevelSetBoundary(horizontal_edge);

  // Vector of points to fix Level set nodes (useful for load points)
  double tol_x = 3.01, tol_y = 2.01;
  double load_coord_x = num_elem_x, load_coord_y = double(num_elem_y)*2/5;
  std::vector<LSM::Coord> points(2);
  points[0] = LSM::Coord({load_coord_x - tol_x, load_coord_y - tol_y});
  points[1] = LSM::Coord({load_coord_x + 0.01, load_coord_y + 0.01});
  level_set.fixNodes(points);
  points.clear();

  // Initialize the level set to a signed distance function
  level_set.reinitialise();

  // Create a boundary instance
  LSM::Boundary boundary(level_set);

  // END OF SETTINGS FOR THE LEVEL SET METHOD



  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  //                       SETTINGS FOR THE OPTIMIZATION                      //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////

  /*
    Define parameters needed for optimization loop
  */

  // Define/declare parameters needed for optimization loop
  uint max_iter = 500;              // maximum number of iterations
  double max_area = 0.4;            // maximum material area
  bool is_max = false;              // maximization problem?
  double mesh_area = lsm_mesh.width * lsm_mesh.height
    - pow(double(lsm_mesh.width)*3/5, 2); // LSM mesh area
  double max_diff = 0.001;          // max relative difference for convergence
  double p_norm = 6;                // p_norm value for stress

  double time = 0;                  // running time
  vector<double> lambdas(2);        // lambda values for the optimizer
  vector<double> objective_values;  // vector to save objective history
  double relative_difference = 1.0; // variable to stop loop
  uint count_iter = 0;              // number of iterations counter

  // END OF SETTINGS FOR THE OPTIMIZATION



  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  //                    LEVEL SET TOPOLOGY OPTIMIZATION LOOP                  //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////

  /*
    Set up output files/headers
  */

  // Create directories for output if they don't already exist
  system("mkdir -p results/history");
  system("mkdir -p results/level_set");
  system("mkdir -p results/area_fractions");
  system("mkdir -p results/boundary_segments");

  // Remove any existing files from output directories
  system("find ./results -type f -name '*.txt' -delete");
  system("find ./results -type f -name '*.vtk' -delete");
  
  // Define precision for output file
  uint txt_precision = 16;

  // Set up iteration history text file
  string txt_file_name = "results/history/history.txt";

  remove(txt_file_name.c_str());

  ofstream txt_file;
  txt_file.open(txt_file_name, ios_base::app);
  txt_file.precision(txt_precision);
  txt_file << "Iteration\tStress\tTvm_max\tArea\tChange\n";

  // Initialize input/output object
  LSM::InputOutput io;
  io.saveLevelSetVTK(count_iter, level_set, false, false, "results/level_set");
  io.saveAreaFractionsVTK(count_iter, lsm_mesh, "results/area_fractions");

  cout << "\nStarting stress minimization...\n" << endl;

  // Print output header
  printf("---------------------------------------------\n");
  printf("%8s %12s %10s %10s\n", "Iteration", "Objective", "Tvm_max",  "Area");
  printf("---------------------------------------------\n");


  /*
    Start of optimization loop
   */

  while (count_iter < max_iter) {

    count_iter++;

    // Perform boundary discretisation.
    boundary.discretise(false, lambdas.size()) ;

    // Compute and assign area fractions to FEA mesh
    boundary.computeAreaFractions() ;

    for (uint i=0; i < fea_mesh.solid_elements.size(); i++) {

      //Assign area fraction according to level set information
      if (lsm_mesh.elements[i].area < 1e-6) fea_mesh.solid_elements[i].area_fraction = 1e-6;
      else fea_mesh.solid_elements[i].area_fraction = lsm_mesh.elements[i].area;

    }

    // Assemble stiffness matrix [K] using area fraction method:
    fea_study.AssembleKWithAreaFractions(false);

    // Assemble loads
    fea_study.AssembleF(point_load, false);

    // Solve equation
    fea_study.SolveWithCG();

    // Compute sensitivities at the Gauss points.
    sens.ComputeStressSensitivities(false, p_norm);

    // Initalize variable for largest (absolute) boundary pt sensitivity
    double abs_bsens_max = 0.0;

    // Assign sensitivities to all the boundary points
    for (int i = 0; i < boundary.points.size(); i++) {

      // current boundary point
      vector<double> boundary_point (2, 0.0);
      boundary_point[0] = boundary.points[i].coord.x;
      boundary_point[1] = boundary.points[i].coord.y;

      // Interpolate Gauss point sensitivities by least squares
      sens.ComputeBoundarySensitivities(boundary_point, least_sq_radius, sens_type, p_norm);

      // Assign sensitvities
      boundary.points[i].sensitivities[0] = -sens.boundary_sensitivities[i];
      boundary.points[i].sensitivities[1] = -1;

    }

    sens.boundary_sensitivities.clear();


    // Time step associated with the iteration.
    double time_step;
    vector<double> constraint_distances;

    // Push current distance from constraint violation into vector.
    constraint_distances.push_back(mesh_area * max_area - boundary.area);

    // Initialise the optimisation object.
		LSM::Optimise optimise (boundary.points, time_step, move_limit) ;

    // Set up required optimisation parameters
    optimise.length_x      = lsm_mesh.width;
    optimise.length_y      = lsm_mesh.height;
    optimise.boundary_area = boundary.area;   //area of the structure
    optimise.mesh_area     = mesh_area;       // area of the entire mesh
    optimise.max_area      = max_area;

    // Perform the optimisation
    double reduced_move_limit = 0.15;
    optimise.Solve_LbeamStress_With_NewtonRaphson(reduced_move_limit);


    optimise.get_lambdas(lambdas);

    // Extend boundary point velocities to all narrow band nodes.
    level_set.computeVelocities(boundary.points);

    // Compute gradient of the signed distance function within the narrow band.
    level_set.computeGradients();

    // Update the level set function.
    bool is_reinitialised = level_set.update(time_step);

    // Reinitialise the signed distance function, if necessary.
    if (!is_reinitialised) {

      if (num_reinit == 1) {

	level_set.reinitialise();
	num_reinit = 0;

      }

    } else num_reinit = 0;

    num_reinit++; // increment the number of steps since reinitialization

    // Increment the time.
    time += time_step;

    // Calculate current area fraction.
    double area = boundary.area / mesh_area;

    // Convergence criterion [Dunning_11_FINEL]
    objective_values.push_back(sens.objective);
    double objective_value_k, objective_value_m;
    if (count_iter > 5) {

      objective_value_k = sens.objective;
      relative_difference = 0.0;

      for (uint i = 1; i <= 5; i++) {

	objective_value_m = objective_values[count_iter - i - 1];
	relative_difference = max(relative_difference, abs((objective_value_k - objective_value_m)/objective_value_k));

      }

    }

    // Print statistics.
    printf("%8.1f %12.4f %10.4f %10.4f\n", double(count_iter), sens.objective,
	   sens.von_mises_max, area);

    // Write statistics to text file
    txt_file << count_iter << "\t" << sens.objective << "\t"
	     << sens.von_mises_max << "\t" << area << "\t"
	     << relative_difference << "\n";

    // Write level set to file.
    io.saveLevelSetVTK(count_iter, level_set, false, false, "results/level_set");
    io.saveAreaFractionsVTK(count_iter, lsm_mesh, "results/area_fractions");
    io.saveBoundarySegmentsTXT(count_iter, boundary, "results/boundary_segments");

    if ((relative_difference <= 0.0005) & (area <= 1.001 * max_area)) break;

  }

  txt_file.close();

  // END OF LEVEL SET TOPOLOGY OPTIMIZATION LOOP



  /*
    Aaaaaand that's all, folks!
  */

  cout << "\nProgram complete.\n\n";

  return 0;
}

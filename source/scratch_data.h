//
// SPDX-License-Identifier: MIT
// Copyright (C) 2020 by the ryujin authors
//

#ifndef SCRATCH_DATA_H
#define SCRATCH_DATA_H

#include "discretization.h"

#include <deal.II/fe/fe_values.h>
#include <deal.II/lac/full_matrix.h>

namespace ryujin
{

  /**
   * Internal scratch data for thread parallelized assembly. See the
   * deal.II Workstream documentation for details.
   */
  template <int dim>
  class AssemblyScratchData
  {
  public:
    AssemblyScratchData(const AssemblyScratchData<dim> &assembly_scratch_data)
        : AssemblyScratchData(assembly_scratch_data.discretization_)
    {
    }


    AssemblyScratchData(const ryujin::Discretization<dim> &discretization)
        : discretization_(discretization)
        , fe_values_(discretization_.mapping(),
                     discretization_.finite_element(),
                     discretization_.quadrature(),
                     dealii::update_values | dealii::update_gradients |
                         dealii::update_quadrature_points |
                         dealii::update_JxW_values)
        , face_quadrature_(3) // FIXME
        , fe_face_values_(discretization_.mapping(),
                          discretization_.finite_element(),
                          face_quadrature_,
                          dealii::update_normal_vectors |
                              dealii::update_values | dealii::update_JxW_values)
    {
    }

    const ryujin::Discretization<dim> &discretization_;
    dealii::FEValues<dim> fe_values_;
    const dealii::QGauss<dim - 1> face_quadrature_;
    dealii::FEFaceValues<dim> fe_face_values_;
  };

  /**
   * Internal copy data for thread parallelized assembly. See the deal.II
   * Workstream documentation for details.
   */
  template <int dim, typename Number = double>
  class AssemblyCopyData
  {
  public:
    bool is_artificial_;
    std::vector<dealii::types::global_dof_index> local_dof_indices_;
    std::map<dealii::types::global_dof_index,
             std::tuple<dealii::Tensor<1, dim>,
                        dealii::types::boundary_id,
                        dealii::Point<dim>>>
        local_boundary_map_;
    dealii::FullMatrix<Number> cell_mass_matrix_;
    std::array<dealii::FullMatrix<Number>, dim> cell_cij_matrix_;
    dealii::FullMatrix<Number> cell_betaij_matrix_;
    Number cell_measure_;
  };

} // namespace ryujin

#endif /* SCRATCH_DATA_H */

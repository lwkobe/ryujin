//
// SPDX-License-Identifier: MIT
// Copyright (C) 2020 by the ryujin authors
//

#ifndef INITIAL_VALUES_TEMPLATE_H
#define INITIAL_VALUES_TEMPLATE_H

#include "initial_values.h"
#include "simd.h"

#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/vector_tools.templates.h>

#include <random>

namespace ryujin
{
  using namespace dealii;

  template <int dim, typename Number>
  InitialValues<dim, Number>::InitialValues(const std::string &subsection)
      : ParameterAcceptor(subsection)
  {
    ParameterAcceptor::parse_parameters_call_back.connect(std::bind(
        &InitialValues<dim, Number>::parse_parameters_callback, this));

    static constexpr Number gamma = ProblemDescription<dim, Number>::gamma;

    configuration_ = "uniform";
    add_parameter(
        "configuration",
        configuration_,
        "Configuration. Valid options are \"uniform\", \"shock front\", "
        "\"contrast\", \"sod contrast\", \"isentropic vortex\"");

    initial_direction_[0] = 1.;
    add_parameter(
        "initial - direction",
        initial_direction_,
        "Initial direction of shock front, contrast, sod contrast, or vortex");

    initial_position_[0] = 1.;
    add_parameter(
        "initial - position",
        initial_position_,
        "Initial position of shock front, contrast, sod contrast, or vortex");

    initial_1d_state_[0] = gamma;
    initial_1d_state_[1] = 3.0;
    initial_1d_state_[2] = 1.;
    add_parameter("initial - 1d state",
                  initial_1d_state_,
                  "Initial 1d state (rho, u, p) of the uniform, shock front, "
                  "and contrast configurations");

    initial_1d_state_contrast_[0] = gamma;
    initial_1d_state_contrast_[1] = 3.0;
    initial_1d_state_contrast_[2] = 1.;
    add_parameter(
        "initial - 1d state contrast",
        initial_1d_state_contrast_,
        "Contrast 1d state (rho, u, p) of the contrast configuration");

    initial_mach_number_ = 2.0;
    add_parameter("initial - mach number",
                  initial_mach_number_,
                  "Mach number of shock front (S1, S3 = mach * a_L/R), or "
                  "isentropic vortex");

    initial_vortex_beta_ = 5.0;
    add_parameter("vortex - beta",
                  initial_vortex_beta_,
                  "Isentropic vortex strength beta");

    perturbation_ = 0.;
    add_parameter("perturbation",
                  perturbation_,
                  "Add a random perturbation of the specified magnitude to the "
                  "initial state.");
  }


  template <int dim, typename Number>
  void InitialValues<dim, Number>::parse_parameters_callback()
  {
    static constexpr unsigned int problem_dimension =
        ProblemDescription<dim>::problem_dimension;
    static constexpr Number gamma = ProblemDescription<dim, Number>::gamma;
    static constexpr Number b = ProblemDescription<dim, Number>::b;

    /*
     * First, let's normalize the direction:
     */

    AssertThrow(
        initial_direction_.norm() != 0.,
        ExcMessage("Initial shock front direction is set to the zero vector."));
    initial_direction_ /= initial_direction_.norm();

    /*
     * Create a small lambda that translates a 1D state (rho, u, p) into an
     * nD state:
     */

    const auto from_1d_state =
        [=](const dealii::Tensor<1, 3, Number> &state_1d) -> rank1_type {
      const auto &rho = state_1d[0];
      const auto &u = state_1d[1];
      const auto &p = state_1d[2];

      rank1_type state;

      state[0] = rho;
      for (unsigned int i = 0; i < dim; ++i)
        state[1 + i] = rho * u * Number(initial_direction_[i]);
      state[dim + 1] = p / (gamma - Number(1.)) + Number(0.5) * rho * u * u;

      return state;
    };


    /*
     * Now populate the initial_state_ function object:
     */

    if (configuration_ == "uniform") {

      /*
       * A uniform flow:
       */

      initial_state_ = [=](const dealii::Point<dim> & /*point*/, Number /*t*/) {
        return from_1d_state(initial_1d_state_);
      };

    } else if (configuration_ == "shock front") {

      /*
       * Mach shock front S1/S3:
       */

      const auto &rho_R = initial_1d_state_[0];
      const auto &u_R = initial_1d_state_[1];
      const auto &p_R = initial_1d_state_[2];
      const Number mach_S = initial_mach_number_;

      /* a_R^2 = gamma * p / rho / (1 - b * rho) */
      const Number a_R = std::sqrt(gamma * p_R / rho_R / (1 - b * rho_R));
      const Number mach_R = u_R / a_R;

      const Number S3 = mach_S * a_R;
      const Number delta_mach = mach_R - mach_S;

      const Number rho_L =
          rho_R * (gamma + Number(1.)) * delta_mach * delta_mach /
          ((gamma - Number(1.)) * delta_mach * delta_mach + Number(2.));
      Number u_L = (Number(1.) - rho_R / rho_L) * S3 + rho_R / rho_L * u_R;
      Number p_L = p_R *
                   (Number(2.) * gamma * delta_mach * delta_mach -
                    (gamma - Number(1.))) /
                   (gamma + Number(1.));

      dealii::Tensor<1, 3, Number> initial_1d_state_L{{rho_L, u_L, p_L}};

      initial_state_ = [=](const dealii::Point<dim> &point, Number t) {
        const Number position_1d =
            Number((point - initial_position_) * initial_direction_ - S3 * t);

        if (position_1d > 0.) {
          return from_1d_state(initial_1d_state_);
        } else {
          return from_1d_state(initial_1d_state_L);
        }
      };

    } else if (configuration_ == "contrast") {

      /*
       * A contrast:
       */

      initial_state_ = [=](const dealii::Point<dim> &point, Number /*t*/) {
        const Number position_1d = Number((point - initial_position_)[1]);

        if (position_1d > 0.) {
          return from_1d_state(initial_1d_state_);
        } else {
          return from_1d_state(initial_1d_state_contrast_);
        }
      };

    } else if (configuration_ == "sod contrast") {

      /*
       * Contrast of the Sod shock tube:
       */

      dealii::Tensor<1, 3, Number> initial_1d_state_L{
          {Number(0.125), Number(0.), Number(0.1)}};
      dealii::Tensor<1, 3, Number> initial_1d_state_R{
          {Number(1.), Number(0.), Number(1.)}};

      initial_state_ = [=](const dealii::Point<dim> &point, Number /*t*/) {
        const Number position_1d =
            Number((point - initial_position_) * initial_direction_);

        if (position_1d > 0.) {
          return from_1d_state(initial_1d_state_L);
        } else {
          return from_1d_state(initial_1d_state_R);
        }
      };

    } else if (configuration_ == "isentropic vortex") {

      /*
       * 2D isentropic vortex problem. See section 5.6 of Euler-convex
       * limiting paper by Guermond et al.
       */

      if constexpr (dim == 2) {
        initial_state_ = [=](const dealii::Point<dim> &point, Number t) {
          const auto point_bar = point - initial_position_ -
                                 initial_direction_ * initial_mach_number_ * t;
          const Number r_square = Number(point_bar.norm_square());

          const Number factor = initial_vortex_beta_ / Number(2. * M_PI) *
                                exp(Number(0.5) - Number(0.5) * r_square);

          const Number T = Number(1.) - (gamma - Number(1.)) /
                                            (Number(2.) * gamma) * factor *
                                            factor;

          const Number u =
              Number(initial_direction_[0]) * initial_mach_number_ -
              factor * Number(point_bar[1]);

          const Number v =
              Number(initial_direction_[1]) * initial_mach_number_ +
              factor * Number(point_bar[0]);

          const Number rho = ryujin::pow(T, Number(1.) / (gamma - Number(1.)));
          const Number p = ryujin::pow(rho, gamma);
          const Number E =
              p / (gamma - Number(1.)) + Number(0.5) * rho * (u * u + v * v);

          return rank1_type({rho, rho * u, rho * v, E});
        };

      } else {

        AssertThrow(false, dealii::ExcNotImplemented());
      }

    } else {

      AssertThrow(false, dealii::ExcMessage("Unknown initial state."));
    }

    /*
     * Add a random perturbation to the original function object:
     */
    if (perturbation_ != 0.) {
      initial_state_ = [old_state = this->initial_state_,
                        perturbation = this->perturbation_](
                           const dealii::Point<dim> &point, Number t) {
        static std::default_random_engine generator;
        static std::uniform_real_distribution<Number> distribution(-1., 1.);
        auto draw = std::bind(distribution, generator);

        auto state = old_state(point, t);
        for (unsigned int i = 0; i < problem_dimension; ++i)
          state[i] *= (Number(1.) + perturbation * draw());

        return state;
      };
    }
  }


  template <int dim, typename Number>
  typename InitialValues<dim, Number>::vector_type
  InitialValues<dim, Number>::interpolate(
      const OfflineData<dim, Number> &offline_data, Number t)
  {
#ifdef DEBUG_OUTPUT
    std::cout << "InitialValues<dim, Number>::interpolate(t = " << t << ")"
              << std::endl;
#endif

    vector_type U;
    U.reinit(offline_data.vector_partitioner());

    constexpr auto problem_dimension =
        ProblemDescription<dim, Number>::problem_dimension;

    using scalar_type = typename OfflineData<dim, Number>::scalar_type;

    const auto callable = [&](const auto &p) { return initial_state(p, t); };

    scalar_type temp;
    const auto scalar_partitioner = offline_data.scalar_partitioner();
    temp.reinit(scalar_partitioner);

    for (unsigned int d = 0; d < problem_dimension; ++d) {
      VectorTools::interpolate(offline_data.dof_handler(),
                               to_function<dim, Number>(callable, d),
                               temp);
      U.insert_component(temp, d);
    }

    U.update_ghost_values();

    return U;
  }

} /* namespace ryujin */

#endif /* INITIAL_VALUES_TEMPLATE_H */

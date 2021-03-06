//
// SPDX-License-Identifier: MIT
// Copyright (C) 2020 by the ryujin authors
//

#ifndef RIEMANN_SOLVER_TEMPLATE_H
#define RIEMANN_SOLVER_TEMPLATE_H

#include <compile_time_options.h>

#include "limiter.h"
#include "newton.h"
#include "riemann_solver.h"
#include "simd.h"

namespace ryujin
{
  using namespace dealii;

  namespace
  {
    /*
     * We construct a function phi(p) that is montone increasing in p,
     * concave down and whose (weak) third derivative is non-negative and
     * locally bounded [1, p. 912]. We also need to implement derivatives
     * of phi for the quadratic Newton search:
     */

    /**
     * See [1], page 912, (3.4).
     *
     * Cost: 1x pow, 1x division, 2x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline Number
    f(const std::array<Number, 4> &primitive_state, const Number &p_star)
    {
      static_assert(ProblemDescription<1, Number>::b == 0.,
                    "If you change this value, implement the rest...");

      using ScalarNumber = typename get_value_type<Number>::type;

      constexpr auto gamma = ProblemDescription<1, Number>::gamma;
      constexpr auto gamma_inverse =
          ProblemDescription<1, Number>::gamma_inverse;
      constexpr auto gamma_minus_one_inverse =
          ProblemDescription<1, Number>::gamma_minus_one_inverse;

      const auto &[rho, u, p, a] = primitive_state;

      const Number radicand_inverse = ScalarNumber(0.5) * rho *
                                      ((gamma + ScalarNumber(1.)) * p_star +
                                       (gamma - ScalarNumber(1.)) * p);
      const Number true_value = (p_star - p) / std::sqrt(radicand_inverse);

      const auto exponent =
          (gamma - ScalarNumber(1.)) * ScalarNumber(0.5) * gamma_inverse;
      const Number factor = ryujin::pow(p_star / p, exponent) - Number(1.);
      const auto false_value =
          factor * ScalarNumber(2.) * a * gamma_minus_one_inverse;

      return dealii::compare_and_apply_mask<
          dealii::SIMDComparison::greater_than_or_equal>(
          p_star, p, true_value, false_value);
    }

    /**
     * See [1], page 912, (3.4).
     *
     * Cost: 1x pow, 3x division, 1x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline Number
    df(const std::array<Number, 4> &primitive_state, const Number &p_star)
    {
      static_assert(ProblemDescription<1, Number>::b == 0.,
                    "If you change this value, implement the rest...");

      using ScalarNumber = typename get_value_type<Number>::type;

      constexpr auto gamma = ProblemDescription<1, Number>::gamma;
      constexpr auto gamma_inverse =
          ProblemDescription<1, Number>::gamma_inverse;
      constexpr auto gamma_minus_one_inverse =
          ProblemDescription<1, Number>::gamma_minus_one_inverse;
      constexpr auto gamma_plus_one_inverse =
          ProblemDescription<1, Number>::gamma_plus_one_inverse;

      const auto &[rho, u, p, a] = primitive_state;

      const Number radicand_inverse = ScalarNumber(0.5) * rho *
                                      ((gamma + ScalarNumber(1.)) * p_star +
                                       (gamma - ScalarNumber(1.)) * p);
      const Number denominator =
          (p_star + (gamma - ScalarNumber(1.)) * gamma_plus_one_inverse * p);
      const Number true_value =
          (denominator - ScalarNumber(0.5) * (p_star - p)) /
          (denominator * std::sqrt(radicand_inverse));

      const auto exponent =
          (ScalarNumber(-1.) - gamma) * ScalarNumber(0.5) * gamma_inverse;
      const Number factor = (gamma - ScalarNumber(1.)) * ScalarNumber(0.5) *
                            gamma_inverse * ryujin::pow(p_star / p, exponent) /
                            p;
      const auto false_value =
          factor * ScalarNumber(2.) * a * gamma_minus_one_inverse;

      return dealii::compare_and_apply_mask<
          dealii::SIMDComparison::greater_than_or_equal>(
          p_star, p, true_value, false_value);
    }


    /**
     * See [1], page 912, (3.3).
     *
     * Cost: 2x pow, 2x division, 4x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline Number
    phi(const std::array<Number, 4> &riemann_data_i,
        const std::array<Number, 4> &riemann_data_j,
        const Number &p)
    {
      const Number &u_i = riemann_data_i[1];
      const Number &u_j = riemann_data_j[1];

      return f(riemann_data_i, p) + f(riemann_data_j, p) + u_j - u_i;
    }


    /**
     * This is a specialized variant of phi() that computes phi(p_max). It
     * inlines the implementation of f() and eliminates all unnecessary
     * branches in f().
     *
     * Cost: 0x pow, 2x division, 2x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline Number
    phi_of_p_max(const std::array<Number, 4> &riemann_data_i,
                 const std::array<Number, 4> &riemann_data_j)
    {
      using ScalarNumber = typename get_value_type<Number>::type;

      constexpr auto gamma = ProblemDescription<1, Number>::gamma;

      const auto &[rho_i, u_i, p_i, a_i] = riemann_data_i;
      const auto &[rho_j, u_j, p_j, a_j] = riemann_data_j;

      const Number p_max = std::max(p_i, p_j);

      const Number radicand_inverse_i = ScalarNumber(0.5) * rho_i *
                                        ((gamma + ScalarNumber(1.)) * p_max +
                                         (gamma - ScalarNumber(1.)) * p_i);

      const Number value_i = (p_max - p_i) / std::sqrt(radicand_inverse_i);

      const Number radicand_inverse_j = ScalarNumber(0.5) * rho_j *
                                        ((gamma + ScalarNumber(1.)) * p_max +
                                         (gamma - ScalarNumber(1.)) * p_j);

      const Number value_j = (p_max - p_j) / std::sqrt(radicand_inverse_j);

      return value_i + value_j + u_j - u_i;
    }


    /**
     * See [1], page 912, (3.3).
     *
     * Cost: 2x pow, 6x division, 2x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline Number
    dphi(const std::array<Number, 4> &riemann_data_i,
         const std::array<Number, 4> &riemann_data_j,
         const Number &p)
    {
      return df(riemann_data_i, p) + df(riemann_data_j, p);
    }


    /*
     * Next we construct approximations for the two extreme wave speeds of
     * the Riemann fan [1, p. 912, (3.7) + (3.8)] and compute a gap (based
     * on the quality of our current approximation of the two wave speeds)
     * and an upper bound lambda_max of the maximal wave speed:
     */


    /**
     * see [1], page 912, (3.7)
     *
     * Cost: 0x pow, 1x division, 1x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline Number
    lambda1_minus(const std::array<Number, 4> &riemann_data,
                  const Number p_star)
    {
      using ScalarNumber = typename get_value_type<Number>::type;

      constexpr auto gamma = ProblemDescription<1, Number>::gamma;
      constexpr auto gamma_inverse =
          ProblemDescription<1, Number>::gamma_inverse;

      const auto &[rho, u, p, a] = riemann_data;

      const auto factor =
          (gamma + ScalarNumber(1.0)) * ScalarNumber(0.5) * gamma_inverse;
      const Number tmp = positive_part((p_star - p) / p);

      return u - a * std::sqrt(Number(1.0) + factor * tmp);
    }


    /**
     * see [1], page 912, (3.8)
     *
     * Cost: 0x pow, 1x division, 1x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline Number
    lambda3_plus(const std::array<Number, 4> &primitive_state,
                 const Number p_star)
    {
      using ScalarNumber = typename get_value_type<Number>::type;

      constexpr auto gamma = ProblemDescription<1, Number>::gamma;
      constexpr auto gamma_inverse =
          ProblemDescription<1, Number>::gamma_inverse;

      const auto &[rho, u, p, a] = primitive_state;

      const Number factor =
          (gamma + ScalarNumber(1.0)) * ScalarNumber(0.5) * gamma_inverse;
      const Number tmp = positive_part((p_star - p) / p);
      return u + a * std::sqrt(Number(1.0) + factor * tmp);
    }


    /**
     * For two given primitive states <code>riemann_data_i</code> and
     * <code>riemann_data_j</code>, and two guesses p_1 <= p* <= p_2,
     * compute the gap in lambda between both guesses.
     *
     * See [1], page 914, (4.4a), (4.4b), (4.5), and (4.6)
     *
     * Cost: 0x pow, 4x division, 4x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline std::array<Number, 2>
    compute_gap(const std::array<Number, 4> &riemann_data_i,
                const std::array<Number, 4> &riemann_data_j,
                const Number p_1,
                const Number p_2)
    {
      const Number nu_11 = lambda1_minus(riemann_data_i, p_2 /*SIC!*/);
      const Number nu_12 = lambda1_minus(riemann_data_i, p_1 /*SIC!*/);

      const Number nu_31 = lambda3_plus(riemann_data_j, p_1);
      const Number nu_32 = lambda3_plus(riemann_data_j, p_2);

      const Number lambda_max =
          std::max(positive_part(nu_32), negative_part(nu_11));

      const Number gap =
          std::max(std::abs(nu_32 - nu_31), std::abs(nu_12 - nu_11));

      return {gap, lambda_max};
    }


    /**
     * For two given primitive states <code>riemann_data_i</code> and
     * <code>riemann_data_j</code>, and a guess p_2, compute an upper bound
     * for lambda.
     *
     * This is the same lambda_max as computed by compute_gap. The function
     * simply avoids a number of unnecessary computations (in case we do
     * not need to know the gap).
     *
     * Cost: 0x pow, 2x division, 2x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline Number
    compute_lambda(const std::array<Number, 4> &riemann_data_i,
                   const std::array<Number, 4> &riemann_data_j,
                   const Number p_star)
    {
      const Number nu_11 = lambda1_minus(riemann_data_i, p_star);
      const Number nu_32 = lambda3_plus(riemann_data_j, p_star);

      return std::max(positive_part(nu_32), negative_part(nu_11));
    }


    /**
     * Two-rarefaction approximation to p_star computed for two primitive
     * states <code>riemann_data_i</code> and <code>riemann_data_j</code>.
     *
     * See [1], page 914, (4.3)
     *
     * Cost: 2x pow, 2x division, 0x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline Number
    p_star_two_rarefaction(const std::array<Number, 4> &riemann_data_i,
                           const std::array<Number, 4> &riemann_data_j)
    {
      using ScalarNumber = typename get_value_type<Number>::type;

      constexpr auto gamma = ProblemDescription<1, Number>::gamma;
      constexpr auto gamma_inverse =
          ProblemDescription<1, Number>::gamma_inverse;
      constexpr auto gamma_minus_one_inverse =
          ProblemDescription<1, Number>::gamma_minus_one_inverse;

      const auto &[rho_i, u_i, p_i, a_i] = riemann_data_i;
      const auto &[rho_j, u_j, p_j, a_j] = riemann_data_j;

      /*
       * Nota bene (cf. [1, (4.3)]):
       *   a_Z^0 * sqrt(1 - b * rho_Z) = a_Z * (1 - b * rho_Z)
       * We have computed a_Z already, so we are simply going to use this
       * identity below:
       */

      constexpr auto factor = (gamma - ScalarNumber(1.)) * ScalarNumber(0.5);

      const Number numerator = a_i + a_j - factor * (u_j - u_i);
      const Number denominator =
          a_i * ryujin::pow(p_i / p_j, -factor * gamma_inverse) + a_j;

      const auto exponent = ScalarNumber(2.0) * gamma * gamma_minus_one_inverse;

      return p_j * ryujin::pow(numerator / denominator, exponent);
    }


    /**
     * Given the pressure minimum and maximum and two corresponding
     * densities we compute approximations for the density of corresponding
     * shock and expansion waves.
     *
     * [2] Formula (4.4)
     *
     * Cost: 2x pow, 2x division, 0x sqrt
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline std::array<Number, 4>
    shock_and_expansion_density(const Number p_min,
                                const Number p_max,
                                const Number rho_p_min,
                                const Number rho_p_max,
                                const Number p_1,
                                const Number p_2)
    {
      constexpr auto gm1_gp2 =
          ProblemDescription<1, Number>::gamma_minus_one_over_gamma_plus_one;

      const auto rho_p_min_shk =
          rho_p_min * (gm1_gp2 * p_min + p_1) / (gm1_gp2 * p_1 + p_min);

      const auto rho_p_max_shk =
          rho_p_min * (gm1_gp2 * p_max + p_1) / (gm1_gp2 * p_1 + p_max);

      constexpr auto gamma_inverse =
          ProblemDescription<1, Number>::gamma_inverse;

      const auto rho_p_min_exp =
          rho_p_min * ryujin::pow(p_2 / p_min, gamma_inverse);

      const auto rho_p_max_exp =
          rho_p_max * ryujin::pow(p_2 / p_max, gamma_inverse);

      return {rho_p_min_shk, rho_p_max_shk, rho_p_min_exp, rho_p_max_exp};
    }


  } /* anonymous namespace */


  template <int dim, typename Number>
#ifdef OBSESSIVE_INLINING
  DEAL_II_ALWAYS_INLINE inline
#endif
  std::tuple<Number, Number, unsigned int> RiemannSolver<dim, Number>::compute(
      const std::array<Number, 4> &riemann_data_i,
      const std::array<Number, 4> &riemann_data_j)
  {
    /*
     * Step 1:
     *
     * In case we iterate (in the Newton method) we need a good upper and
     * lower bound, p_1 < p_star < p_2, for finding phi(p_star) == 0. In case
     * we do not iterate (because the iteration is really expensive...) we
     * will need p_2 as an approximation to p_star.
     *
     * In any case we have to ensure that phi(p_2) >= 0 (and phi(p_1) <= 0).
     *
     * We will use three candidates, p_min, p_max and the two rarefaction
     * approximation p_star_tilde. We have (up to round-off errors) that
     * phi(p_star_tilde) >= 0. So this is a safe upper bound.
     *
     * Depending on the sign of phi(p_max) we select the following ranges:
     *
     * phi(p_max) <  0:
     *   p_1  <-  p_max   and   p_2  <-  p_star_tilde
     *
     * phi(p_max) >= 0:
     *   p_1  <-  p_min   and   p_2  <-  min(p_max, p_star_tilde)
     *
     * Nota bene:
     *
     *  - The special case phi(p_max) == 0 as discussed in [1] is already
     *    contained in the second condition.
     *
     *  - In principle, we would have to treat the case phi(p_min) > 0 as
     *    well. This corresponds to two expansion waves and a good estimate
     *    for the wavespeed is obtained by setting p_star = 0 and computing
     *    lambda_max with that.
     *    However, it turns out that numerically in this case the
     *    two-rarefaction approximation p_star_tilde is already an
     *    excellent guess and we will have
     *
     *      0 < p_star <= p_star_tilde <= p_min <= p_max.
     *
     *    So let's simply detect this case numerically by checking for p_2 <
     *    p_1 and setting p_1 <- 0 if necessary.
     */

    const Number p_min = std::min(riemann_data_i[2], riemann_data_j[2]);
    const Number p_max = std::max(riemann_data_i[2], riemann_data_j[2]);

    const Number p_star_tilde =
        p_star_two_rarefaction(riemann_data_i, riemann_data_j);

    const Number phi_p_max = phi_of_p_max(riemann_data_i, riemann_data_j);

    Number p_2 =
        dealii::compare_and_apply_mask<dealii::SIMDComparison::less_than>(
            phi_p_max, Number(0.), p_star_tilde, std::min(p_max, p_star_tilde));

    /* If we do no Newton iteration, cut it short: */

    if constexpr (newton_max_iter_ == 0) {
      const Number lambda_max =
          compute_lambda(riemann_data_i, riemann_data_j, p_2);
      return {lambda_max, p_2, -1};
    }

    Number p_1 =
        dealii::compare_and_apply_mask<dealii::SIMDComparison::less_than>(
            phi_p_max, Number(0.), p_max, p_min);

    /*
     * Ensure that p_1 < p_2. If we hit a case with two expansions we might
     * indeed have that p_star_tilde < p_1. Set p_1 = p_2 in this case.
     */

    p_1 = dealii::compare_and_apply_mask<
        dealii::SIMDComparison::less_than_or_equal>(p_1, p_2, p_1, p_2);

    /*
     * Step 2: Perform quadratic Newton iteration.
     *
     * See [1], p. 915f (4.8) and (4.9)
     */

    auto [gap, lambda_max] =
        compute_gap(riemann_data_i, riemann_data_j, p_1, p_2);

    unsigned int i = 0;
    for (; i < newton_max_iter_; ++i) {

      /* We return our current guess if we reach the tolerance... */
      if (std::max(Number(0.), gap - newton_eps<Number>) == Number(0.))
        break;

      // FIXME: Fuse these computations:
      const Number phi_p_1 = phi(riemann_data_i, riemann_data_j, p_1);
      const Number phi_p_2 = phi(riemann_data_i, riemann_data_j, p_2);
      const Number dphi_p_1 = dphi(riemann_data_i, riemann_data_j, p_1);
      const Number dphi_p_2 = dphi(riemann_data_i, riemann_data_j, p_2);

      quadratic_newton_step(p_1, p_2, phi_p_1, phi_p_2, dphi_p_1, dphi_p_2);

      /* Update  lambda_max and gap: */
      {
        auto [gap_new, lambda_max_new] =
            compute_gap(riemann_data_i, riemann_data_j, p_1, p_2);
        gap = gap_new;
        lambda_max = lambda_max_new;
      }
    }

    if constexpr (greedy_dij_) {
      /* FIXME: Abuse the interface and return p_1, p_2 instead of
       * lambda_max, p_2 for the greedy_dij code path */
      return {p_1, p_2, i};

    } else {

#ifdef CHECK_BOUNDS
      const auto phi_p_star = phi(riemann_data_i, riemann_data_j, p_2);
      AssertThrowSIMD(
          phi_p_star,
          [](auto val) { return val >= -newton_eps<ScalarNumber>; },
          dealii::ExcMessage("Invalid state in Riemann problem."));
#endif

      return {lambda_max, p_2, i};
    }
  }


  namespace
  {
    /**
     * For two given 1D primitive states riemann_data_i and riemann_data_j,
     * and left and right estimates p_1 < p_star < p_2
     * compute bounds = {rho_min, rho_max, s_min, salpha_avg, salpha_flux}
     * that are needed in the limiter for the "greedy d_ij" computation.
     */
    template <typename Number>
    DEAL_II_ALWAYS_INLINE inline std::array<Number, 5>
    compute_bounds(const std::array<Number, 4> &riemann_data_i,
                   const std::array<Number, 4> &riemann_data_j,
                   const Number &p_1,
                   const Number &p_2)
    {
      /*
       * Step 3: In case of the greedy lambda_max computation we have to
       * compute bounds on the density for the limiting process:
       */

      const Number p_min = std::min(riemann_data_i[2], riemann_data_j[2]);
      const Number p_max = std::max(riemann_data_i[2], riemann_data_j[2]);

      /* Get the density of the corresponding min and max states: */

      const auto rho_p_min =
          dealii::compare_and_apply_mask<dealii::SIMDComparison::less_than>(
              riemann_data_i[2],
              riemann_data_j[2],
              riemann_data_i[0],
              riemann_data_j[0]);

      const auto rho_p_max =
          dealii::compare_and_apply_mask<dealii::SIMDComparison::less_than>(
              riemann_data_i[2],
              riemann_data_j[2],
              riemann_data_j[0],
              riemann_data_i[0]);

      const auto [rho_p_min_shk, rho_p_max_shk, rho_p_min_exp, rho_p_max_exp] =
          shock_and_expansion_density(
              p_min, p_max, rho_p_min, rho_p_max, p_1, p_2);

      /*
       * We have the following cases:
       *
       *  - phi(p_min) >= 0 : two expansion waves with
       *
       *      p_1 <= p* <= p_2 <= p_min <= p_max  (and p_2 == p_star_tilde)
       *
       *    thus, select [p_2, p_max] as limiter bounds and update
       *
       *      rho_min  =  min(rho_exp_min, rho_exp_max)
       *
       *  - phi(p_min) < 0 and phi(p_max) >= 0 : shock and expansion with
       *
       *      p_min <= p_1 <= p* <= p_2 <= min(p_max, p_star_tilde)
       *
       *    thus, select [p_min, p_max] as limiter bounds and update
       *
       *      rho_min  =  min(rho_min, rho_exp_max)
       *      rho_max  =  max(rho_shk_min, rho_max)
       *
       *  - phi(p_min) < 0 and phi(p_max) < 0 : two shocks with
       *
       *      p_min <= p_max <= p_1 <= p* <= p_2 <= p_star_tilde
       *
       *    thus, select [p_min, p_1] as limiter bounds and update
       *
       *      rho_max  =  max(rho_shk_min, rho_shk_max)
       *
       * In summary, we do the following:
       */

      auto rho_min = std::min(rho_p_min, rho_p_max);
      auto rho_max = std::max(rho_p_min, rho_p_max);

      rho_min = std::min(rho_min, std::min(rho_p_min_exp, rho_p_max_exp));
      rho_max = std::max(rho_max, std::max(rho_p_min_shk, rho_p_max_shk));

      /*
       * And finally we compute s_min of both states.
       *
       * Normally, we would just call ProblemDescription::specific_entropy,
       * however, given the fact that we only have primitive variables
       * available, we do a little dance here and avoid recomputing
       * quantitites:
       *
       * The specific entropy is
       *
       *   s = p * 1/(gamma - 1) * rho ^ (- gamma)
       *
       * We also need entropy bounds (for both states) for enforcing an
       * entropy inequality. We use a Harten-type entropy (with alpha = 1) of
       * the form:
       *
       *   salpha  = (rho^2 e) ^ (1 / (gamma + 1))
       *
       * In primitive variables this translates to
       *
       *   salpha =  (p * 1/(gamma - 1) * rho) ^ (1 / (gamma + 1))
       */

      static_assert(ProblemDescription<1, Number>::b == 0.,
                    "If you change this value, implement the rest...");

      const auto &[rho_i, u_i, p_i, a_i] = riemann_data_i;
      const auto &[rho_j, u_j, p_j, a_j] = riemann_data_j;

      constexpr auto gamma = ProblemDescription<1, Number>::gamma;
      constexpr auto gamma_minus_one_inverse =
          ProblemDescription<1, Number>::gamma_minus_one_inverse;
      constexpr auto gamma_plus_one_inverse =
          ProblemDescription<1, Number>::gamma_plus_one_inverse;

      const auto rho_e_i = p_i * gamma_minus_one_inverse;
      const auto s_i = rho_e_i * ryujin::pow(rho_i, -gamma);
      const auto salpha_i =
          ryujin::pow(rho_e_i * rho_i, gamma_plus_one_inverse);

      const auto rho_e_j = p_j * gamma_minus_one_inverse;
      const auto s_j = rho_e_j * ryujin::pow(rho_j, -gamma);
      const auto salpha_j =
          ryujin::pow(rho_e_j * rho_j, gamma_plus_one_inverse);

      const auto s_min = std::min(s_i, s_j);

      using ScalarNumber = typename get_value_type<Number>::type;

      /* average of entropy: */
      const auto a = ScalarNumber(0.5) * (salpha_i + salpha_j);
      /* flux of entropy: */
      const auto b = ScalarNumber(0.5) * (u_i * salpha_i - u_j * salpha_j);

      return {rho_min, rho_max, s_min, a, b};
    }


    /**
     * For a given (2+dim dimensional) state vector <code>U</code>, and a
     * (normalized) "direction" n_ij, first compute the corresponding
     * projected state in the corresponding 1D Riemann problem, and then
     * compute and return the Riemann data [rho, u, p, a] (used in the
     * approximative Riemann solver).
     */
    template <int dim, typename Number>
    DEAL_II_ALWAYS_INLINE inline std::array<Number, 4> riemann_data_from_state(
        const typename ProblemDescription<dim, Number>::rank1_type &U,
        const dealii::Tensor<1, dim, Number> &n_ij)
    {
      const auto m = ProblemDescription<dim, Number>::momentum(U);
      const auto projected_momentum = n_ij * m;
      const auto perp = m - projected_momentum * n_ij;

      const Number rho_inverse = Number(1.0) / U[0];
      typename ProblemDescription<1, Number>::rank1_type projected(
          {U[0],
           projected_momentum,
           U[1 + dim] - Number(0.5) * perp.norm_square() * rho_inverse});

      return {projected[0],               // rho
              projected[1] * rho_inverse, // u
              ProblemDescription<1, Number>::pressure(projected),
              ProblemDescription<1, Number>::speed_of_sound(projected)};
    }
  } /* anonymous namespace */


  template <int dim, typename Number>
#ifdef OBSESSIVE_INLINING
  DEAL_II_ALWAYS_INLINE inline
#endif
      std::tuple<Number, Number, unsigned int>
      RiemannSolver<dim, Number>::compute(
          const rank1_type &U_i,
          const rank1_type &U_j,
          const dealii::Tensor<1, dim, Number> &n_ij,
          const Number hd_i)
  {
    const auto riemann_data_i = riemann_data_from_state(U_i, n_ij);
    const auto riemann_data_j = riemann_data_from_state(U_j, n_ij);

    if constexpr (!greedy_dij_) {
      return compute(riemann_data_i, riemann_data_j);
    }

    const auto &[p_1, p_2, i] = compute(riemann_data_i, riemann_data_j);
    const auto lambda_max = compute_lambda(riemann_data_i, riemann_data_j, p_2);

    /*
     * If we are greedy, ensure that all the work we are about to do is
     * actually beneficial. We do this by checking whether we have a
     * contrast of more than greedy_threshold_ in the density. If not, cut
     * it short:
     */

    const Number rho_min = std::min(riemann_data_i[0], riemann_data_j[0]);
    const Number rho_max = std::max(riemann_data_i[0], riemann_data_j[0]);

    constexpr ScalarNumber eps = std::numeric_limits<ScalarNumber>::epsilon();
    if (std::max(Number(0.), rho_max * greedy_threshold_ - rho_min + eps) ==
        Number(0.)) {
      return {lambda_max, p_2, i};
    }

    /*
     * So, we are indeed greedy... Lets' try to minimize lambda_max as much
     * as possible. We do this by simply limiting a bar state against an
     * (almost) inviscid update:
     */

    /* bar state: U = 0.5 * (U_i + U_j) */
    const auto U = ScalarNumber(0.5) * (U_i + U_j);

    /* P = - 0.5 * (f_j - f_i) * n_ij */

    const auto f_i = ProblemDescription<dim, Number>::f(U_i);
    const auto f_j = ProblemDescription<dim, Number>::f(U_j);

    dealii::Tensor<1, problem_dimension, Number> P;
    for (unsigned int k = 0; k < problem_dimension; ++k)
      P[k] = ScalarNumber(0.5) * (f_i[k] - f_j[k]) * n_ij;

    auto bounds = compute_bounds(riemann_data_i, riemann_data_j, p_1, p_2);

    (void)hd_i;
    if constexpr (greedy_relax_bounds_) {
      /*
       * Relax entropy bounds a tiny bit: Note, we use a much smaller
       * window r_i = h_i ^ (3/2) than is done for the second order
       * limiting.
       */
      const Number factor = Number(1.) - hd_i;
      std::get<2>(bounds) *= factor;
      std::get<3>(bounds) *= factor;
      std::get<4>(bounds) *= factor;
    }

    constexpr auto limiter = Limiter<dim, Number>::Limiters::entropy_inequality;
    const auto lambda_greedy_inverse =
        Limiter<dim, Number>::template limit<limiter>(
            bounds,
            U,
            P,
            ScalarNumber(1.) / lambda_max,
            ScalarNumber(1000.) / lambda_max);

    const auto lambda_greedy = ScalarNumber(1.) / lambda_greedy_inverse;

#ifdef CHECK_BOUNDS
    AssertThrowSIMD(
        lambda_max - lambda_greedy,
        [](auto val) { return val > -100. * newton_eps<ScalarNumber>; },
        dealii::ExcMessage("Garbled up lambda_greedy."));
#endif

    return {std::min(lambda_greedy, lambda_max), p_2, i};
  }

} /* namespace ryujin */

#endif /* RIEMANN_SOLVER_TEMPLATE_H */

// Copyright 2015 TIER IV, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef OPENSCENARIO_INTERPRETER__STOCHASTIC_DISTRIBUTION_SAMPLER_HPP_
#define OPENSCENARIO_INTERPRETER__STOCHASTIC_DISTRIBUTION_SAMPLER_HPP_

#include <openscenario_interpreter/scope.hpp>
#include <openscenario_interpreter/syntax/double.hpp>
#include <random>

namespace openscenario_interpreter
{
inline namespace random
{
template <typename DistributionT>
struct StochasticDistributionSampler
{
  template <typename... Ts>
  explicit StochasticDistributionSampler(Ts... xs) : distribute(xs...)
  {
  }

  DistributionT distribute;

  auto operator()(std::mt19937 & random_engine) { return distribute(random_engine); }
};
}  // namespace random
}  // namespace openscenario_interpreter
#endif  // OPENSCENARIO_INTERPRETER__STOCHASTIC_DISTRIBUTION_SAMPLER_HPP_

// Copyright 2015-2020 Tier IV, Inc. All rights reserved.
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

#include <simulation_api/behavior/vehicle/behavior_tree.hpp>
#include <simulation_api/behavior/vehicle/follow_lane_sequence/stop_at_crossing_entity_action.hpp>
#include <simulation_api/math/catmull_rom_spline.hpp>

#include <string>
#include <vector>
#include <utility>

namespace entity_behavior
{
namespace vehicle
{
namespace follow_lane_sequence
{
StopAtCrossingEntityAction::StopAtCrossingEntityAction(
  const std::string & name,
  const BT::NodeConfiguration & config)
: entity_behavior::VehicleActionNode(name, config) {}

const boost::optional<openscenario_msgs::msg::Obstacle>
StopAtCrossingEntityAction::calculateObstacle(
  const openscenario_msgs::msg::WaypointsArray & waypoints)
{
  if (!distance_to_stop_target_) {
    return boost::none;
  }
  if (distance_to_stop_target_.get() < 0) {
    return boost::none;
  }
  simulation_api::math::CatmullRomSpline spline(waypoints.waypoints);
  if (distance_to_stop_target_.get() > spline.getLength()) {
    return boost::none;
  }
  openscenario_msgs::msg::Obstacle obstacle;
  obstacle.type = obstacle.ENTITY;
  obstacle.s = distance_to_stop_target_.get();
  return obstacle;
}

const openscenario_msgs::msg::WaypointsArray StopAtCrossingEntityAction::calculateWaypoints()
{
  if (!entity_status.lanelet_pose_valid) {
    throw BehaviorTreeRuntimeError("failed to assign lane");
  }
  if (entity_status.action_status.twist.linear.x >= 0) {
    openscenario_msgs::msg::WaypointsArray waypoints;
    double horizon =
      boost::algorithm::clamp(entity_status.action_status.twist.linear.x * 5, 20, 50);
    simulation_api::math::CatmullRomSpline spline(hdmap_utils->getCenterPoints(route_lanelets));
    waypoints.waypoints = spline.getTrajectory(
      entity_status.lanelet_pose.s,
      entity_status.lanelet_pose.s + horizon, 1.0);
    return waypoints;
  } else {
    return openscenario_msgs::msg::WaypointsArray();
  }
}

boost::optional<double> StopAtCrossingEntityAction::calculateTargetSpeed(
  double current_velocity)
{
  if (!distance_to_stop_target_) {
    return boost::none;
  }
  double rest_distance = distance_to_stop_target_.get() -
    (vehicle_parameters->bounding_box.dimensions.length + 3);
  if (rest_distance < calculateStopDistance()) {
    if (rest_distance > 0) {
      return std::sqrt(2 * 5 * rest_distance);
    } else {
      return 0;
    }
  }
  return current_velocity;
}

BT::NodeStatus StopAtCrossingEntityAction::tick()
{
  getBlackBoardValues();
  if (request != "none" && request != "follow_lane") {
    return BT::NodeStatus::FAILURE;
  }
  if(!driver_model.see_around) {
    return BT::NodeStatus::FAILURE;
  }
  if (getRightOfWayEntities(route_lanelets).size() != 0) {
    return BT::NodeStatus::FAILURE;
  }
  const auto waypoints = calculateWaypoints();
  const auto spline = simulation_api::math::CatmullRomSpline(waypoints.waypoints);
  distance_to_stop_target_ = getDistanceToConflictingEntity(route_lanelets, spline);
  boost::optional<double> target_linear_speed;
  if (distance_to_stop_target_) {
    target_linear_speed = calculateTargetSpeed(entity_status.action_status.twist.linear.x);
  } else {
    target_linear_speed = boost::none;
  }
  if (!distance_to_stop_target_) {
    setOutput("updated_status", calculateEntityStatusUpdated(0));
    const auto obstacle = calculateObstacle(waypoints);
    setOutput("waypoints", waypoints);
    setOutput("obstacle", obstacle);
    return BT::NodeStatus::SUCCESS;
  }
  if (target_speed) {
    if (target_speed.get() > target_linear_speed.get()) {
      target_speed = target_linear_speed.get();
    }
  } else {
    target_speed = target_linear_speed.get();
  }
  setOutput("updated_status", calculateEntityStatusUpdated(target_speed.get()));
  const auto obstacle = calculateObstacle(waypoints);
  setOutput("waypoints", waypoints);
  setOutput("obstacle", obstacle);
  return BT::NodeStatus::RUNNING;
}
}  // namespace follow_lane_sequence
}  // namespace vehicle
}  // namespace entity_behavior

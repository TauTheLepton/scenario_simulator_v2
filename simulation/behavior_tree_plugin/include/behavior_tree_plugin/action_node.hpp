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

#ifndef BEHAVIOR_TREE_PLUGIN__ACTION_NODE_HPP_
#define BEHAVIOR_TREE_PLUGIN__ACTION_NODE_HPP_

#include <behaviortree_cpp_v3/action_node.h>

#include <algorithm>
#include <geometry/spline/catmull_rom_spline.hpp>
#include <memory>
#include <optional>
#include <string>
#include <traffic_simulator/behavior/behavior_plugin_base.hpp>
#include <traffic_simulator/data_type/behavior.hpp>
#include <traffic_simulator/data_type/entity_status.hpp>
#include <traffic_simulator/entity/entity_base.hpp>
#include <traffic_simulator/hdmap_utils/hdmap_utils.hpp>
#include <traffic_simulator/helper/stop_watch.hpp>
#include <traffic_simulator/traffic_lights/traffic_light_manager.hpp>
#include <traffic_simulator_msgs/msg/obstacle.hpp>
#include <traffic_simulator_msgs/msg/waypoints_array.hpp>
#include <unordered_map>
#include <vector>

namespace entity_behavior
{
class ActionNode : public BT::ActionNodeBase
{
public:
  ActionNode(const std::string & name, const BT::NodeConfiguration & config);
  ~ActionNode() override = default;
  auto foundConflictingEntity(const lanelet::Ids & following_lanelets) const -> bool;
  auto getDistanceToConflictingEntity(
    const lanelet::Ids & route_lanelets,
    const math::geometry::CatmullRomSplineInterface & spline) const -> std::optional<double>;
  auto getFrontEntityName(const math::geometry::CatmullRomSplineInterface & spline) const
    -> std::optional<std::string>;
  auto calculateStopDistance(const traffic_simulator_msgs::msg::DynamicConstraints &) const
    -> double;
  auto getDistanceToFrontEntity(const math::geometry::CatmullRomSplineInterface & spline) const
    -> std::optional<double>;
  auto getDistanceToStopLine(
    const lanelet::Ids & route_lanelets,
    const std::vector<geometry_msgs::msg::Point> & waypoints) const -> std::optional<double>;
  auto getDistanceToTrafficLightStopLine(
    const lanelet::Ids & route_lanelets,
    const math::geometry::CatmullRomSplineInterface & spline) const -> std::optional<double>;
  auto getRightOfWayEntities() const -> std::vector<traffic_simulator::CanonicalizedEntityStatus>;
  auto getRightOfWayEntities(const lanelet::Ids & following_lanelets) const
    -> std::vector<traffic_simulator::CanonicalizedEntityStatus>;
  auto getYieldStopDistance(const lanelet::Ids & following_lanelets) const -> std::optional<double>;
  auto getOtherEntityStatus(lanelet::Id lanelet_id) const
    -> std::vector<traffic_simulator::CanonicalizedEntityStatus>;
  auto stopEntity() const -> void;
  auto getHorizon() const -> double;
  auto getActionStatus() const noexcept -> traffic_simulator_msgs::msg::ActionStatus;
  auto getEntityName() const noexcept -> std::string;

  /// throws if the derived class return RUNNING.
  auto executeTick() -> BT::NodeStatus override;

  void halt() override final { setStatus(BT::NodeStatus::IDLE); }

  static BT::PortsList providedPorts()
  {
    return {
      // clang-format off
      BT::InputPort<double>("current_time"),
      BT::InputPort<double>("step_time"),
      BT::InputPort<EntityStatusDict>("other_entity_status"),
      BT::InputPort<std::optional<double>>("target_speed"),
      BT::InputPort<std::shared_ptr<hdmap_utils::HdMapUtils>>("hdmap_utils"),
      BT::InputPort<std::shared_ptr<traffic_simulator::CanonicalizedEntityStatus>>("entity_status"),
      BT::InputPort<std::unordered_map<std::string, traffic_simulator_msgs::msg::EntityType>>("entity_type_list"),
      BT::InputPort<lanelet::Ids>("route_lanelets"),
      BT::InputPort<traffic_simulator::behavior::Request>("request"),
      BT::InputPort<std::shared_ptr<traffic_simulator::TrafficLightManager>>("traffic_light_manager"),
      BT::OutputPort<std::optional<traffic_simulator_msgs::msg::Obstacle>>("obstacle"),
      BT::OutputPort<std::shared_ptr<traffic_simulator::CanonicalizedEntityStatus>>("updated_status"),
      BT::OutputPort<traffic_simulator_msgs::msg::WaypointsArray>("waypoints"),
      BT::OutputPort<traffic_simulator::behavior::Request>("request"),
      // clang-format on
    };
  }
  auto getBlackBoardValues() -> void;
  auto getEntityStatus(const std::string & target_name) const
    -> traffic_simulator::CanonicalizedEntityStatus;
  auto getDistanceToTargetEntityPolygon(
    const math::geometry::CatmullRomSplineInterface & spline, const std::string target_name,
    double width_extension_right = 0.0, double width_extension_left = 0.0,
    double length_extension_front = 0.0, double length_extension_rear = 0.0) const
    -> std::optional<double>;
  auto calculateUpdatedEntityStatus(
    double target_speed, const traffic_simulator_msgs::msg::DynamicConstraints &) const
    -> traffic_simulator::CanonicalizedEntityStatus;
  auto calculateUpdatedEntityStatusInWorldFrame(
    double target_speed, const traffic_simulator_msgs::msg::DynamicConstraints &) const
    -> traffic_simulator::CanonicalizedEntityStatus;

protected:
  traffic_simulator::behavior::Request request;
  std::shared_ptr<hdmap_utils::HdMapUtils> hdmap_utils;
  std::shared_ptr<traffic_simulator::TrafficLightManager> traffic_light_manager;
  std::shared_ptr<traffic_simulator::CanonicalizedEntityStatus> entity_status;
  double current_time;
  double step_time;
  std::optional<double> target_speed;
  std::shared_ptr<traffic_simulator::CanonicalizedEntityStatus> updated_status;
  EntityStatusDict other_entity_status;
  std::unordered_map<std::string, traffic_simulator_msgs::msg::EntityType> entity_type_list;
  lanelet::Ids route_lanelets;

private:
  auto getDistanceToTargetEntityOnCrosswalk(
    const math::geometry::CatmullRomSplineInterface & spline,
    const traffic_simulator::CanonicalizedEntityStatus & status) const -> std::optional<double>;
  auto getDistanceToTargetEntityPolygon(
    const math::geometry::CatmullRomSplineInterface & spline,
    const traffic_simulator::CanonicalizedEntityStatus & status, double width_extension_right = 0.0,
    double width_extension_left = 0.0, double length_extension_front = 0.0,
    double length_extension_rear = 0.0) const -> std::optional<double>;
  auto getConflictingEntityStatus(const lanelet::Ids & following_lanelets) const
    -> std::optional<traffic_simulator::CanonicalizedEntityStatus>;
  auto getConflictingEntityStatusOnCrossWalk(const lanelet::Ids & route_lanelets) const
    -> std::vector<traffic_simulator::CanonicalizedEntityStatus>;
  auto getConflictingEntityStatusOnLane(const lanelet::Ids & route_lanelets) const
    -> std::vector<traffic_simulator::CanonicalizedEntityStatus>;
};
}  // namespace entity_behavior

#endif  // BEHAVIOR_TREE_PLUGIN__ACTION_NODE_HPP_

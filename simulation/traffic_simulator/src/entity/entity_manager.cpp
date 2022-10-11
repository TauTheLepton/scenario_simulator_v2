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

#include <cstdint>
#include <geometry/bounding_box.hpp>
#include <geometry/intersection/collision.hpp>
#include <geometry/transform.hpp>
#include <limits>
#include <memory>
#include <queue>
#include <scenario_simulator_exception/exception.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <traffic_simulator/entity/entity_manager.hpp>
#include <traffic_simulator/helper/helper.hpp>
#include <traffic_simulator/helper/stop_watch.hpp>
#include <unordered_map>
#include <vector>

namespace traffic_simulator
{
namespace entity
{
void EntityManager::broadcastEntityTransform()
{
  std::vector<std::string> names = getEntityNames();
  for (const auto & name : names) {
    geometry_msgs::msg::PoseStamped pose;
    pose.pose = getEntityStatus(name).pose;
    pose.header.stamp = clock_ptr_->now();
    pose.header.frame_id = name;
    broadcastTransform(pose);
  }
}

void EntityManager::broadcastTransform(
  const geometry_msgs::msg::PoseStamped & pose, const bool static_transform)
{
  geometry_msgs::msg::TransformStamped transform_stamped;
  {
    transform_stamped.header.stamp = pose.header.stamp;
    transform_stamped.header.frame_id = "map";
    transform_stamped.child_frame_id = pose.header.frame_id;
    transform_stamped.transform.translation.x = pose.pose.position.x;
    transform_stamped.transform.translation.y = pose.pose.position.y;
    transform_stamped.transform.translation.z = pose.pose.position.z;
    transform_stamped.transform.rotation = pose.pose.orientation;
  }

  if (static_transform) {
    broadcaster_.sendTransform(transform_stamped);
  } else {
    base_link_broadcaster_.sendTransform(transform_stamped);
  }
}

bool EntityManager::checkCollision(const std::string & name0, const std::string & name1)
{
  return name0 != name1 and math::geometry::checkCollision2D(
                              getEntityStatus(name0).pose, getBoundingBox(name0),
                              getEntityStatus(name1).pose, getBoundingBox(name1));
}

visualization_msgs::msg::MarkerArray EntityManager::makeDebugMarker() const
{
  visualization_msgs::msg::MarkerArray marker;
  for (const auto & entity : entities_) {
    entity.second->appendDebugMarker(marker);
  }
  return marker;
}

bool EntityManager::despawnEntity(const std::string & name)
{
  return entityExists(name) && entities_.erase(name);
}

bool EntityManager::entityExists(const std::string & name)
{
  return entities_.find(name) != std::end(entities_);
}

auto EntityManager::getBoundingBoxDistance(const std::string & from, const std::string & to)
  -> boost::optional<double>
{
  return math::geometry::getPolygonDistance(
    getMapPose(from), getBoundingBox(from), getMapPose(to), getBoundingBox(to));
}

auto EntityManager::getCurrentTime() const noexcept -> double { return current_time_; }

auto EntityManager::getDistanceToCrosswalk(
  const std::string & name, const std::int64_t target_crosswalk_id) -> boost::optional<double>
{
  const auto it = entities_.find(name);
  if (it == entities_.end()) {
    return boost::none;
  }
  if (getWaypoints(name).waypoints.empty()) {
    return boost::none;
  }
  math::geometry::CatmullRomSpline spline(getWaypoints(name).waypoints);
  auto polygon = hdmap_utils_ptr_->getLaneletPolygon(target_crosswalk_id);
  return spline.getCollisionPointIn2D(polygon);
}

auto EntityManager::getDistanceToStopLine(
  const std::string & name, const std::int64_t target_stop_line_id) -> boost::optional<double>
{
  auto it = entities_.find(name);
  if (it == entities_.end()) {
    return boost::none;
  }
  if (getWaypoints(name).waypoints.empty()) {
    return boost::none;
  }
  math::geometry::CatmullRomSpline spline(getWaypoints(name).waypoints);
  auto polygon = hdmap_utils_ptr_->getStopLinePolygon(target_stop_line_id);
  return spline.getCollisionPointIn2D(polygon);
}

auto EntityManager::getEntityNames() const -> const std::vector<std::string>
{
  std::vector<std::string> names{};
  for (const auto & each : entities_) {
    names.push_back(each.first);
  }
  return names;
}

auto EntityManager::getEntityStatus(const std::string & name) const
  -> traffic_simulator_msgs::msg::EntityStatus
{
  traffic_simulator_msgs::msg::EntityStatus status_msg;
  auto it = entities_.find(name);
  if (it == entities_.end()) {
    THROW_SEMANTIC_ERROR("entity : ", name, " does not exist.");
  }
  status_msg = it->second->getStatus();
  status_msg.bounding_box = getBoundingBox(name);
  status_msg.action_status.current_action = getCurrentAction(name);
  switch (getEntityType(name).type) {
    case traffic_simulator_msgs::msg::EntityType::EGO:
      status_msg.type.type = status_msg.type.EGO;
      break;
    case traffic_simulator_msgs::msg::EntityType::VEHICLE:
      status_msg.type.type = status_msg.type.VEHICLE;
      break;
    case traffic_simulator_msgs::msg::EntityType::PEDESTRIAN:
      status_msg.type.type = status_msg.type.PEDESTRIAN;
      break;
  }
  status_msg.time = current_time_;
  status_msg.name = name;
  return status_msg;
}

auto EntityManager::getEntityTypeList() const
  -> const std::unordered_map<std::string, traffic_simulator_msgs::msg::EntityType>
{
  std::unordered_map<std::string, traffic_simulator_msgs::msg::EntityType> ret;
  for (auto it = entities_.begin(); it != entities_.end(); it++) {
    ret.emplace(it->first, it->second->getEntityType());
  }
  return ret;
}

auto EntityManager::getHdmapUtils() -> const std::shared_ptr<hdmap_utils::HdMapUtils> &
{
  return hdmap_utils_ptr_;
}

auto EntityManager::getLongitudinalDistance(
  const LaneletPose & from, const LaneletPose & to, const double max_distance)
  -> boost::optional<double>
{
  auto forward_distance =
    hdmap_utils_ptr_->getLongitudinalDistance(from.lanelet_id, from.s, to.lanelet_id, to.s);

  if (forward_distance and forward_distance.get() > max_distance) {
    forward_distance = boost::none;
  }

  auto backward_distance =
    hdmap_utils_ptr_->getLongitudinalDistance(to.lanelet_id, to.s, from.lanelet_id, from.s);

  if (backward_distance and backward_distance.get() > max_distance) {
    backward_distance = boost::none;
  }
  if (forward_distance && backward_distance) {
    if (forward_distance.get() > backward_distance.get()) {
      return -backward_distance.get();
    } else {
      return forward_distance.get();
    }
  } else if (forward_distance) {
    return forward_distance.get();
  } else if (backward_distance) {
    return -backward_distance.get();
  }
  return boost::none;
}

auto EntityManager::getLongitudinalDistance(
  const LaneletPose & from, const std::string & to, const double max_distance)
  -> boost::optional<double>
{
  if (!laneMatchingSucceed(to)) {
    return boost::none;
  } else {
    return getLongitudinalDistance(from, getEntityStatus(to).lanelet_pose, max_distance);
  }
}

auto EntityManager::getLongitudinalDistance(
  const std::string & from, const LaneletPose & to, const double max_distance)
  -> boost::optional<double>
{
  if (!laneMatchingSucceed(from)) {
    return boost::none;
  } else {
    return getLongitudinalDistance(getEntityStatus(from).lanelet_pose, to, max_distance);
  }
}

auto EntityManager::getLongitudinalDistance(
  const std::string & from, const std::string & to, const double max_distance)
  -> boost::optional<double>
{
  if (laneMatchingSucceed(from) and laneMatchingSucceed(to)) {
    return getLongitudinalDistance(getEntityStatus(from).lanelet_pose, to, max_distance);
  } else {
    return boost::none;
  }
}

/**
 * @brief If the target entity's lanelet pose is valid, return true
 *
 * @param name name of the target entity
 * @return true lane matching is succeed
 * @return false lane matching is failed
 */
bool EntityManager::laneMatchingSucceed(const std::string & name)
{
  return getEntityStatus(name).lanelet_pose_valid;
}

auto EntityManager::getNumberOfEgo() const -> std::size_t
{
  return std::count_if(std::begin(entities_), std::end(entities_), [this](const auto & each) {
    return isEgo(each.first);
  });
}

const std::string EntityManager::getEgoName() const
{
  const auto names = getEntityNames();
  for (const auto & name : names) {
    if (isEgo(name)) {
      return name;
    }
  }
  THROW_SEMANTIC_ERROR(
    "const std::string EntityManager::getEgoName(const std::string & name) function was called, "
    "but ego vehicle does not exist");
}

auto EntityManager::getObstacle(const std::string & name)
  -> boost::optional<traffic_simulator_msgs::msg::Obstacle>
{
  if (!npc_logic_started_) {
    return boost::none;
  }
  return entities_.at(name)->getObstacle();
}

auto EntityManager::getRelativePose(
  const geometry_msgs::msg::Pose & from, const geometry_msgs::msg::Pose & to) const
  -> geometry_msgs::msg::Pose
{
  return math::geometry::getRelativePose(from, to);
}

auto EntityManager::getRelativePose(
  const geometry_msgs::msg::Pose & from, const std::string & to) const -> geometry_msgs::msg::Pose
{
  return getRelativePose(from, getEntityStatus(to).pose);
}

auto EntityManager::getRelativePose(
  const std::string & from, const geometry_msgs::msg::Pose & to) const -> geometry_msgs::msg::Pose
{
  return getRelativePose(getEntityStatus(from).pose, to);
}

auto EntityManager::getRelativePose(const std::string & from, const std::string & to) const
  -> geometry_msgs::msg::Pose
{
  return getRelativePose(getEntityStatus(from).pose, getEntityStatus(to).pose);
}

auto EntityManager::getRelativePose(
  const geometry_msgs::msg::Pose & from, const LaneletPose & to) const -> geometry_msgs::msg::Pose
{
  return getRelativePose(from, toMapPose(to));
}

auto EntityManager::getRelativePose(
  const LaneletPose & from, const geometry_msgs::msg::Pose & to) const -> geometry_msgs::msg::Pose
{
  return getRelativePose(toMapPose(from), to);
}

auto EntityManager::getRelativePose(const std::string & from, const LaneletPose & to) const
  -> geometry_msgs::msg::Pose
{
  return getRelativePose(getEntityStatus(from).pose, to);
}

auto EntityManager::getRelativePose(const LaneletPose & from, const std::string & to) const
  -> geometry_msgs::msg::Pose
{
  return getRelativePose(from, getEntityStatus(to).pose);
}

auto EntityManager::getStepTime() const noexcept -> double { return step_time_; }

auto EntityManager::getWaypoints(const std::string & name)
  -> traffic_simulator_msgs::msg::WaypointsArray
{
  if (!npc_logic_started_) {
    return traffic_simulator_msgs::msg::WaypointsArray();
  }
  return entities_.at(name)->getWaypoints();
}

void EntityManager::getGoalPoses(
  const std::string & name, std::vector<traffic_simulator_msgs::msg::LaneletPose> & goals)
{
  if (!npc_logic_started_) {
    goals = std::vector<traffic_simulator_msgs::msg::LaneletPose>();
  }
  goals = entities_.at(name)->getGoalPoses();
}

void EntityManager::getGoalPoses(
  const std::string & name, std::vector<geometry_msgs::msg::Pose> & goals)
{
  std::vector<traffic_simulator_msgs::msg::LaneletPose> lanelet_poses;
  if (!npc_logic_started_) {
    goals = std::vector<geometry_msgs::msg::Pose>();
  }
  getGoalPoses(name, lanelet_poses);
  for (const auto lanelet_pose : lanelet_poses) {
    goals.push_back(toMapPose(lanelet_pose));
  }
}

bool EntityManager::isEgo(const std::string & name) const
{
  using traffic_simulator_msgs::msg::EntityType;
  return getEntityType(name).type == EntityType::EGO and
         dynamic_cast<EgoEntity const *>(entities_.at(name).get());
}

bool EntityManager::isEgoSpawned() const
{
  for (const auto & name : getEntityNames()) {
    if (isEgo(name)) {
      return true;
    }
  }
  return false;
}

bool EntityManager::isInLanelet(
  const std::string & name, const std::int64_t lanelet_id, const double tolerance)
{
  double l = hdmap_utils_ptr_->getLaneletLength(lanelet_id);
  auto status = getEntityStatus(name);

  if (not status.lanelet_pose_valid) {
    return false;
  }
  if (status.lanelet_pose.lanelet_id == lanelet_id) {
    return true;
  } else {
    auto dist0 = hdmap_utils_ptr_->getLongitudinalDistance(
      lanelet_id, l, status.lanelet_pose.lanelet_id, status.lanelet_pose.s);
    auto dist1 = hdmap_utils_ptr_->getLongitudinalDistance(
      status.lanelet_pose.lanelet_id, status.lanelet_pose.s, lanelet_id, 0);
    if (dist0) {
      if (dist0.get() < tolerance) {
        return true;
      }
    }
    if (dist1) {
      if (dist1.get() < tolerance) {
        return true;
      }
    }
  }
  return false;
}

bool EntityManager::isStopping(const std::string & name) const
{
  return std::fabs(getEntityStatus(name).action_status.twist.linear.x) <
         std::numeric_limits<double>::epsilon();
}

bool EntityManager::reachPosition(
  const std::string & name, const std::string & target_name, const double tolerance) const
{
  return reachPosition(name, getEntityStatus(target_name).pose, tolerance);
}

bool EntityManager::reachPosition(
  const std::string & name, const geometry_msgs::msg::Pose & target_pose,
  const double tolerance) const
{
  const auto pose = getEntityStatus(name).pose;

  const double distance = std::sqrt(
    std::pow(pose.position.x - target_pose.position.x, 2) +
    std::pow(pose.position.y - target_pose.position.y, 2) +
    std::pow(pose.position.z - target_pose.position.z, 2));

  return distance < tolerance;
}

bool EntityManager::reachPosition(
  const std::string & name, const std::int64_t lanelet_id, const double s, const double offset,
  const double tolerance) const
{
  traffic_simulator_msgs::msg::LaneletPose lanelet_pose;
  {
    lanelet_pose.lanelet_id = lanelet_id;
    lanelet_pose.s = s;
    lanelet_pose.offset = offset;
  }

  const auto target_pose = hdmap_utils_ptr_->toMapPose(lanelet_pose);

  return reachPosition(name, target_pose.pose, tolerance);
}

void EntityManager::requestLaneChange(
  const std::string & name, const traffic_simulator::lane_change::Direction & direction)
{
  if (const auto target = hdmap_utils_ptr_->getLaneChangeableLaneletId(
        getEntityStatus(name).lanelet_pose.lanelet_id, direction);
      target) {
    requestLaneChange(name, target.get());
  }
}

bool EntityManager::trafficLightsChanged()
{
  return traffic_light_manager_ptr_->hasAnyLightChanged();
}

void EntityManager::requestSpeedChange(
  const std::string & name, double target_speed, bool continuous)
{
  if (isEgo(name) && getCurrentTime() > 0) {
    THROW_SEMANTIC_ERROR("You cannot set target speed to the ego vehicle after starting scenario.");
  }
  return entities_.at(name)->requestSpeedChange(target_speed, continuous);
}

void EntityManager::requestSpeedChange(
  const std::string & name, const double target_speed, const speed_change::Transition transition,
  const speed_change::Constraint constraint, const bool continuous)
{
  if (isEgo(name) && getCurrentTime() > 0) {
    THROW_SEMANTIC_ERROR("You cannot set target speed to the ego vehicle after starting scenario.");
  }
  return entities_.at(name)->requestSpeedChange(target_speed, transition, constraint, continuous);
}

void EntityManager::requestSpeedChange(
  const std::string & name, const speed_change::RelativeTargetSpeed & target_speed, bool continuous)
{
  if (isEgo(name) && getCurrentTime() > 0) {
    THROW_SEMANTIC_ERROR("You cannot set target speed to the ego vehicle after starting scenario.");
  }
  return entities_.at(name)->requestSpeedChange(target_speed, continuous);
}

void EntityManager::requestSpeedChange(
  const std::string & name, const speed_change::RelativeTargetSpeed & target_speed,
  const speed_change::Transition transition, const speed_change::Constraint constraint,
  const bool continuous)
{
  if (isEgo(name) && getCurrentTime() > 0) {
    THROW_SEMANTIC_ERROR("You cannot set target speed to the ego vehicle after starting scenario.");
  }
  return entities_.at(name)->requestSpeedChange(target_speed, transition, constraint, continuous);
}

bool EntityManager::setEntityStatus(
  const std::string & name, traffic_simulator_msgs::msg::EntityStatus status)
{
  if (isEgo(name) && getCurrentTime() > 0) {
    THROW_SEMANTIC_ERROR(
      "You cannot set entity status to the ego vehicle name ", std::quoted(name),
      " after starting scenario.");
  } else {
    status.name = name;  // FIXME UGLY CODE!!!
    return entities_.at(name)->setStatus(status);
  }
}

void EntityManager::setVerbose(const bool verbose)
{
  configuration.verbose = verbose;
  for (auto & entity : entities_) {
    entity.second->verbose = verbose;
  }
}

auto EntityManager::toMapPose(const traffic_simulator_msgs::msg::LaneletPose & lanelet_pose) const
  -> const geometry_msgs::msg::Pose
{
  return hdmap_utils_ptr_->toMapPose(lanelet_pose).pose;
}

traffic_simulator_msgs::msg::EntityStatus EntityManager::updateNpcLogic(
  const std::string & name,
  const std::unordered_map<std::string, traffic_simulator_msgs::msg::EntityType> & type_list)
{
  if (configuration.verbose) {
    std::cout << "update " << name << " behavior" << std::endl;
  }
  entities_[name]->setEntityTypeList(type_list);
  entities_[name]->onUpdate(current_time_, step_time_);
  return entities_[name]->getStatus();
}

void EntityManager::update(const double current_time, const double step_time)
{
  traffic_simulator::helper::StopWatch<std::chrono::milliseconds> stop_watch_update(
    "EntityManager::update", configuration.verbose);
  step_time_ = step_time;
  current_time_ = current_time;
  setVerbose(configuration.verbose);
  if (getNumberOfEgo() >= 2) {
    THROW_SEMANTIC_ERROR("multi ego simulation does not support yet");
  }
  if (npc_logic_started_) {
    traffic_light_manager_ptr_->update(step_time_);
  }
  auto type_list = getEntityTypeList();
  std::unordered_map<std::string, traffic_simulator_msgs::msg::EntityStatus> all_status;
  const std::vector<std::string> entity_names = getEntityNames();
  for (const auto & entity_name : entity_names) {
    all_status.emplace(entity_name, entities_[entity_name]->getStatus());
  }
  for (auto it = entities_.begin(); it != entities_.end(); it++) {
    it->second->setOtherStatus(all_status);
  }
  all_status.clear();
  for (const auto & entity_name : entity_names) {
    auto status = updateNpcLogic(entity_name, type_list);
    status.bounding_box = getBoundingBox(entity_name);
    all_status.emplace(entity_name, status);
  }
  for (auto it = entities_.begin(); it != entities_.end(); it++) {
    it->second->setOtherStatus(all_status);
  }
  auto entity_type_list = getEntityTypeList();
  traffic_simulator_msgs::msg::EntityStatusWithTrajectoryArray status_array_msg;
  for (const auto & status : all_status) {
    traffic_simulator_msgs::msg::EntityStatusWithTrajectory status_with_traj;
    auto status_msg = status.second;
    status_msg.name = status.first;
    status_msg.bounding_box = getBoundingBox(status.first);
    status_msg.action_status.current_action = getCurrentAction(status.first);
    switch (getEntityType(status.first).type) {
      case traffic_simulator_msgs::msg::EntityType::EGO:
        status_msg.type.type = status_msg.type.EGO;
        break;
      case traffic_simulator_msgs::msg::EntityType::VEHICLE:
        status_msg.type.type = status_msg.type.VEHICLE;
        break;
      case traffic_simulator_msgs::msg::EntityType::PEDESTRIAN:
        status_msg.type.type = status_msg.type.PEDESTRIAN;
        break;
    }
    status_with_traj.waypoint = getWaypoints(status.first);
    std::vector<geometry_msgs::msg::Pose> goals;
    getGoalPoses(status.first, goals);
    for (const auto goal : goals) {
      status_with_traj.goal_pose.push_back(goal);
    }
    const auto obstacle = getObstacle(status.first);
    if (obstacle) {
      status_with_traj.obstacle = obstacle.get();
      status_with_traj.obstacle_find = true;
    } else {
      status_with_traj.obstacle_find = false;
    }
    status_with_traj.status = status_msg;
    status_with_traj.name = status.first;
    status_with_traj.time = current_time + step_time;
    status_array_msg.data.emplace_back(status_with_traj);
  }
  entity_status_array_pub_ptr_->publish(status_array_msg);
  stop_watch_update.stop();
  if (configuration.verbose) {
    stop_watch_update.print();
  }
}

void EntityManager::updateHdmapMarker()
{
  MarkerArray markers;
  const auto stamp = clock_ptr_->now();
  for (const auto & marker_raw : markers_raw_.markers) {
    visualization_msgs::msg::Marker marker = marker_raw;
    marker.header.stamp = stamp;
    markers.markers.emplace_back(marker);
  }
  lanelet_marker_pub_ptr_->publish(markers);
}

void EntityManager::startNpcLogic()
{
  npc_logic_started_ = true;
  for (auto it = entities_.begin(); it != entities_.end(); it++) {
    it->second->startNpcLogic();
  }
}
}  // namespace entity
}  // namespace traffic_simulator

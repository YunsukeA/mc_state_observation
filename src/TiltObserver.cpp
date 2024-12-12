#include <mc_observers/ObserverMacros.h>

#include "mc_state_observation/measurements/measurements.h"
#include <mc_state_observation/TiltObserver.h>
#include <mc_state_observation/gui_helpers.h>

namespace mc_state_observation
{

namespace so = stateObservation;

using OdometryType = measurements::OdometryType;
using LoContactsManager = odometry::LeggedOdometryManager::ContactsManager;

TiltObserver::TiltObserver(const std::string & type, double dt, bool asBackup)
: mc_observers::Observer(type, dt), estimator_(alpha_, beta_, gamma_, dt), odometryManager_(dt)
{
  asBackup_ = asBackup;
}

void TiltObserver::configure(const mc_control::MCController & ctl, const mc_rtc::Configuration & config)
{
  auto contactsConfig = config("contacts");
  auto leggedOdomConfig = config("leggedOdometry");

  robot_ = config("robot", ctl.robot().name());

  imuSensor_ = config("imuSensor", ctl.robot().bodySensor().name());

  config("maxAnchorFrameDiscontinuity", maxAnchorFrameDiscontinuity_);
  config("updateRobot", updateRobot_);
  config("updateSensor", updateSensor_);

  auto filterGainsConfig = config("filterGains");
  filterGainsConfig("initAlpha", alpha_);
  filterGainsConfig("initBeta", beta_);
  filterGainsConfig("initGamma", gamma_);

  filterGainsConfig("finalAlpha", finalAlpha_);
  filterGainsConfig("finalBeta", finalBeta_);
  filterGainsConfig("finalGamma", finalGamma_);

  anchorFrameFunction_ = "KinematicAnchorFrame::" + ctl.robot(robot_).name();
  // if a user-defined anchor frame function is given, we use it instead
  if(config.has("anchorFrameFunction"))
  {
    if(ctl.datastore().has(anchorFrameFunction_))
    {
      anchorFrameFunction_ = config("anchorFrameFunction", name() + "::" + ctl.robot(robot_).name());
    }
  }

  std::string odometryTypeStr = static_cast<std::string>(leggedOdomConfig("odometryType"));
  // we set the odometry type now because it will be necessary for the next check
  setOdometryType(measurements::stringToOdometryType(odometryTypeStr, name()));

  // specific configurations for the use of odometry.
  if(odometryManager_.odometryType_ != measurements::OdometryType::None)
  {
    bool verbose = config("verbose", true);
    bool withYawEstimation = config("withYawEstimation", true);

    // surfaces used for the contact detection. If the desired detection method doesn't use surfaces, we make sure this
    // list is not filled in the configuration file to avoid the use of an undesired method.
    std::vector<std::string> surfacesForContactDetection;
    contactsConfig("surfacesForContactDetection", surfacesForContactDetection);

    std::string contactsDetectionString = static_cast<std::string>(contactsConfig("contactsDetection"));
    LoContactsManager::ContactsDetection contactsDetectionMethod =
        odometryManager_.contactsManager().stringToContactsDetection(contactsDetectionString, name());

    if(surfacesForContactDetection.size() > 0
       && contactsDetectionMethod != LoContactsManager::ContactsDetection::Surfaces)
    {
      mc_rtc::log::error_and_throw<std::runtime_error>("Another type of contacts detection than Surfaces is currently "
                                                       "used, please change it to 'Surfaces' or empty the "
                                                       "surfacesForContactDetection variable");
    }

    odometry::LeggedOdometryManager::Configuration odomConfig(robot_, name(), odometryManager_.odometryType_);
    odomConfig.velocityUpdate(odometry::LeggedOdometryManager::VelocityUpdate::NoUpdate)
        .withYawEstimation(withYawEstimation);
    if(asBackup_) { odomConfig.withModeSwitchInGui(false); }

    if(contactsDetectionMethod == LoContactsManager::ContactsDetection::Surfaces)
    {
      measurements::ContactsManagerSurfacesConfiguration contactsConf(name(), surfacesForContactDetection);
      contactsConf.verbose(verbose);
      if(contactsConfig.has("schmittTriggerLowerPropThreshold")
         && contactsConfig.has("schmittTriggerUpperPropThreshold"))
      {
        double schmittTriggerLowerPropThreshold = contactsConfig("schmittTriggerLowerPropThreshold");
        double schmittTriggerUpperPropThreshold = contactsConfig("schmittTriggerUpperPropThreshold");
        contactsConf.schmittTriggerPropThresholds(schmittTriggerLowerPropThreshold, schmittTriggerUpperPropThreshold);
      }
      odometryManager_.init(ctl, odomConfig, contactsConf);
    }
    if(contactsDetectionMethod == LoContactsManager::ContactsDetection::Sensors)
    {
      std::vector<std::string> forceSensorsToOmit = config("forceSensorsToOmit", std::vector<std::string>());

      measurements::ContactsManagerSensorsConfiguration contactsConf(name());
      contactsConf.verbose(verbose).forceSensorsToOmit(forceSensorsToOmit);
      if(contactsConfig.has("schmittTriggerLowerPropThreshold")
         && contactsConfig.has("schmittTriggerUpperPropThreshold"))
      {
        double schmittTriggerLowerPropThreshold = contactsConfig("schmittTriggerLowerPropThreshold");
        double schmittTriggerUpperPropThreshold = contactsConfig("schmittTriggerUpperPropThreshold");
        contactsConf.schmittTriggerPropThresholds(schmittTriggerLowerPropThreshold, schmittTriggerUpperPropThreshold);
      }
      odometryManager_.init(ctl, odomConfig, contactsConf);
    }
    if(contactsDetectionMethod == LoContactsManager::ContactsDetection::Solver)
    {
      measurements::ContactsManagerSolverConfiguration contactsConf(name());
      contactsConf.verbose(verbose);
      if(contactsConfig.has("schmittTriggerLowerPropThreshold")
         && contactsConfig.has("schmittTriggerUpperPropThreshold"))
      {
        double schmittTriggerLowerPropThreshold = contactsConfig("schmittTriggerLowerPropThreshold");
        double schmittTriggerUpperPropThreshold = contactsConfig("schmittTriggerUpperPropThreshold");
        contactsConf.schmittTriggerPropThresholds(schmittTriggerLowerPropThreshold, schmittTriggerUpperPropThreshold);
      }
      odometryManager_.init(ctl, odomConfig, contactsConf);
    }
  }
}

void TiltObserver::reset(const mc_control::MCController & ctl)
{
  const auto & robot = ctl.robot(robot_);
  const auto & realRobot = ctl.realRobot(robot_);

  my_robots_ = mc_rbdyn::Robots::make();
  my_robots_->robotCopy(robot, robot.name());

  // the updated robot has the same floating base's pose than the control robot, but its encoders are updated. We use it
  // to get more accurate local Kinematics.
  my_robots_->robotCopy(robot, "updatedRobot");
  ctl.gui()->addElement(
      {"Robots"}, mc_rtc::gui::Robot(name(), [this]() -> const mc_rbdyn::Robot & { return my_robots_->robot(); }));

  const auto & imu = robot.bodySensor(imuSensor_);

  poseW_ = realRobot.posW();
  velW_ = realRobot.velW();
  prevPoseW_ = sva::PTransformd::Identity();
  velW_ = sva::MotionVecd::Zero();

  const Eigen::Matrix3d cOri = (imu.X_b_s() * ctl.robot(robot_).bodyPosW(imu.parentBody())).rotation();
  so::Vector3 initX2 = cOri * so::Vector3::UnitZ(); // so::kine::rotationMatrixToRotationVector(cOri.transpose());

  estimator_.initEstimator(so::Vector3::Zero(), initX2, initX2);

  /* Initialization of the variables */
  worldAnchorKine_ctl_ = stateObservation::kine::Kinematics::zeroKinematics(so::kine::Kinematics::Flags::position
                                                                            | so::kine::Kinematics::Flags::linVel);
  worldAnchorKine_ = stateObservation::kine::Kinematics::zeroKinematics(so::kine::Kinematics::Flags::position
                                                                        | so::kine::Kinematics::Flags::linVel);
  imuAnchorKine_ = stateObservation::kine::Kinematics::zeroKinematics(so::kine::Kinematics::Flags::position
                                                                      | so::kine::Kinematics::Flags::linVel);

  anchorFrameJumped_ = false;
  iter_ = 0;
  imuVelC_ = sva::MotionVecd::Zero();
  X_C_IMU_ = sva::PTransformd::Identity();
}

bool TiltObserver::run(const mc_control::MCController & ctl)
{

  const auto & realRobot = ctl.realRobot(robot_);
  auto & logger = (const_cast<mc_control::MCController &>(ctl)).logger();

  if(logger.t() > 1.0)
  {
    alpha_ = finalAlpha_;
    beta_ = finalBeta_;
    gamma_ = finalGamma_;
  }

  if(odometryManager_.odometryType_ == measurements::OdometryType::None)
  {
    const auto & robot = ctl.robot(robot_);

    const auto & realQ = realRobot.mbc().q;
    const auto & realAlpha = realRobot.mbc().alpha;

    std::copy(std::next(realQ.begin()), realQ.end(), std::next(my_robots_->robot("updatedRobot").mbc().q.begin()));
    std::copy(std::next(realAlpha.begin()), realAlpha.end(),
              std::next(my_robots_->robot("updatedRobot").mbc().alpha.begin()));
    my_robots_->robot("updatedRobot").mbc().q[0] = robot.mbc().q[0];
    my_robots_->robot("updatedRobot").mbc().alpha[0] = robot.mbc().alpha[0];

    my_robots_->robot("updatedRobot").forwardKinematics();
    my_robots_->robot("updatedRobot").forwardVelocity();

    runTiltEstimator(ctl, my_robots_->robot("updatedRobot"));
  }
  else
  {
    odometryManager_.initLoop(ctl, logger, odometry::LeggedOdometryManager::RunParameters());
    runTiltEstimator(ctl, odometryManager_.odometryRobot());
  }

  iter_++;

  /* Update of the observed robot */
  my_robots_->robot().mbc().q = realRobot.mbc().q;
  update(my_robots_->robot());

  return true;
}

void TiltObserver::updateAnchorFrameNoOdometry(const mc_control::MCController & ctl,
                                               const mc_rbdyn::Robot & updatedRobot)
{
  const auto & robot = ctl.robot(robot_);
  // const auto & robot = my_robots_->robot("updatedRobot");

  anchorFrameJumped_ = false;

  // We don't use the default anchorFrameFunction because the obtained anchor position is in the shape of steps and we
  // obtain very high velocities when using finite differences

  if(!ctl.datastore().has(anchorFrameFunction_))
  {
    double leftFootRatio = robot.indirectSurfaceForceSensor("LeftFootCenter").force().z()
                           / (robot.indirectSurfaceForceSensor("LeftFootCenter").force().z()
                              + robot.indirectSurfaceForceSensor("RightFootCenter").force().z());

    X_0_C_ctl_ =
        sva::interpolate(robot.surfacePose("RightFootCenter"), robot.surfacePose("LeftFootCenter"), leftFootRatio);
    X_0_C_ = sva::interpolate(updatedRobot.surfacePose("RightFootCenter"), updatedRobot.surfacePose("LeftFootCenter"),
                              leftFootRatio);
  }
  else
  {
    X_0_C_ctl_ = ctl.datastore().call<sva::PTransformd>(anchorFrameFunction_, robot);
    X_0_C_ = ctl.datastore().call<sva::PTransformd>(anchorFrameFunction_, updatedRobot);
  }

  // new pose of the anchor frame in the world.
  newWorldAnchorKine_ctl_ = conversions::kinematics::fromSva(X_0_C_ctl_, so::kine::Kinematics::Flags::pose);
  newWorldAnchorKine_ = conversions::kinematics::fromSva(X_0_C_, so::kine::Kinematics::Flags::pose);

  // the velocities of the anchor frames are computed by finite differences
  worldAnchorKine_ctl_.update(newWorldAnchorKine_ctl_, ctl.timeStep,
                              so::kine::Kinematics::Flags::position | so::kine::Kinematics::Flags::linVel);
  worldAnchorKine_.update(newWorldAnchorKine_, ctl.timeStep,
                          so::kine::Kinematics::Flags::position | so::kine::Kinematics::Flags::linVel);

  // we ignore the initial outlier velocities due to the position jump
  if(iter_ < itersBeforeAnchorsVel_)
  {
    worldAnchorKine_ctl_.linVel().setZero();
    worldAnchorKine_.linVel().setZero();
  }
}

void TiltObserver::updateNecessaryFramesOdom(const mc_control::MCController & ctl, const mc_rbdyn::Robot & updatedRobot)

{
  const auto & imu = ctl.robot(robot_).bodySensor(imuSensor_);

  // pose of the floating base' frame in the world for the odometry robot
  worldFbKine_ = conversions::kinematics::fromSva(updatedRobot.posW(), updatedRobot.velW(), true);

  const sva::PTransformd & imuXbs = imu.X_b_s();
  so::kine::Kinematics parentImuKine =
      conversions::kinematics::fromSva(imuXbs, so::kine::Kinematics::Flags::pose | so::kine::Kinematics::Flags::vel);

  // pose of the IMU's parent body in the world for the odometry robot
  const sva::PTransformd & parentPoseW = updatedRobot.bodyPosW(imu.parentBody());
  // velocity of the IMU's parent body in the world for the odometry robot
  const sva::MotionVecd & v_0_imuParent = updatedRobot.mbc().bodyVelW[updatedRobot.bodyIndexByName(imu.parentBody())];

  // kinematics of the IMU's parent body in the world for the odometry robot
  so::kine::Kinematics worldParentKine = conversions::kinematics::fromSva(parentPoseW, v_0_imuParent, true);

  // pose and velocities of the IMU in the world frame for the odometry robot
  worldImuKine_ = worldParentKine * parentImuKine;

  // pose and velocities of the IMU in the floating base for the odometry robot
  fbImuKine_ = worldFbKine_.getInverse() * worldImuKine_;

  // position and linear velocity of the anchor point in the frame of the IMU.
  imuAnchorKine_ = odometryManager_.getAnchorKineIn(worldImuKine_);

  if(odometryManager_.anchorPointMethodChanged_) { imuAnchorKine_.linVel().setZero(); }
}

void TiltObserver::updateNecessaryFramesNoOdometry(const mc_control::MCController & ctl,
                                                   const mc_rbdyn::Robot & updatedRobot)
{
  updateAnchorFrameNoOdometry(ctl, updatedRobot);

  const auto & imu = ctl.robot(robot_).bodySensor(imuSensor_);

  // pose of the floating base' frame in the world for the robot with the updated encoders
  worldFbKine_ = conversions::kinematics::fromSva(updatedRobot.posW(), updatedRobot.velW(), true);

  // we use the imu object of control robot because the copy of BodySensor objects seems to be incomplete. Anyway we use
  // it only to get information about the parent body, which is the same as in the control robot.
  const sva::PTransformd & imuXbs = imu.X_b_s();
  so::kine::Kinematics parentImuKine =
      conversions::kinematics::fromSva(imuXbs, so::kine::Kinematics::Flags::pose | so::kine::Kinematics::Flags::vel);

  // pose of the IMU's parent body in the world for the robot with the updated encoders
  const sva::PTransformd & updatedParentPoseW = updatedRobot.bodyPosW(imu.parentBody());
  // velocity of the IMU's parent body in the world for the robot with the updated encoders
  const sva::MotionVecd & updated_v_0_imuParent =
      updatedRobot.mbc().bodyVelW[updatedRobot.bodyIndexByName(imu.parentBody())];

  // kinematics of the IMU's parent body in the world for the robot with the updated encoders
  so::kine::Kinematics worldParentKine =
      conversions::kinematics::fromSva(updatedParentPoseW, updated_v_0_imuParent, true);

  // pose and velocities of the IMU in the world frame for the robot with the updated encoders
  worldImuKine_ = worldParentKine * parentImuKine;

  // pose and velocities of the IMU in the floating base for the robot with the updated encoders
  fbImuKine_ = worldFbKine_.getInverse() * worldImuKine_;

  const auto & robot = ctl.robot(robot_);
  const sva::PTransformd & parentPoseW_ctl = robot.bodyPosW(imu.parentBody());
  // Compute velocity of the imu in the control frame
  auto & v_0_imuParent_ctl = robot.mbc().bodyVelW[robot.bodyIndexByName(imu.parentBody())];

  so::kine::Kinematics worldParentKine_ctl = conversions::kinematics::fromSva(parentPoseW_ctl, v_0_imuParent_ctl, true);
  // pose and velocities of the IMU in the world frame
  worldImuKine_ctl_ = worldParentKine_ctl * parentImuKine;

  // new pose of the anchor frame in the IMU frame. The velocity is computed right after because we don't want to use
  // the one given by mc_rtc.
  so::kine::Kinematics newImuAnchorKine = worldImuKine_.getInverse() * worldAnchorKine_;

  // The velocities of the IMU in the world (given by mc_rtc) and the ones of the anchor frame in the world (by finite
  // differences) are not computed the same way, combining them to get the velocity of the anchor frame in the IMU
  // frame therefore leads to errors. So we "unset" the erroneous newly compute velocities to compute them by finite
  // differences from the pose of the anchor frame in the IMU.
  newImuAnchorKine.linVel.set(false);
  newImuAnchorKine.angVel.set(false);

  imuAnchorKine_.update(newImuAnchorKine, ctl.timeStep,
                        so::kine::Kinematics::Flags::position | so::kine::Kinematics::Flags::linVel);

  // we ignore the initial outlier velocity due to the position jump
  // we also reset the velocity of the anchor frame when its computation mode changes.
  if(iter_ < itersBeforeAnchorsVel_) { imuAnchorKine_.linVel().setZero(); }
}

void TiltObserver::runTiltEstimator(const mc_control::MCController & ctl, const mc_rbdyn::Robot & updatedRobot)
{
  estimator_.setAlpha(alpha_);
  estimator_.setBeta(beta_);
  estimator_.setGamma(gamma_);

  if(odometryManager_.odometryType_ != measurements::OdometryType::None)
  {
    updateNecessaryFramesOdom(ctl, updatedRobot);
  }
  else { updateNecessaryFramesNoOdometry(ctl, updatedRobot); }

  const auto & imu = ctl.robot(robot_).bodySensor(imuSensor_);

  auto k = estimator_.getCurrentTime();

  // computation of the local linear velocity of the IMU in the world.

  if(odometryManager_.odometryType_ == measurements::OdometryType::None) // case if we don't use odometry
  {
    yv_ = worldImuKine_ctl_.orientation.toMatrix3().transpose() * worldAnchorKine_ctl_.linVel()
          - (imu.angularVelocity()).cross(imuAnchorKine_.position()) - imuAnchorKine_.linVel();

    estimator_.setMeasurement(yv_, imu.linearAcceleration(), imu.angularVelocity(), k + 1);
  }
  else
  {
    // The anchor frame can be obtained using 2 ways:
    // - 1: contacts are detected and can be used
    // - 2: no contact is detected, the robot is hanging. As we still need an anchor frame for the tilt estimation we
    // arbitrarily use the frame of the IMU. As we cannot perform odometry anymore as there is no contact, we cannot
    // obtain the velocity of the IMU. We will then consider it as zero and consider it as constant with the linear
    // acceleration as zero too.
    // When switching from one mode to another, we consider x1hat = x1 before the estimation to avoid discontinuities.
    if(odometryManager_.maintainedContacts().size() == 0)
    {
      estimator_.setAlpha(30);
      estimator_.setBeta(0);
      //  estimator_.setGamma(0.1);
      yv_.setZero();
    }
    else
    {
      estimator_.setAlpha(alpha_);
      estimator_.setBeta(beta_);
      //  estimator_.setGamma(gamma_);

      yv_ = -imu.angularVelocity().cross(imuAnchorKine_.position()) - imuAnchorKine_.linVel();
    }
  }
  estimator_.setMeasurement(yv_, imu.linearAcceleration(), imu.angularVelocity(), k + 1);

  if(odometryManager_.anchorPointMethodChanged_) { estimator_.resetImuLocVelHat(); }

  // estimation of the state with the complementary filters
  xk_ = estimator_.getEstimatedState(k + 1);

  // retrieving the estimated Tilt
  so::Vector3 tilt = xk_.tail(3);

  // Once we obtain the tilt (which is required by the legged odometry, estimating only the yaw), we update the pose and
  // velocities of the floating base

  if(odometryManager_.odometryType_ != measurements::OdometryType::None)
  {
    // we can update the estimated pose using odometry. The velocity will be updated later using the estimated local
    // linear velocity of the IMU.

    // Orientation of the imu in the world obtained from the estimated tilt and the yaw of the IMU in the odometry
    // robot. This yaw is used by default as it will be replace by the one coming from the contacts.
    estimatedRotationIMU_ = so::kine::mergeTiltWithYawAxisAgnostic(tilt, worldImuKine_.orientation.toMatrix3());

    // Estimated orientation of the floating base in the world (especially the tilt)
    R_0_fb_ = estimatedRotationIMU_ * fbImuKine_.orientation.toMatrix3().transpose();

    odometryManager_.run(ctl, odometry::LeggedOdometryManager::KineParams(poseW_).tiltMeas(R_0_fb_));
  }
  else
  {
    // Orientation of the imu in the world obtained from the estimated tilt and the yaw of the control robot.
    // When using odometry, the tilt will be kept but the yaw will be replaced by the one of the odometry robot.
    estimatedRotationIMU_ = so::kine::mergeTiltWithYawAxisAgnostic(tilt, worldImuKine_ctl_.orientation.toMatrix3());

    // Estimated orientation of the floating base in the world (especially the tilt)
    R_0_fb_ = estimatedRotationIMU_ * fbImuKine_.orientation.toMatrix3().transpose();
  }

  updatePoseAndVel(xk_.head(3), imu.angularVelocity());

  if(asBackup_) { backupFbKinematics_.push_back(correctedWorldFbKine_); }
}

void TiltObserver::updatePoseAndVel(const so::Vector3 & localWorldImuLinVel, const so::Vector3 & localWorldImuAngVel)
{
  // if we use odometry, the pose will already updated in odometryManager_.run(...)
  if(odometryManager_.odometryType_ == measurements::OdometryType::None)
  {
    so::kine::Kinematics fbAnchorKine = worldFbKine_.getInverse() * worldAnchorKine_;

    correctedWorldFbKine_.orientation = R_0_fb_;
    correctedWorldFbKine_.position = worldAnchorKine_ctl_.position() - R_0_fb_ * fbAnchorKine.position();

    poseW_.translation() = correctedWorldFbKine_.position();
    poseW_.rotation() = R_0_fb_.transpose();
  }
  else { correctedWorldFbKine_ = conversions::kinematics::fromSva(poseW_, so::kine::Kinematics::Flags::pose); }

  // we use the newly estimated orientation and local linear velocity of the IMU to obtain the one of the floating base.
  correctedWorldImuKine_ =
      correctedWorldFbKine_
      * fbImuKine_; // corrected pose of the imu in the world. This step is used only to get the
                    // pose of the IMU in the world that is required for the kinematics composition.

  correctedWorldImuKine_.linVel = correctedWorldImuKine_.orientation * localWorldImuLinVel;
  correctedWorldImuKine_.angVel = correctedWorldImuKine_.orientation * localWorldImuAngVel;

  correctedWorldFbKine_ = correctedWorldImuKine_ * fbImuKine_.getInverse();

  velW_.linear() = correctedWorldFbKine_.linVel();
  velW_.angular() = correctedWorldFbKine_.angVel();

  if(odometryManager_.odometryType_ != measurements::OdometryType::None)
  {
    // the velocity of the odometry robot was obtained using finite differences. We give it our estimated velocity which
    // is more accurate.
    odometryManager_.replaceRobotVelocity(velW_);
  }
}

void TiltObserver::update(mc_control::MCController & ctl)
{
  auto & realRobot = ctl.realRobot(robot_);
  if(updateRobot_)
  {
    update(realRobot);
    realRobot.forwardKinematics();
    realRobot.forwardVelocity();
  }

  if(updateSensor_)
  {
    auto & robot = ctl.robot(robot_);

    auto & imu = const_cast<mc_rbdyn::BodySensor &>(robot.bodySensor(imuSensor_));
    auto & rimu = const_cast<mc_rbdyn::BodySensor &>(realRobot.bodySensor(imuSensor_));

    imu.orientation(Eigen::Quaterniond{estimatedRotationIMU_.transpose()});
    rimu.orientation(Eigen::Quaterniond{estimatedRotationIMU_.transpose()});
  }
}

void TiltObserver::update(mc_rbdyn::Robot & robot)
{
  robot.posW(poseW_);
  robot.velW(velW_);
}

const so::kine::Kinematics TiltObserver::backupFb(boost::circular_buffer<so::kine::Kinematics> * koBackupFbKinematics)
{
  // new initial pose of the floating base
  so::kine::Kinematics worldResetKine = *(koBackupFbKinematics->begin());

  // original initial pose of the floating base
  so::kine::Kinematics worldFbInitBackup = backupFbKinematics_.front();

  so::kine::Kinematics fbWorldInitBackup = worldFbInitBackup.getInverse();

  // we apply the transformation from the initial pose to the intermediates pose estimated by the tilt estimator to the
  // new starting pose of the Kinetics Observer
  for(int i = 0; i < koBackupFbKinematics->size(); i++)
  {
    // Intermediary pose of the floating base estimated by the tilt estimator
    so::kine::Kinematics worldFbIntermBackup = backupFbKinematics_.at(i);

    // transformation between the initial and the intermediary pose during the backup interval
    so::kine::Kinematics initInterm = fbWorldInitBackup * worldFbIntermBackup;

    koBackupFbKinematics->at(i) = worldResetKine * initInterm;
  }

  so::Vector3 tiltLocalLinVel = poseW_.rotation() * velW_.linear();
  so::Vector3 tiltLocalAngVel = poseW_.rotation() * velW_.angular();

  // koBackupFbKinematics->back() is the new last pose of the kinetics observer
  koBackupFbKinematics->back().linVel = koBackupFbKinematics->back().orientation.toMatrix3() * tiltLocalLinVel;
  koBackupFbKinematics->back().angVel = koBackupFbKinematics->back().orientation.toMatrix3() * tiltLocalAngVel;

  return koBackupFbKinematics->back();
}

so::kine::Kinematics TiltObserver::applyLastTransformation(const so::kine::Kinematics & previousKine)
{
  so::kine::Kinematics worldFbPreviousBackup = backupFbKinematics_.at(backupFbKinematics_.size() - 2);

  so::kine::Kinematics fbWorldPreviousBackup = worldFbPreviousBackup.getInverse();
  so::kine::Kinematics worldFbFinalBackup = backupFbKinematics_.back();

  so::kine::Kinematics lastTransformation = fbWorldPreviousBackup * worldFbFinalBackup;

  so::kine::Kinematics newKine = previousKine * lastTransformation;

  so::Vector3 tiltLocalLinVel = poseW_.rotation() * velW_.linear();
  so::Vector3 tiltLocalAngVel = poseW_.rotation() * velW_.angular();

  // koBackupFbKinematics->back() is the new last pose of the kinetics observer
  newKine.linVel = newKine.orientation.toMatrix3() * tiltLocalLinVel;
  newKine.angVel = newKine.orientation.toMatrix3() * tiltLocalAngVel;

  return newKine;
}

void TiltObserver::setOdometryType(OdometryType newOdometryType)
{
  odometryManager_.setOdometryType(newOdometryType);
}

void TiltObserver::addToLogger(const mc_control::MCController & ctl,
                               mc_rtc::Logger & logger,
                               const std::string & category)
{
  category_ = category;
  if(odometryManager_.odometryType_ != measurements::OdometryType::None)
  {
    odometryManager_.addToLogger(logger, category + "_leggedOdometryManager");
  }

  logger.addLogEntry(category + "_estimatedState_x1", [this]() -> so::Vector3 { return xk_.head(3); });
  logger.addLogEntry(category + "_estimatedState_x2prime",
                     [this]() -> so::Vector3 { return xk_.segment(3, 3).normalized(); });
  logger.addLogEntry(category + "_estimatedState_x2", [this]() -> so::Vector3 { return xk_.tail(3).normalized(); });
  logger.addLogEntry(category + "_realRobotState_x1",
                     [this, &ctl]() -> so::Vector3
                     {
                       const auto & realRobot = ctl.realRobot(robot_);
                       const auto & rimu = realRobot.bodySensor(imuSensor_);

                       const sva::PTransformd & rimuXbs = rimu.X_b_s();

                       so::kine::Kinematics parentImuKine = conversions::kinematics::fromSva(
                           rimuXbs, so::kine::Kinematics::Flags::pose | so::kine::Kinematics::Flags::vel);

                       const sva::PTransformd & realRobotParentPoseW = realRobot.bodyPosW(rimu.parentBody());

                       // Compute velocity of the imu in the control frame
                       auto & realRobotV_0_imuParent =
                           realRobot.mbc().bodyVelW[realRobot.bodyIndexByName(rimu.parentBody())];

                       so::kine::Kinematics worldParentKine =
                           conversions::kinematics::fromSva(realRobotParentPoseW, realRobotV_0_imuParent, true);

                       so::kine::Kinematics worldImuKine = worldParentKine * parentImuKine;
                       return worldImuKine.orientation.toMatrix3().transpose() * worldImuKine.linVel();
                     });

  logger.addLogEntry(category + "_realRobotState_x2",
                     [this, &ctl]() -> so::Vector3
                     {
                       const auto & realRobot = ctl.realRobot(robot_);
                       const auto & rimu = realRobot.bodySensor(imuSensor_);

                       const sva::PTransformd & rimuXbs = rimu.X_b_s();

                       so::kine::Kinematics parentImuKine = conversions::kinematics::fromSva(
                           rimuXbs, so::kine::Kinematics::Flags::pose | so::kine::Kinematics::Flags::vel);

                       const sva::PTransformd & realRobotParentPoseW = realRobot.bodyPosW(rimu.parentBody());

                       // Compute velocity of the imu in the control frame
                       auto & realRobotV_0_imuParent =
                           realRobot.mbc().bodyVelW[realRobot.bodyIndexByName(rimu.parentBody())];

                       so::kine::Kinematics worldParentKine =
                           conversions::kinematics::fromSva(realRobotParentPoseW, realRobotV_0_imuParent, true);

                       so::kine::Kinematics worldImuKine = worldParentKine * parentImuKine;
                       return (worldImuKine.orientation.toMatrix3().transpose() * so::Vector3::UnitZ()).normalized();
                     });

  logger.addLogEntry(category + "_constants_alpha", [this]() -> double { return estimator_.getAlpha(); });
  logger.addLogEntry(category + "_constants_beta", [this]() -> double { return estimator_.getBeta(); });
  logger.addLogEntry(category + "_constants_gamma", [this]() -> double { return estimator_.getGamma(); });

  logger.addLogEntry(category + "_debug_OdometryType",
                     [this]() -> std::string
                     { return measurements::odometryTypeToSstring(odometryManager_.odometryType_); });

  logger.addLogEntry(category + "_controlAnchorFrame", [this]() -> const sva::PTransformd & { return X_0_C_ctl_; });
  logger.addLogEntry(category + "_updatedRobot",
                     [this]() -> const sva::PTransformd &
                     {
                       const auto & updatedRobot = my_robots_->robot("updatedRobot");
                       return updatedRobot.posW();
                     });

  logger.addLogEntry(category + "_IMU_world_orientation",
                     [this]() { return Eigen::Quaterniond{estimatedRotationIMU_}; });
  logger.addLogEntry(category + "_IMU_AnchorFrame_pose", [this]() -> const sva::PTransformd & { return X_C_IMU_; });
  logger.addLogEntry(category + "_IMU_AnchorFrame_linVel", [this]() -> const sva::MotionVecd & { return imuVelC_; });
  logger.addLogEntry(category + "_AnchorFrame_world_position",
                     [this]() -> so::Vector3 { return worldAnchorKine_ctl_.position(); });
  logger.addLogEntry(category + "_AnchorFrame_world_linVel_global",
                     [this]() -> so::Vector3 { return worldAnchorKine_ctl_.linVel(); });
  logger.addLogEntry(category + "_FloatingBase_world_pose", [this]() -> const sva::PTransformd & { return poseW_; });
  logger.addLogEntry(category + "_FloatingBase_world_vel", [this]() -> const sva::MotionVecd & { return velW_; });
  logger.addLogEntry(category + "_debug_x1", [this]() -> const so::Vector3 & { return yv_; });

  logger.addLogEntry(category + "_debug_realWorldImuLocAngVel",
                     [this, &ctl]() -> so::Vector3
                     {
                       const sva::PTransformd & realImuXbs = ctl.realRobot(robot_).bodySensor(imuSensor_).X_b_s();

                       so::kine::Kinematics realParentImuKine = conversions::kinematics::fromSva(
                           realImuXbs, so::kine::Kinematics::Flags::pose | so::kine::Kinematics::Flags::vel);

                       const sva::PTransformd & realParentPoseW =
                           ctl.realRobot(robot_).bodyPosW(ctl.realRobot(robot_).bodySensor(imuSensor_).parentBody());

                       // Compute velocity of the imu in the control frame
                       auto & real_v_0_imuParent =
                           ctl.realRobot(robot_).mbc().bodyVelW[ctl.realRobot(robot_).bodyIndexByName(
                               ctl.realRobot(robot_).bodySensor(imuSensor_).parentBody())];

                       so::kine::Kinematics realWorldParentKine =
                           conversions::kinematics::fromSva(realParentPoseW, real_v_0_imuParent, true);

                       so::kine::Kinematics realWorldImuKine_ = realWorldParentKine * realParentImuKine;

                       return realWorldImuKine_.orientation.toMatrix3().transpose() * realWorldImuKine_.angVel();
                     });

  logger.addLogEntry(category + "_debug_ctlWorldImuLocAngVel",
                     [this, &ctl]() -> so::Vector3
                     {
                       const sva::PTransformd & imuXbs = ctl.robot(robot_).bodySensor(imuSensor_).X_b_s();

                       so::kine::Kinematics parentImuKine = conversions::kinematics::fromSva(
                           imuXbs, so::kine::Kinematics::Flags::pose | so::kine::Kinematics::Flags::vel);

                       const sva::PTransformd & parentPoseW =
                           ctl.robot(robot_).bodyPosW(ctl.robot(robot_).bodySensor(imuSensor_).parentBody());

                       // Compute velocity of the imu in the control frame
                       auto & v_0_imuParent = ctl.robot(robot_).mbc().bodyVelW[ctl.robot(robot_).bodyIndexByName(
                           ctl.robot(robot_).bodySensor(imuSensor_).parentBody())];

                       so::kine::Kinematics worldParentKine =
                           conversions::kinematics::fromSva(parentPoseW, v_0_imuParent, true);

                       so::kine::Kinematics worldImuKine_ = worldParentKine * parentImuKine;

                       return worldImuKine_.orientation.toMatrix3().transpose() * worldImuKine_.angVel();
                     });

  logger.addLogEntry(category + "_debug_ctlWorldAnchorVelExpressedInImu",
                     [this, &ctl]() -> so::Vector3
                     {
                       const auto & robot = ctl.robot(robot_);

                       const auto & imu = robot.bodySensor(imuSensor_);
                       const sva::PTransformd & imuXbs = imu.X_b_s();

                       return imuXbs.rotation() * worldAnchorKine_ctl_.linVel();
                     });

  logger.addLogEntry(category + "_debug_realX1",
                     [this, &ctl]() -> so::Vector3
                     {
                       const auto & realRobot = ctl.realRobot(robot_);
                       const auto & rimu = realRobot.bodySensor(imuSensor_);

                       const sva::PTransformd & rimuXbs = rimu.X_b_s();

                       so::kine::Kinematics parentImuKine = conversions::kinematics::fromSva(
                           rimuXbs, so::kine::Kinematics::Flags::pose | so::kine::Kinematics::Flags::vel);

                       const sva::PTransformd & parentPoseW = realRobot.bodyPosW(rimu.parentBody());

                       // Compute velocity of the imu in the control frame
                       auto & v_0_imuParent = realRobot.mbc().bodyVelW[realRobot.bodyIndexByName(rimu.parentBody())];

                       so::kine::Kinematics worldParentKine =
                           conversions::kinematics::fromSva(parentPoseW, v_0_imuParent, true);

                       so::kine::Kinematics worldImuKine = worldParentKine * parentImuKine;
                       return worldImuKine.orientation.toMatrix3().transpose() * worldImuKine.linVel();
                     });

  logger.addLogEntry(category + "_debug_realImuVel",
                     [this, &ctl]() -> so::Vector3
                     {
                       const auto & realRobot = ctl.realRobot(robot_);
                       const auto & rimu = realRobot.bodySensor(imuSensor_);

                       const sva::PTransformd & rimuXbs = rimu.X_b_s();

                       so::kine::Kinematics parentImuKine = conversions::kinematics::fromSva(
                           rimuXbs, so::kine::Kinematics::Flags::pose | so::kine::Kinematics::Flags::vel);

                       const sva::PTransformd & parentPoseW = realRobot.bodyPosW(rimu.parentBody());

                       auto & v_0_imuParent = realRobot.mbc().bodyVelW[realRobot.bodyIndexByName(rimu.parentBody())];

                       so::kine::Kinematics worldParentKine =
                           conversions::kinematics::fromSva(parentPoseW, v_0_imuParent, true);

                       so::kine::Kinematics worldImuKine = worldParentKine * parentImuKine;
                       return worldImuKine.linVel();
                     });

  logger.addLogEntry(category + "_debug_realBodyVel",
                     [this, &ctl]() -> so::Vector3
                     {
                       const auto & realRobot = ctl.realRobot(robot_);
                       const auto & rimu = realRobot.bodySensor(imuSensor_);

                       return realRobot.mbc().bodyVelW[realRobot.bodyIndexByName(rimu.parentBody())].linear();
                     });

  logger.addLogEntry(category + "_debug_ctlX1",
                     [this, &ctl]() -> so::Vector3
                     {
                       const auto & robot = ctl.robot(robot_);
                       const auto & imu = robot.bodySensor(imuSensor_);

                       const sva::PTransformd & imuXbs = imu.X_b_s();

                       so::kine::Kinematics parentImuKine = conversions::kinematics::fromSva(
                           imuXbs, so::kine::Kinematics::Flags::pose | so::kine::Kinematics::Flags::vel);

                       const sva::PTransformd & parentPoseW = robot.bodyPosW(imu.parentBody());

                       // Compute velocity of the imu in the control frame
                       auto & v_0_imuParent = robot.mbc().bodyVelW[robot.bodyIndexByName(imu.parentBody())];

                       so::kine::Kinematics worldParentKine =
                           conversions::kinematics::fromSva(parentPoseW, v_0_imuParent, true);

                       so::kine::Kinematics worldImuKine = worldParentKine * parentImuKine;
                       return worldImuKine.orientation.toMatrix3().transpose() * worldImuKine.linVel();
                     });

  logger.addLogEntry(category + "_debug_ctlImuVel",
                     [this, &ctl]() -> so::Vector3
                     {
                       const auto & robot = ctl.robot(robot_);
                       const auto & imu = robot.bodySensor(imuSensor_);

                       const sva::PTransformd & imuXbs = imu.X_b_s();

                       so::kine::Kinematics parentImuKine = conversions::kinematics::fromSva(
                           imuXbs, so::kine::Kinematics::Flags::pose | so::kine::Kinematics::Flags::vel);

                       const sva::PTransformd & parentPoseW = robot.bodyPosW(imu.parentBody());

                       auto & v_0_imuParent = robot.mbc().bodyVelW[robot.bodyIndexByName(imu.parentBody())];

                       so::kine::Kinematics worldParentKine =
                           conversions::kinematics::fromSva(parentPoseW, v_0_imuParent, true);

                       so::kine::Kinematics worldImuKine = worldParentKine * parentImuKine;
                       return worldImuKine.linVel();
                     });

  logger.addLogEntry(category + "_debug_contactDetected",
                     [this]() -> std::string
                     { return odometryManager_.contactsManager().contactsDetected() ? "contacts" : "no contacts"; });

  logger.addLogEntry(category + "_debug_ctlBodyVel",
                     [this, &ctl]() -> so::Vector3
                     {
                       const auto & robot = ctl.robot(robot_);
                       const auto & imu = robot.bodySensor(imuSensor_);

                       return robot.mbc().bodyVelW[robot.bodyIndexByName(imu.parentBody())].linear();
                     });

  conversions::kinematics::addToLogger(logger, worldAnchorKine_ctl_, category + "_debug_worldAnchorKine_ctl");
  conversions::kinematics::addToLogger(logger, worldImuKine_, category + "_debug_worldImuKine");
  conversions::kinematics::addToLogger(logger, worldImuKine_ctl_, category + "_debug_worldImuKine_ctl");
  conversions::kinematics::addToLogger(logger, imuAnchorKine_, category + "_debug_imuAnchorKine_");

  conversions::kinematics::addToLogger(logger, worldFbKine_, category + "_debug_worldFbKine_");
  conversions::kinematics::addToLogger(logger, correctedWorldImuKine_, category + "_debug_correctedWorldImuKine_");
}

void TiltObserver::removeFromLogger(mc_rtc::Logger & logger, const std::string & category)
{
  logger.removeLogEntry(category + "_imuVelC");
  logger.removeLogEntry(category + "_imuPoseC");
  logger.removeLogEntry(category + "_imuEstRotW");
  logger.removeLogEntry(category + "_controlAnchorFrame");
}

void TiltObserver::addToGUI(const mc_control::MCController &,
                            mc_rtc::gui::StateBuilder & gui,
                            const std::vector<std::string> & category)
{
  using namespace mc_state_observation::gui;
  gui.addElement(category, make_input_element("alpha", alpha_), make_input_element("beta", beta_),
                 make_input_element("gamma", gamma_));

  // we allow to change the odometry type if the Tilt Observer is not used as a backup of the Kinetics Observer.
  // Otherwise the type can be changed by changing the one of the Kinetics Observer.
  if(asBackup_ != true && odometryManager_.odometryType_ != measurements::OdometryType::None)
  {
    std::vector<std::string> odomCategory = category;
    odomCategory.insert(odomCategory.end(), {"Odometry"});
  }
}

} // namespace mc_state_observation
EXPORT_OBSERVER_MODULE("Tilt", mc_state_observation::TiltObserver)

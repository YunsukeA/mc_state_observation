#pragma once

#include <mc_observers/Observer.h>
#include <state-observation/observer/tilt-estimator.hpp>
#include <state-observation/tools/rigid-body-kinematics.hpp>

namespace mc_state_observation
{

struct TiltObserver : public mc_observers::Observer
{
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
public:
  TiltObserver(const std::string & type, double dt);

  void configure(const mc_control::MCController & ctl, const mc_rtc::Configuration &) override;

  void reset(const mc_control::MCController & ctl) override;

  bool run(const mc_control::MCController & ctl) override;

  void updatePoseAndVel(const mc_control::MCController & ctl,
                        const stateObservation::Vector3 & localWorldImuLinVel,
                        const stateObservation::Vector3 & localWorldImuAngVel);

  void update(mc_control::MCController & ctl) override;

protected:
  /*! \brief update the robot pose in the world only for visualization purpose
   *
   * @param robot Robot to update
   */

  void update(mc_rbdyn::Robot & robot, const mc_control::MCController & ctl);
  /*! \brief Add observer from logger
   *
   * @param category Category in which to log this observer
   */
  void addToLogger(const mc_control::MCController &, mc_rtc::Logger &, const std::string & category) override;

  /*! \brief Remove observer from logger
   *
   * @param category Category in which this observer entries are logged
   */
  void removeFromLogger(mc_rtc::Logger &, const std::string & category) override;

  /*! \brief Add observer information the GUI.
   *
   * @param category Category in which to add this observer
   */
  void addToGUI(const mc_control::MCController &,
                mc_rtc::gui::StateBuilder &,
                const std::vector<std::string> & /* category */) override;

protected:
  std::string robot_;
  bool updateRobot_ = false;
  std::string updateRobotName_;
  std::string imuSensor_;
  bool updateSensor_ = true;
  std::string updateSensorName_;
  /*!
   * parameter related to the convergence of the linear velocity
   * of the IMU expressed in the control frame
   */
  double alpha_ = 200;
  ///  parameter related to the fast convergence of the tilt
  double beta_ = 5;
  /// parameter related to the orthogonality
  double gamma_ = 15;
  std::string anchorFrameFunction_;
  stateObservation::TiltEstimator estimator_;

  // values used for computation
  stateObservation::kine::Kinematics fbImuKine_;
  sva::MotionVecd imuVelC_ = sva::MotionVecd::Zero();
  sva::PTransformd X_C_IMU_ = sva::PTransformd::Identity();
  sva::PTransformd X_0_C_ = sva::PTransformd::Identity(); // control anchor frame
  sva::PTransformd X_0_C_real_ = sva::PTransformd::Identity(); // anchor frame updated by the other observers
  sva::PTransformd X_0_C_real_previous_ = sva::PTransformd::Identity(); // previous real anchor frame

  sva::PTransformd newWorldAnchorPose = sva::PTransformd::Identity(); // control anchor frame

  stateObservation::kine::Kinematics::Flags::Byte flagPoseVels_ =
      stateObservation::kine::Kinematics::Flags::position | stateObservation::kine::Kinematics::Flags::orientation
      | stateObservation::kine::Kinematics::Flags::linVel | stateObservation::kine::Kinematics::Flags::angVel;

  stateObservation::kine::Kinematics worldAnchorKine =
      stateObservation::kine::Kinematics::zeroKinematics(flagPoseVels_);
  stateObservation::kine::Kinematics worldAnchorKine_ =
      stateObservation::kine::Kinematics::zeroKinematics(flagPoseVels_);
  stateObservation::kine::Kinematics realWorldAnchorKine_ =
      stateObservation::kine::Kinematics::zeroKinematics(flagPoseVels_);
  stateObservation::kine::Kinematics worldFbKine_;
  stateObservation::kine::Kinematics realWorldFbKine_;

  stateObservation::Vector3 estimatedWorldImuLocalLinVel_;
  stateObservation::Vector3 virtualMeasureWorldImuLocalLinVel_;
  stateObservation::Vector3 realRobotWorldImuLocalLinVel_;

  // result
  // The observed tilt of the sensor
  Eigen::Matrix3d estimatedRotationIMU_;

  // // The RPY angles for the estimated orientation of the waist ({B})
  // TimedOrientation3D m_rpyBEst;
  // InPort<TimedOrientation3D> m_rpyBEstIn;

  // // The estimated position of the waist ({B})
  // TimedPoint3D m_pBEst;
  // InPort<TimedPoint3D> m_pBEstIn;

  // // The RPY angles for the estimated orientation of the control frame ({F})
  // TimedOrientation3D m_rpyFEst;
  // InPort<TimedOrientation3D> m_rpyFEstIn;

  // // The estimated position of the control frame ({F})
  // TimedPoint3D m_pFEst;
  // InPort<TimedPoint3D> m_pFEstIn;

  // // The observed tilt of the sensor ({S})
  // TimedOrientation3D m_rpyS;
  // OutPort<TimedOrientation3D> m_rpySOut;

  stateObservation::Vector3 m_pF_prev;
  /// Instance of the Tilt Estimator
  stateObservation::Vector xk_;

  bool firstSample_;

private:
  std::shared_ptr<mc_rbdyn::Robots> my_robots_;

  Eigen::Matrix3d R_0_fb_; // estimated orientation of the floating base in the world frame
  sva::PTransformd poseW_ = sva::PTransformd::Identity(); ///< Estimated pose of the floating-base in world frame */
  sva::PTransformd prevPoseW_ = sva::PTransformd::Identity(); ///< Estimated pose of the floating-base in world frame */
  sva::MotionVecd velW_ = sva::MotionVecd::Zero();

  sva::PTransformd poseForDisplay;
  double maxAnchorFrameDiscontinuity_ =
      0.01; ///< Threshold (norm) above wich the anchor frame is considered to have had a discontinuity
  bool anchorFrameJumped_ = false; /** Detects whether the anchor frame had a discontinuity */
  bool firstIter_ = true;

  stateObservation::kine::Kinematics oldRealRobotWorldAnchorKine_;
  stateObservation::kine::Kinematics oldRealRobotBasedAnchorKine_;
  stateObservation::Vector3 x1_;
};

} // namespace mc_state_observation

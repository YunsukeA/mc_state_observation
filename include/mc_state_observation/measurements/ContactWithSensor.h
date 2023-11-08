#pragma once

#include <mc_state_observation/measurements/Contact.h>

namespace mc_state_observation::measurements
{
/**
 * Object making easier the handling of contacts associated to a sensor within the observers.

 * If the contact is detected using a thresholding on the contact force, the contact force cannot be obtained and the
 * name of the contact will be the one of the force sensor. Otherwise the name of the contact surface is used, allowing
 * the creation of contacts associated to a same sensor but a different surface.
 **/
struct ContactWithSensor : public Contact
{

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
public:
  ContactWithSensor() {}
  // constructor if the contact is not associated to a surface
  ContactWithSensor(int id, std::string forceSensorName)
  {
    id_ = id;
    name_ = forceSensorName;
    forceSensorName_ = forceSensorName;

    resetContact();
  }

  // constructor if the contact is associated to a surface
  ContactWithSensor(int id,
                    const std::string & forceSensorName,
                    const std::string & surfaceName,
                    bool sensorAttachedToSurface)
  {
    id_ = id;
    name_ = surfaceName;
    resetContact();

    surface_ = surfaceName;
    forceSensorName_ = forceSensorName;
    sensorAttachedToSurface_ = sensorAttachedToSurface;
  }
  ~ContactWithSensor() {}
  inline void resetContact()
  {
    wasAlreadySet_ = false;
    isSet_ = false;
    sensorWasEnabled_ = false;

    // also filtered force? see when this feature will be corrected
  }

  std::string & forceSensorName() { return forceSensorName_; }

public:
  Eigen::Matrix<double, 6, 1> wrenchInCentroid_ = Eigen::Matrix<double, 6, 1>::Zero(); // for logs
  double forceNorm_ = 0.0;
  // the sensor measurement have to be used by the observer
  bool sensorEnabled_ = true;
  // allows to know if the contact's measurements have to be added during the update.
  bool sensorWasEnabled_ = false;

  // measured contact wrench, expressed in the frame of the contact. Not automatically computed so must be explicitely
  // computed and called.
  Eigen::Matrix<double, 6, 1> contactWrenchVector_;

  // indicates if the sensor is directly attached to a surface (true) or not (false). Default is true because in the
  // case of detection of contacts by thresholding the measured force (@contactsDetection_ = fromThreshold), we cannot
  // know precisely the surface of contact, so we will consider that the kinematics of the contact surface are the
  // ones of the sensor
  bool sensorAttachedToSurface_ = true;
  // surface of contact
protected:
  std::string forceSensorName_;

  /* Force filtering for the contact detection */ // ! Not working yet!
  // Eigen::Vector3d filteredForce = Eigen::Vector3d::Zero();
  // double lambda = 0.0;
};
} // namespace mc_state_observation::measurements
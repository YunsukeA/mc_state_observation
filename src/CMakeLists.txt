set(mc_state_observation_SRC conversions/kinematics.cpp
  odometry/LeggedOdometryManager.cpp)
set(mc_state_observation_HDR
  ${CMAKE_SOURCE_DIR}/include/mc_state_observation/conversions/kinematics.h
  ${CMAKE_SOURCE_DIR}/include/mc_state_observation/odometry/LeggedOdometryManager.h
)
add_library(mc_state_observation SHARED ${mc_state_observation_SRC}
  ${mc_state_observation_HDR})

target_link_libraries(mc_state_observation PUBLIC mc_rtc::mc_control)
install(
  TARGETS mc_state_observation
  EXPORT "${TARGETS_EXPORT_NAME}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_library(
  mc_state_observation SHARED
  observersTools/kinematicsTools.cpp observersTools/measurementsTools.cpp
  observersTools/leggedOdometryTools.cpp)
target_link_libraries(
  mc_state_observation PUBLIC SpaceVecAlg::SpaceVecAlg
  state-observation::state-observation)
install(
  TARGETS mc_state_observation
  EXPORT "${TARGETS_EXPORT_NAME}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

option(BUILD_MCKINETICS_ONLY "" OFF)

macro(add_simple_observer observer_name)
  add_observer(${observer_name} "${observer_name}.cpp"
    "${CMAKE_SOURCE_DIR}/include/${PROJECT_NAME}/${observer_name}.h")
  # mc_observers doesn't actually depend on mc_control, but the observer's
  # implementation does
  target_link_libraries(${observer_name} PUBLIC mc_rtc::mc_control)
  # Force installation in non-debug folder
  if("${CMAKE_BUILD_TYPE}" STREQUAL Debug)
    install(TARGETS ${observer_name} DESTINATION ${MC_RTC_LIBDIR}/mc_observers)
  endif()
endmacro()

macro(add_so_observer observer_name)
  add_simple_observer(${observer_name})
  target_link_libraries(
    ${observer_name} PUBLIC state-observation::state-observation
    mc_state_observation)
endmacro()

macro(add_kineticsobserver)
  add_observer(
    MCKineticsObserver "MCKineticsObserver.cpp"
    "${CMAKE_SOURCE_DIR}/include/${PROJECT_NAME}/MCKineticsObserver.h")
  # mc_observers doesn't actually depend on mc_control, but the observer's
  # implementation does
  target_link_libraries(
    MCKineticsObserver
    PUBLIC mc_rtc::mc_control state-observation::state-observation
    mc_state_observation TiltObserver)
endmacro()

macro(add_observer_with_filter observer_name)
  add_observer(
    ${observer_name}
    "${observer_name}.cpp;filtering.cpp"
    "${CMAKE_SOURCE_DIR}/include/${PROJECT_NAME}/${observer_name}.h;${CMAKE_SOURCE_DIR}/include/${PROJECT_NAME}/filtering.h"
  )
  target_link_libraries(${observer_name} PUBLIC mc_rtc::mc_control)
endmacro()

add_so_observer(AttitudeObserver)
add_so_observer(NaiveOdometry)
add_so_observer(TiltObserver)
add_so_observer(MCWaiko)
add_so_observer(MCVanyte)
add_so_observer(MocapVisualizer)

add_kineticsobserver()

if(WITH_ROS_OBSERVERS AND NOT BUILD_MCKINETICS_ONLY)
  add_simple_observer(MocapObserver)
  add_simple_observer(MocapObserverROS)
  target_link_libraries(MocapObserverROS PUBLIC MocapObserver)
  target_link_libraries(MocapObserverROS PUBLIC mc_state_observation::ROS
    mc_rtc::mc_rtc_ros)
  set_target_properties(
    MocapObserverROS PROPERTIES INSTALL_RPATH
    ${MC_OBSERVERS_RUNTIME_INSTALL_PREFIX})

  find_package(gram_savitzky_golay REQUIRED)
  add_observer_with_filter(SLAMObserver)
  target_link_libraries(
    SLAMObserver
    PUBLIC mc_rtc::mc_control mc_state_observation::ROS mc_rtc::mc_rtc_ros
    gram_savitzky_golay::gram_savitzky_golay)
  set_target_properties(
    SLAMObserver PROPERTIES INSTALL_RPATH
    ${MC_OBSERVERS_RUNTIME_INSTALL_PREFIX})

  add_simple_observer(ObjectObserver)
  target_link_libraries(
    ObjectObserver PUBLIC mc_rtc::mc_control mc_state_observation::ROS
    mc_rtc::mc_rtc_ros)
  set_target_properties(
    ObjectObserver PROPERTIES INSTALL_RPATH
    ${MC_OBSERVERS_RUNTIME_INSTALL_PREFIX})
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/${PROJECT_NAME}
  DESTINATION include)

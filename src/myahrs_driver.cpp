//------------------------------------------------------------------------------
// Copyright (c) 2015, Yoonseok Pyo
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.

// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.

// * Neither the name of myahrs_driver nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//------------------------------------------------------------------------------
#include <myahrs_driver/myahrs_plus.hpp>

#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/MagneticField.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>

//------------------------------------------------------------------------------
using namespace WithRobot;

//------------------------------------------------------------------------------
void handle_error(const char* error_msg)
{
  fprintf(stderr, "ERROR: %s\n", error_msg);
  exit(1);
}

//------------------------------------------------------------------------------
class MyAhrsDriverForROS : public iMyAhrsPlus
{
private:
  ros::NodeHandle nh_;
  ros::NodeHandle nh_priv_;

  std::string frame_id_;
  bool autocalibrate_;
  double linear_acceleration_stddev_;
  double angular_velocity_stddev_;
  double magnetic_field_stddev_;
  double orientation_stddev_;

  void OnSensorData(int sensor_id, SensorData data)
  {
    LockGuard _l(lock_);
    sensor_data_ = data;
    publish_topic(sensor_id);
  }

  void OnAttributeChange(int sensor_id, std::string attribute_name, std::string value)
  {
    printf("OnAttributeChange(id %d, %s, %s)\n", sensor_id, attribute_name.c_str(), value.c_str());
  }

public:
  ros::Publisher imu_data_pub_;
  ros::Publisher imu_mag_pub_;
  Platform::Mutex lock_;
  SensorData sensor_data_;
  tf::TransformBroadcaster broadcaster_;

  std::string port_;
  int baud_rate_;

  MyAhrsDriverForROS(std::string port="", unsigned int baudrate=115200)
  : iMyAhrsPlus(port, baudrate),
    nh_priv_("~")
  {
    nh_priv_.param("port", port_, std::string("/dev/ttyACM0"));
    nh_priv_.param("baud", baud_rate_, 115200);
    nh_priv_.param("frame_id", frame_id_, std::string("imu_link"));
    nh_priv_.param("autocalibrate", autocalibrate_, false);
    nh_priv_.param("linear_acceleration_stddev", linear_acceleration_stddev_, 0.0);
    nh_priv_.param("angular_velocity_stddev", angular_velocity_stddev_, 0.0);
    nh_priv_.param("magnetic_field_stddev", magnetic_field_stddev_, 0.0);
    nh_priv_.param("orientation_stddev", orientation_stddev_, 0.0);

    imu_data_pub_ = nh_.advertise<sensor_msgs::Imu>("imu/data", 1);
    imu_mag_pub_  = nh_.advertise<sensor_msgs::MagneticField>("imu/mag", 1);
  }

  ~MyAhrsDriverForROS()
  {}

  bool initialize()
  {
    bool ok = false;

    do
    {
      if(start() == false) break;
      //Euler angle(x, y, z axis)
      //IMU(linear_acceleration, angular_velocity, magnetic_field)
      if(cmd_binary_data_format("EULER, IMU") == false) break;
      // 100Hz
      if(cmd_divider("1") == false) break;
      // Binary and Continue mode
      if(cmd_mode("BC") == false) break;
      ok = true;
    } while(0);

    return ok;
  }

  inline void get_data(SensorData& data)
  {
    LockGuard _l(lock_);
    data = sensor_data_;
  }

  inline SensorData get_data()
  {
    LockGuard _l(lock_);
    return sensor_data_;
  }

  void publish_topic(int sensor_id)
  {
    printf(".");
    static double convertor_g2a  = 9.80665;    // for linear_acceleration (g to m/s^2)
    static double convertor_d2r  = M_PI/180.0; // for angular_velocity (degree to radian)
    static double convertor_r2d  = 180.0/M_PI; // for easy understanding (radian to degree)
    static double convertor_ut2t = 1/1000000;  // for magnetic_field (uT to Tesla)
    static double convertor_c    = 1;          // for temperature (celsius)

    double roll, pitch, yaw;
    // original sensor data used the degree unit, convert to radian (see ROS REP103)
    // we used the ROS's axis orientation like x forward, y left and z up
    // so changed the y and z aixs of myAHRS+ board
    roll  =  sensor_data_.euler_angle.roll*convertor_d2r;
    pitch = -sensor_data_.euler_angle.pitch*convertor_d2r;
    yaw   = -sensor_data_.euler_angle.yaw*convertor_d2r;

    tf::Quaternion orientation = tf::createQuaternionFromRPY(roll, pitch, yaw);

    ImuData<float>& imu = sensor_data_.imu;

//    printf("sensor_id %d, Quaternion(xyzw)=%.4f,%.4f,%.4f,%.4f, Angle(rpy)=%.1f, %.1f, %.1f, Accel(xyz)=%.4f,%.4f,%.4f, Gyro(xyz)=%.4f,%.4f,%.4f, Magnet(xyz)=%.2f,%.2f,%.2f\n",
//      sensor_id,
//      q.x, q.y, q.z, q.w,
//      e.roll, e.pitch, e.yaw,
//      imu.ax, imu.ay, imu.az,
//      imu.gx, imu.gy, imu.gz,
//      imu.mx, imu.my, imu.mz);

    ros::Time now = ros::Time::now();

    sensor_msgs::Imu imu_msg;
    sensor_msgs::MagneticField magnetic_msg;

    imu_msg.header.stamp = now;
    magnetic_msg.header.stamp = now;

    imu_msg.header.frame_id = "imu_base";
    magnetic_msg.header.frame_id = "imu_base";

    // orientation
    imu_msg.orientation.x = orientation[0];
    imu_msg.orientation.y = orientation[1];
    imu_msg.orientation.z = orientation[2];
    imu_msg.orientation.w = orientation[3];

    // original data used the g unit, convert to m/s^2
    imu_msg.linear_acceleration.x = imu.ax * convertor_g2a;
    imu_msg.linear_acceleration.y = imu.ay * convertor_g2a;
    imu_msg.linear_acceleration.z = imu.az * convertor_g2a;

    // original data used the degree/s unit, convert to radian/s
    imu_msg.angular_velocity.x = imu.gx * convertor_d2r;
    imu_msg.angular_velocity.y = imu.gy * convertor_d2r;
    imu_msg.angular_velocity.z = imu.gz * convertor_d2r;

    // original data used the uTesla unit, convert to Tesla
    magnetic_msg.magnetic_field.x = imu.mx * convertor_ut2t;
    magnetic_msg.magnetic_field.y = imu.mx * convertor_ut2t;
    magnetic_msg.magnetic_field.z = imu.mx * convertor_ut2t;

    imu_data_pub_.publish(imu_msg);
    imu_mag_pub_.publish(magnetic_msg);

    broadcaster_.sendTransform(tf::StampedTransform(tf::Transform(tf::createQuaternionFromRPY(roll, pitch, yaw),
                                                                 tf::Vector3(0.0, 0.0, 0.1)),
                                                   ros::Time::now(), "imu_base", "imu"));
  }
};


//------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
  ros::init(argc, argv, "myahrs_driver");

  MyAhrsDriverForROS sensor("/dev/ttyACM0", 115200);

  if(sensor.initialize() == false)
  {
    handle_error("initialize() returns false");
  }

  ros::Rate loop_rate(100); // 0.01sec

  while (ros::ok())
  {
    ros::spinOnce();
    loop_rate.sleep();
  }

  return 0;
}

//------------------------------------------------------------------------------

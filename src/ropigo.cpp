#include <ros/ros.h>
#include <std_msgs/Int16.h>
#include <std_msgs/Float64.h>
#include <geometry_msgs/Twist.h>
#include <ropigo/SimpleWrite.h>
#include <sensor_msgs/BatteryState.h>
#include <sensor_msgs/JointState.h>
#include <sensor_msgs/Range.h>
#include <angles/angles.h>

extern "C" {
#include <gopigo.h>
}

#include <cmath>

static ros::Publisher servo_state_pub;

void cmdCallback(const geometry_msgs::Twist::ConstPtr& msg) {
    ROS_DEBUG("-------");

    const double t = msg->linear.x;
    const double r = msg->angular.z;

    const double mag = sqrt(pow(t,2)+pow(r,2));

    ROS_DEBUG("translation: %f, rotation: %f, magnitude: %f", t, r, mag);

    // ignore small values
    if(mag <= 0.1) {
        ROS_DEBUG("Not moving");
        if( motor1(0,0)!=1 || motor2(0,0)!=1 ) {
            ROS_WARN("Could not stop!");
        }
        unsigned char speed[2];
        read_motor_speed(speed);
        ROS_DEBUG("speed %i, %i", speed[0], speed[1]);
        return;
    }

    // left and right motor values
    double m1 = 0;
    double m2 = 0;

    m1 = t/mag * std::abs(t);
    m2 = t/mag * std::abs(t);

    m1 += r/mag * std::abs(r);
    m2 += -r/mag * std::abs(r);

    ROS_DEBUG("m1, m2: %f, %f", m1, m2);

    // restrict range [-1...1]
    m1 = m1/std::abs(m1) * std::min(std::abs(m1), 1.0);
    m2 = m2/std::abs(m2) * std::min(std::abs(m2), 1.0);

    ROS_DEBUG("m1, m2: %f(%i), %f(%i)", m1, m1>=0, m2, m2>=0);

    ROS_DEBUG("writing values (motor1, motor2): (%i)%i, (%i)%i", std::max(0, int(m1/std::abs(m1))), int(m1*255), std::max(0, int(m2/std::abs(m2))), int(m2*255));

    if( motor1(m1>=0, std::abs(int(m1*255))) != 1 ) {
        ROS_WARN("Error when writing to motor 1");
    }

    if( motor2(m2>=0, std::abs(int(m2*255))) != 1) {
        ROS_WARN("Error when writing to motor 2");
    }

    unsigned char speed[2];
    read_motor_speed(speed);
    ROS_DEBUG("speed %i, %i", speed[0], speed[1]);
}

void servoCallback(const std_msgs::Float64 &servo_angle) {
    // set the servo angle
    servo(int(angles::to_degrees(servo_angle.data)));

    // publish the same servo angle
    sensor_msgs::JointState servo_joint;
    servo_joint.name.push_back("servo");
    servo_joint.position.push_back(servo_angle.data);
    servo_state_pub.publish(servo_joint);
}

bool enc_enable(ropigo::SimpleWrite::Request &/*req*/, ropigo::SimpleWrite::Response &res) {
    int ret = enable_encoders();
    res.status = int8_t(ret);
    if(ret!=1) ROS_WARN("Error enabling encoders!");
    return ret==1;
}

bool enc_disable(ropigo::SimpleWrite::Request &/*req*/, ropigo::SimpleWrite::Response &res) {
    int ret = disable_encoders();
    res.status = int8_t(ret);
    if(ret!=1) ROS_WARN("Error disabling encoders!");
    return ret==1;
}

bool led_enable_left(ropigo::SimpleWrite::Request &/*req*/, ropigo::SimpleWrite::Response &res) {
    int ret = led_on(1);
    res.status = int8_t(ret);
    if(ret!=1) ROS_WARN("Error enabling LED left!");
    else ROS_INFO("Left LED enabled");
    return ret==1;
}

bool led_enable_right(ropigo::SimpleWrite::Request &/*req*/, ropigo::SimpleWrite::Response &res) {
    int ret = led_on(0);
    res.status = int8_t(ret);
    if(ret!=1) ROS_WARN("Error enabling LED right!");
    else ROS_INFO("Right LED enabled");
    return ret==1;
}

bool led_disable_left(ropigo::SimpleWrite::Request &/*req*/, ropigo::SimpleWrite::Response &res) {
    int ret = led_off(1);
    res.status = int8_t(ret);
    if(ret!=1) ROS_WARN("Error disabling LED left!");
    else ROS_INFO("Left LED disabled");
    return ret==1;
}

bool led_disable_right(ropigo::SimpleWrite::Request &/*req*/, ropigo::SimpleWrite::Response &res) {
    int ret = led_off(0);
    res.status = int8_t(ret);
    if(ret!=1) ROS_WARN("Error disabling LED right!");
    else ROS_INFO("Right LED disabled");
    return ret==1;
}

int main(int argc, char **argv) {

    init();

    ROS_INFO("GoPiGo Firmware Version: %d",fw_ver());
    ROS_INFO("GoPiGo Board Version: %d",brd_rev());

    led_on(0);
    usleep(500000);
    led_on(1);
    usleep(500000);
    led_off(0);
    usleep(500000);
    led_off(1);

    ros::init(argc, argv, "ropigo");
    ros::NodeHandle n;

    static const int us_pin = 15;

    ros::Subscriber cmd = n.subscribe("cmd_vel", 1, cmdCallback);
    ros::Subscriber servo_sub = n.subscribe("servo_cmd", 1, servoCallback);

    ros::Publisher battery_pub = n.advertise<sensor_msgs::BatteryState>("battery",1);

    servo_state_pub = n.advertise<sensor_msgs::JointState>("servo_state",1,true);

    ros::Publisher ultrasonic_pub = n.advertise<sensor_msgs::Range>("ultrasonic",1);

    // odometry
    ros::Publisher lwheel_pub = n.advertise<std_msgs::Int16>("lwheel",1);
    ros::Publisher rwheel_pub = n.advertise<std_msgs::Int16>("rwheel",1);

    /// Services
    // Encoder
    ros::ServiceServer enc_enable_srv = n.advertiseService("encoder_enable", enc_enable);
    ros::ServiceServer enc_disable_srv = n.advertiseService("encoder_disable", enc_disable);
    // LED
    ros::ServiceServer led_left_enable_srv = n.advertiseService("led_on_left", led_enable_left);
    ros::ServiceServer led_left_disable_srv = n.advertiseService("led_off_left", led_disable_left);
    ros::ServiceServer led_right_enable_srv = n.advertiseService("led_on_right", led_enable_right);
    ros::ServiceServer led_right_disable_srv = n.advertiseService("led_off_right", led_disable_right);

    ros::Rate loop(10);

    while(ros::ok()) {
        ros::Time time = ros::Time::now();

        // battery voltage
        sensor_msgs::BatteryState battery;
        battery.voltage = volt();
        battery_pub.publish(battery);

        // wheel encoder
        std_msgs::Int16 lwheel, rwheel;
        lwheel.data = int16_t(enc_read(0));
        rwheel.data = int16_t(enc_read(1));
        lwheel_pub.publish(lwheel);
        rwheel_pub.publish(rwheel);

        // ultrasonic distance
        sensor_msgs::Range us_range;
        us_range.header.stamp = time;
        us_range.radiation_type = sensor_msgs::Range::ULTRASOUND;
        // ultrasonic distance from cm in meter
        us_range.range = us_dist(us_pin) / 100.0f;
        ultrasonic_pub.publish(us_range);

        ros::spinOnce();
        loop.sleep();
    }

    led_off(0);
    led_off(1);

    ROS_INFO("Exit.");

    return 0;
}

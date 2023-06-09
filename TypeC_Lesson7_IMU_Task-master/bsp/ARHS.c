/*
* AHRS
* Copyright 2010 S.O.H. Madgwick
*
* This program is free software: you can redistribute it and/or
* modify it under the terms of the GNU Lesser Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser Public License for more details.
*
* You should have received a copy of the GNU Lesser Public License
* along with this program.  If not, see
* .
*/
// AHRS.c
// S.O.H. Madgwick
// 25th August 2010
//
// Quaternion implementation of the 'DCM filter' [Mayhony et al].  Incorporates the magnetic distortion
// compensation algorithms from my filter [Madgwick] which eliminates the need for a reference
// direction of flux (bx bz) to be predefined and limits the effect of magnetic distortions to yaw
// axis only.
//
// User must define 'halfT' as the (sample period / 2), and the filter gains 'Kp' and 'Ki'.
//
// Global variables 'q0', 'q1', 'q2', 'q3' are the quaternion elements representing the estimated
// orientation.  See my report for an overview of the use of quaternions in this application.
//
// User must call 'AHRSupdate()' every sample period and parse calibrated gyroscope ('gx', 'gy', 'gz'),
// accelerometer ('ax', 'ay', 'ay') and magnetometer ('mx', 'my', 'mz') data.  Gyroscope units are
// radians/second, accelerometer and magnetometer units are irrelevant as the vector is normalised.
//


#include "stm32f4xx.h"
#include "AHRS.h"




/* Private define ------------------------------------------------------------*/
#define Kp 2.0f                        // proportional gain governs rate of convergence to accelerometer/magnetometer
#define Ki 0.005f                // integral gain governs rate of convergence of gyroscope biases
#define halfT 0.0025f                // half the sample period        : 0.005s/2=0.0025s

#define ACCEL_1G 1000 //the acceleration of gravity is: 1000 mg

/* Private variables ---------------------------------------------------------*/
static float q0 = 1, q1 = 0, q2 = 0, q3 = 0;        // quaternion elements representing the estimated orientation
static float exInt = 0, eyInt = 0, ezInt = 0;        // scaLED integral error

/* Public variables ----------------------------------------------------------*/
EulerAngle_Type EulerAngle;        //unit: radian
u8 InitEulerAngle_Finished = 0;

float Magnetoresistor_mGauss_X = 0, Magnetoresistor_mGauss_Y = 0, Magnetoresistor_mGauss_Z = 0;//unit: milli-Gauss
float Accelerate_mg_X, Accelerate_mg_Y, Accelerate_mg_Z;//unit: mg
float AngularRate_dps_X, AngularRate_dps_Y, AngularRate_dps_Z;//unit: dps: degree per second

int16_t Magnetoresistor_X, Magnetoresistor_Y, Magnetoresistor_Z;
uint16_t Accelerate_X = 0, Accelerate_Y = 0, Accelerate_Z = 0;
uint16_t AngularRate_X = 0, AngularRate_Y = 0, AngularRate_Z = 0;

u8 Quaternion_Calibration_ok = 0;

/* Private macro -------------------------------------------------------------*/
/* Private typedef -----------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/

/*******************************************************************************
* Function Name  : AHRSupdate
* Description    : None
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
void AHRSupdate(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my, float mz) {
    float norm;
    float hx, hy, hz, bx, bz;
    float vx, vy, vz, wx, wy, wz;
    float ex, ey, ez;

    // auxiliary variables to reduce number of repeated operations
    float q0q0 = q0*q0;
    float q0q1 = q0*q1;
    float q0q2 = q0*q2;
    float q0q3 = q0*q3;
    float q1q1 = q1*q1;
    float q1q2 = q1*q2;
    float q1q3 = q1*q3;
    float q2q2 = q2*q2;
    float q2q3 = q2*q3;
    float q3q3 = q3*q3;

    // normalise the measurements
    norm = sqrt(ax*ax + ay*ay + az*az);
    ax = ax / norm;
    ay = ay / norm;
    az = az / norm;
    norm = sqrt(mx*mx + my*my + mz*mz);
    mx = mx / norm;
    my = my / norm;
    mz = mz / norm;

    // compute reference direction of flux
    hx = 2*mx*(0.5 - q2q2 - q3q3) + 2*my*(q1q2 - q0q3) + 2*mz*(q1q3 + q0q2);
    hy = 2*mx*(q1q2 + q0q3) + 2*my*(0.5 - q1q1 - q3q3) + 2*mz*(q2q3 - q0q1);
    hz = 2*mx*(q1q3 - q0q2) + 2*my*(q2q3 + q0q1) + 2*mz*(0.5 - q1q1 - q2q2);
    bx = sqrt((hx*hx) + (hy*hy));
    bz = hz;

    // estimated direction of gravity and flux (v and w)
    vx = 2*(q1q3 - q0q2);
    vy = 2*(q0q1 + q2q3);
    vz = q0q0 - q1q1 - q2q2 + q3q3;
    wx = 2*bx*(0.5 - q2q2 - q3q3) + 2*bz*(q1q3 - q0q2);
    wy = 2*bx*(q1q2 - q0q3) + 2*bz*(q0q1 + q2q3);
    wz = 2*bx*(q0q2 + q1q3) + 2*bz*(0.5 - q1q1 - q2q2);

    // error is sum of cross product between reference direction of fields and direction measured by sensors
    ex = (ay*vz - az*vy) + (my*wz - mz*wy);
    ey = (az*vx - ax*vz) + (mz*wx - mx*wz);
    ez = (ax*vy - ay*vx) + (mx*wy - my*wx);

    // integral error scaled integral gain
    exInt = exInt + ex*Ki;
    eyInt = eyInt + ey*Ki;
    ezInt = ezInt + ez*Ki;

    // adjusted gyroscope measurements
    gx = gx + Kp*ex + exInt;
    gy = gy + Kp*ey + eyInt;
    gz = gz + Kp*ez + ezInt;

    // integrate quaternion rate and normalise
    q0 = q0 + (-q1*gx - q2*gy - q3*gz)*halfT;
    q1 = q1 + (q0*gx + q2*gz - q3*gy)*halfT;
    q2 = q2 + (q0*gy - q1*gz + q3*gx)*halfT;
    q3 = q3 + (q0*gz + q1*gy - q2*gx)*halfT;

    // normalise quaternion
    norm = sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
    q0 = q0 / norm;
    q1 = q1 / norm;
    q2 = q2 / norm;
    q3 = q3 / norm;
}

float clamp(float Value, float Min, float Max)
{
    if(Value > Max)
    {
        return Max;
    }else if(Value < Min)
    {
        return Min;
    }else
    {
        return Value;
    }
}
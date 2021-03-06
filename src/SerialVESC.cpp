/*
  Copyright 2012-2014 Nathan Kau nathankau@stanford.edu

  This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#include "SerialVESC.h"
#include "buffer.h"
#include "datatypes.h"
#include "utils.h"
#include "VescUart.h"

/*************** PRIVATE METHODS ****************/

/**
* Converts an angle in the vesc encoder reference frame to a normalized angle
* @param  raw_angle [description]
* @return           [description]
*/
float VESC::vesc_to_normalized_angle(float raw_angle) {
  float normalized = raw_angle;

  // correct for encoder direction, ie if + angle is CW or CCW
  if(encoder_direction == -1) {
    normalized = utils_angle_difference(0, normalized);
  }

  // add encoder offset
  normalized += encoder_offset;

  // normalize to [0 360)
  utils_norm_angle(normalized);

  return normalized;
}

/**
 * Converts an angle in the robot frame to an angle in the vesc encoder frame
 * @param  raw_angle [description]
 * @return           [description]
 */
float VESC::normalized_to_vesc_angle(float normalized_angle) {
  float raw_angle = normalized_angle;

  // subtract offset
  raw_angle -= encoder_offset;

  // reverse if opposite direction
  if(encoder_direction == -1) {
    raw_angle = utils_angle_difference(0, raw_angle);
  }

  // normalize angle to 0 to 360
  utils_norm_angle(raw_angle);

  return raw_angle;
}

/**
 * Sends CAN position command to the VESC
 * @param pos desired position, automatically normalizes it
 */

// TODO: Doesn't seem to work? Motor will randomly turn off. Probably not resetting the timeout
// TODO: Test pos -> norm_pos fix

void VESC::_send_position(float pos) {
  vesc_uart.set_position(pos);
}

/**
 * Sends current command to the VESC
 */
void VESC::_send_current(float current) {
  vesc_uart.set_current(current);
}


// TODO: Test if I need to normalize pos
/**
 * Sends a CAN message with the new pid constants and position to the VESC
 * @param kp
 * @param ki
 * @param kd
 * @param pos
 */
void VESC::_send_position_pid_constants(float kp, float ki, float kd, float pos) {
  // CAN_message_t msg;
  // int MULTIPLIER = 100000; // max valu is .3 for any value (2^15 / 100000)
  // msg.id = controller_channel_ID | ((int32_t) CAN_PACKET_SET_P_PID_K<<8);
  // msg.len = 8;
  // int32_t index = 0;
  // buffer_append_int16(msg.buf,(int16_t)(kp*MULTIPLIER),&index);
  // buffer_append_int16(msg.buf,(int16_t)(ki*MULTIPLIER),&index);
  // buffer_append_int16(msg.buf,(int16_t)(kd*MULTIPLIER),&index);
  //
  //
  // // JANK AF ADDING POS TO CONSTANT COMMAND
  // int POS_MULTIPLIER = 50;
  // // Multiplying by 50 means resolution of 0.02 degrees, which is less than the
  // // encoder resolution of 0.07 degrees
  // buffer_append_int16(msg.buf,(int16_t)(pos*POS_MULTIPLIER),&index);
  //
  // /*
	// WACKO fix, doesn't work without the wait
	// */
	// long time = micros();
	// while(time - micros() < 1) {}
  //
	// CANtx.write(msg);
}

/*************** PUBLIC METHODS ****************/

/**
 * Constructs VESC object with initial params
 * I dont understand member initializer lists :( but CANtx breaks without it
 *
 * @param vesc_encoder_reading_period : Tell the VESC object how often it is
 *                                      getting new encoder readings.
 *                                      If the VESC is sending at 2000hz,
 *                                      use 500us
 * @param serial_port : reference to serial port to use to communicate to the
 *                      teensy
 */
 // TODO add serial object to constructor
VESC::VESC(int vesc_encoder_reading_period,
  HardwareSerial* serial_port) : pos_controller(0,0), vesc_uart(serial_port){

  // the first time_delta will be large and will give small deg per sec which is ok
  time_last_angle_read = 0;

  vesc_angle = 0.0;
  true_degps = 0;

  VESC_ENCODER_PERIOD = vesc_encoder_reading_period;
}
/**
 * Processes a given byte over the teensy-vesc serial port.
 * TODO: Don't update position if the type of message received wasn't a
 * position update
 *
 * @param rx_data byte from VESC sent over serial bus
 */
void VESC::packet_process_byte(uint8_t rx_data) {
  if(vesc_uart.packet_process_byte(rx_data,0)) {

    update_angle(vesc_uart.get_rotor_position());

    // Debug code for receiving messages
    // Serial.print(micros());
    // Serial.print("\t");
    // Serial.println(vesc_angle);
  }
}

/**
 * Sets up the vesc object to talk over this CAN ID channel
 * @param _encoder_offset    float, encoder offset
 * @param _encoder_direction int
 * @param _max_current       float, maximum current to send
 */
void VESC::attach(float _encoder_offset, int _encoder_direction, float _max_current) {
  encoder_offset = _encoder_offset;
  encoder_direction = _encoder_direction;
  max_current = _max_current;
}

/**
 * Sends position CAN message to motor to update position hold command.
 * Currently only implements VESC-side position hold.
 * @param deg normalized target angle in degrees
 */
void VESC::write(float deg) {
  _send_position(normalized_to_vesc_angle(deg));
}

/**
 * Sends CAN message to set current
 * @param current desired current in amps
 */
void VESC::write_current(float current) {
  _send_current(current);
}

/**
 * Sends CAN message to update position PID gains and position
 * @param kp P term gain
 * @param ki I term gain
 * @param kd D term gain
 * @param pos : normalized target position
 */
void VESC::write_pos_and_pid_gains(float kp, float ki, float kd, float pos) {
  _send_position_pid_constants(kp, ki, kd, normalized_to_vesc_angle(pos));
}

/**
   * Returns the last read normalized motor position in degrees. Note
   * that the motor position read is not the commanded position, but
   * the actual, last-read motor position
   * @return motor position
   */
float VESC::read() {
  return vesc_to_normalized_angle(vesc_angle);
}

/**
 * Updates the VESC objects knowledge of the motor angle
 * Takes between 4 and 5 us when using the while loop-based normalize
 * angle function
 *
 * @param raw_deg : measured position in vesc encoder frame. degrees
 * automatically normalizes the given angle (no > 180deg moves)
 */
void VESC::update_angle(float angle) {
  float corrected = angle;

  // 26 us without this line, 31 with this line = 5 us time
  utils_norm_angle(corrected);

  // Compute velocity in deg per s
  // This computation is subject to noise!
  // 37-38 us loop time
  // this line takes 6-8 us OUTDATED 12-21

  // Hardcoded sampling rate of 1000hz, make sure to change if changing
  // the send frequency of the VECS
  float temp = VESC_ENCODER_PERIOD*utils_angle_difference(corrected,vesc_angle);
  true_degps = temp;
  // add lowpass filter didn't help much or made the vibrations worse!
  // Tested 0.5 and 0.8 * temp

  // Update angle state
  vesc_angle = corrected;
}

/**
 * Prints VESC object state
 */
void VESC::print_debug() {
	if(last_print_debug > 100) {
		last_print_debug = 0;

		Serial.print("O: ");
		Serial.print(pos_controller.get_command());
		Serial.print(" \tEr: ");
		Serial.print(pos_controller.get_error());
		Serial.print(" \tEr.w:  ");
		Serial.print(pos_controller.get_error_deriv());
    Serial.print(" \tw: ");
    Serial.print(true_degps);
		Serial.print(" \tKp: ");
    float pterm,dterm;
    pos_controller.get_error_terms(pterm, dterm);
		Serial.print(pterm);
		Serial.print(" \tKd: ");
		Serial.println(dterm);
	}
}

/***** OLD ONBOARD PID CODE *******/
/**
 * Compute PID output and send to VESC given a normalized angle set
 * point. Uses last given position values.
 * @param set_point normalized angle set point
 */
void VESC::pid_update_normalized(float set_point) {
  pid_update(normalized_to_vesc_angle(set_point));
}

/**
 * Compute PID output and send to VESC. Uses last given values
 */
void VESC::pid_update(float set_point) {
  float error = utils_angle_difference(vesc_angle,set_point);
  float cur_command = max_current *
            pos_controller.compute_command(error,true_degps);

  _send_current(cur_command);
}

/**
 * Update kP and kD for teensy-side PID control
 * @param kP proportional gain
 * @param kD derivative gain
 */
void VESC::set_pid_gains(float kP, float kD) {
  pos_controller.set_gains(kP, kD);
}

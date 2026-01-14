#ifndef MESSAGE_H
#define MESSAGE_H

#include <tuple>
#include <iostream>

#include "crc16.h"

#define ACQUIRE_FIRMWARE_VERSION "01"
#define ACQUIRE_HARDWARE_ID "02"
#define AUTOFOCUS "04"
#define MANUAL_ZOOM "05"
#define ABSOLUTE_ZOOM "0f"
#define ACQUIRE_MAX_ZOOM "16"
#define MANUAL_FOCUS "06"
#define GIMBAL_ROTATION "07"
#define CENTER "08"
#define ACQUIRE_GIMBAL_INFO "0a"
#define FUNCTION_FEEDBACK_INFO "0b"
#define PHOTO_VIDEO "0c"
#define ACQUIRE_GIMBAL_ATTITUDE "0d"
#define CONTROL_ANGLE "0e"

class SIYI_Message {
public:
    std::string increment_seq(int val);

    static std::string compute_data_len(std::string data);

    [[nodiscard]] std::tuple<std::string, int, std::string, int> decode_msg(const std::string &msg) const;

    std::string encode_msg(const std::string &data, const std::string &cmd_id);

    /////////////////////////
    // MESSAGE DEFINITIONS //
    /////////////////////////

    std::string firmware_version_msg();

    std::string hardware_id_msg();

    std::string autofocus_msg();

    std::string zoom_in_msg();

    std::string zoom_out_msg();

    std::string zoom_halt_msg();

    std::string absolute_zoom_msg(int integer, int fractional);

    std::string maximum_zoom_msg();

    std::string focus_far_msg();

    std::string focus_close_msg();

    std::string focus_halt_msg();

    std::string gimbal_speed_msg(int yaw_speed, int pitch_speed);

    std::string gimbal_center_msg();

    std::string gimbal_info_msg();

    std::string lock_mode_msg();

    std::string follow_mode_msg();

    std::string fpv_mode_msg();

    std::string function_feedback_msg();

    std::string photo_msg();

    std::string record_msg();

    std::string gimbal_attitude_msg();

    std::string gimbal_angles_msg(float yaw, float pitch);

private:
    std::string HEADER = "5566";
    std::string _ctr = "01";
    std::string _cmd_id = "00";
    std::string _data;
    std::string _crc16 = "0000";
    const int MINIMUM_DATA_LENGTH = 10 * 2;
    int _seq = 0;
};

#endif // MESSAGE_H
#ifndef SDK_H
#define SDK_H

#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>

#include "message.h"

class SIYI_SDK {
public:
    explicit SIYI_SDK(const char *ip_address = "10.14.11.3", int port = 37260);

    ~SIYI_SDK();

    SIYI_Message msg;

    struct FirmwareVersionMsg {
        int seq = 0;
        std::string code_board_version;
        std::string gimbal_firmware_version;
        std::string zoom_firmware_version;
    };

    struct HardwareIDMsg {
        int seq = 0;
        std::string id;
    };

    struct AutofocusMsg {
        int seq = 0;
        bool success = false;
    };

    struct ManualZoomMsg {
        int seq = 0;
        float zoom_level = -1;
    };

    struct AbsoluteZoomMsg {
        int seq = 0;
        bool success = false;
    };

    struct MaxZoomMsg {
        int seq = 0;
        float max_level = 0.;
    };

    struct ManualFocusMsg {
        int seq = 0;
        bool success = false;
    };

    struct GimbalSpeedMsg {
        int seq = 0;
        bool success = false;
    };

    struct CenterMsg {
        int seq = 0;
        bool success = false;
    };

    struct RecordingStateMsg {
        int seq = 0;
        int state = -1;
    };

    struct MountingDirectionMsg {
        int seq = 0;
        int direction = -1;
    };

    struct MotionModeMsg {
        int seq = 0;
        int mode = -1;
    };

    struct FunctionFeedbackMsg {
        int seq = 0;
        int info_type = -1;
    };

    struct GimbalAttitudeMsg {
        int seq = 0;
        float yaw = 0.;
        float pitch = 0.;
        float roll = 0.;
        float yaw_speed = 0.;
        float pitch_speed = 0.;
        float roll_speed = 0.;
    };

    struct GimbalAnglesMsg {
        int seq = 0;
        float yaw = 0.;
        float pitch = 0.;
        float roll = 0.;
    };

    bool send_message(const std::string &message);

    void receive_message_loop(bool &connected);

    void gimbal_attitude_loop(bool &connected);

    void gimbal_info_loop(bool &connected);

    /////////////////////////////////
    //  REQUEST AND SET FUNCTIONS  //
    /////////////////////////////////

    bool request_firmware_version();

    bool request_hardware_id();

    bool request_autofocus();

    bool request_zoom_in();

    bool request_zoom_out();

    bool request_zoom_halt();

    bool set_absolute_zoom(int integer, int fractional);

    bool request_maximum_zoom();

    bool request_focus_far();

    bool request_focus_close();

    bool request_focus_halt();

    bool set_gimbal_speed(int yaw_speed, int pitch_speed);

    bool request_gimbal_center();

    bool request_gimbal_info();

    bool request_lock_mode();

    bool request_follow_mode();

    bool request_fpv_mode();

    bool request_function_feedback();

    bool request_photo();

    bool request_record();

    bool request_gimbal_attitude();

    bool set_gimbal_angles(float yaw, float pitch);


    ///////////////////////
    //  PARSE FUNCTIONS  //
    ///////////////////////

    void parse_firmware_version_msg(const std::string &parse_msg, int seq);

    void parse_hardware_id_msg(const std::string &parse_msg, int seq);

    void parse_autofocus_msg(const std::string &parse_msg, int seq);

    void parse_manual_zoom_msg(const std::string &parse_msg, int seq);

    void parse_absolute_zoom_msg(const std::string &parse_msg, int seq);

    void parse_maximum_zoom_msg(const std::string &parse_msg, int seq);

    void parse_manual_focus_msg(const std::string &parse_msg, int seq);

    void parse_gimbal_speed_msg(const std::string &parse_msg, int seq);

    void parse_gimbal_center_msg(const std::string &parse_msg, int seq);

    void parse_gimbal_info_msg(const std::string &parse_msg, int seq);

    void parse_function_feedback_msg(const std::string &parse_msg, int seq);

    void parse_gimbal_attitude_msg(const std::string &parse_msg, int seq);

    void parse_gimbal_angles_msg(const std::string &parse_msg, int seq);

    /////////////////////
    //  GET FUNCTIONS  //
    /////////////////////

    [[nodiscard]] std::tuple<std::string, std::string, std::string> get_firmware_version() const;

    [[nodiscard]] std::string get_hardware_id() const;

    [[nodiscard]] float get_zoom_level() const;

    [[nodiscard]] float get_maximum_zoom() const;

    [[nodiscard]] int get_recording_state() const;

    [[nodiscard]] int get_motion_mode() const;

    [[nodiscard]] int get_mounting_direction() const;

    [[nodiscard]] int get_function_feedback() const;

    [[nodiscard]] std::tuple<float, float, float> get_gimbal_attitude() const;

    [[nodiscard]] std::tuple<float, float, float> get_gimbal_attitude_speed() const;

private:
    FirmwareVersionMsg firmware_version_msg;
    HardwareIDMsg hardware_id_msg;
    AutofocusMsg autofocus_msg;
    ManualZoomMsg manual_zoom_msg;
    MaxZoomMsg max_zoom_msg;
    AbsoluteZoomMsg absoluteZoom_msg;
    ManualFocusMsg manual_focus_msg;
    GimbalSpeedMsg gimbal_speed_msg;
    CenterMsg gimbal_center_msg;
    RecordingStateMsg recording_state_msg;
    MountingDirectionMsg mounting_direction_msg;
    MotionModeMsg motion_mode_msg;
    FunctionFeedbackMsg function_feedback_msg;
    GimbalAttitudeMsg gimbal_att_msg;
    GimbalAnglesMsg gimbal_angles_msg;

    bool live = false;
    std::thread receive_message_thread;
    std::thread gimbal_attitude_thread;
    std::thread gimbal_info_thread;
    const int MINIMUM_DATA_LENGTH = 20;
    const int BUFFER_SIZE = 1024;
    std::string HEADER = "5566";

    int sockfd_;
    struct sockaddr_in server_addr_{};
    socklen_t server_addr_len = sizeof(server_addr_);
};

#endif // SDK_H

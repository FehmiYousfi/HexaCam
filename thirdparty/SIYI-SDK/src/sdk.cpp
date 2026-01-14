#include "sdk.h"

SIYI_SDK::SIYI_SDK(const char *ip_address, const int port) {
    // Create a UDP socket
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        std::cout << "Error, failed create socket" << std::endl;
        throw std::runtime_error("Failed to create socket");
    }

    // Set up the server address
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_addr.s_addr = inet_addr(ip_address);
    server_addr_.sin_port = htons(port);

    live = true;
    gimbal_attitude_thread = std::thread([this] { gimbal_attitude_loop(live); });
    gimbal_info_thread = std::thread([this] { gimbal_info_loop(live); });
    receive_message_thread = std::thread([this] { receive_message_loop(live); });
    std::cout << "UDP connection established" << std::endl;
}

SIYI_SDK::~SIYI_SDK() {
    // Close the socket
    close(sockfd_);

    live = false;
    gimbal_attitude_thread.join();
    gimbal_info_thread.join();
    receive_message_thread.join();
    std::cout << "UDP connection closed" << std::endl;
}

bool SIYI_SDK::send_message(const std::string &message) {
    // Convert the hex string to bytes
    std::stringstream ss(message);
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < message.length(); i += 2) {
        std::string byte_str = message.substr(i, 2);
        uint8_t byte = std::stoi(byte_str, nullptr, 16);
        bytes.push_back(byte);
    }

    // Send a response to the client
    ssize_t send_len = sendto(sockfd_, bytes.data(), bytes.size(), 0, (struct sockaddr *) &server_addr_,
                              sizeof(server_addr_));
    if (send_len < 0) {
        std::cout << "Error, failed to send message" << std::endl;
        return false;
    }
    return true;
}

void SIYI_SDK::receive_message_loop(bool &connected) {
    while (connected) {
        char buff[BUFFER_SIZE];
        int bytes = int(recvfrom(sockfd_, buff, BUFFER_SIZE, 0, (struct sockaddr *) &server_addr_, &server_addr_len));
        
        // Verify if any data was received
        if (bytes <= 0) {
            std::cerr << "Error: No data received or connection error (bytes=" << bytes << ")" << std::endl;
            continue;
        }
        
        // Convert received bytes to hex string in uppercase
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setfill('0');
        for (int i = 0; i < bytes; i++) {
            ss << std::setw(2) << (int) (unsigned char) buff[i];
        }

        // Convert hex string to lowercase
        std::string buff_str = ss.str();
        for (char &i: buff_str) i = char(tolower(i));

        // Add a sanity check for buffer length before further processing
        if (buff_str.length() < MINIMUM_DATA_LENGTH) {
            std::cerr << "Error: Received data is too short to process. Length: " << buff_str.length() << std::endl;
            continue;
        }

        // Process the buffer
        while (buff_str.length() >= MINIMUM_DATA_LENGTH) {
            if (buff_str.substr(0, 4) != HEADER) {
                std::cerr << "Error: Invalid header detected. Discarding bytes..." << std::endl;
                buff_str = buff_str.substr(2); // Shift the buffer by two characters
                continue;
            }

            // Check data length in the message
            std::string low_b = buff_str.substr(6, 2);
            std::string high_b = buff_str.substr(8, 2);
            std::string data_len_str = high_b + low_b;
            int data_len;

            try {
                data_len = std::stoi(data_len_str, nullptr, 16);
            } catch (const std::exception &e) {
                std::cerr << "Error: Failed to parse data length. " << e.what() << std::endl;
                buff_str = "";
                break;
            }

            // Ensure buffer has enough data for the payload
            if (buff_str.length() >= (MINIMUM_DATA_LENGTH + data_len * 2)) {
                std::string packet = buff_str.substr(0, MINIMUM_DATA_LENGTH + data_len * 2);
                buff_str = buff_str.substr(MINIMUM_DATA_LENGTH + data_len * 2);

                // Decode the packet
                std::tuple<std::string, int, std::string, int> decoded = msg.decode_msg(packet);
                if (std::get<0>(decoded).empty()) {
                    std::cerr << "Error: Failed to decode message." << std::endl;
                    continue;
                }

                std::string data = std::get<0>(decoded);
                std::string cmd_id = std::get<2>(decoded);
                int seq = std::get<3>(decoded);

                // Parse message based on command ID
                if (cmd_id == ACQUIRE_GIMBAL_ATTITUDE) SIYI_SDK::parse_gimbal_attitude_msg(data, seq);
                else if (cmd_id == ACQUIRE_GIMBAL_INFO) SIYI_SDK::parse_gimbal_info_msg(data, seq);
                else if (cmd_id == MANUAL_ZOOM) SIYI_SDK::parse_manual_zoom_msg(data, seq);
                else if (cmd_id == ACQUIRE_FIRMWARE_VERSION) SIYI_SDK::parse_firmware_version_msg(data, seq);
                else if (cmd_id == ACQUIRE_HARDWARE_ID) SIYI_SDK::parse_hardware_id_msg(data, seq);
                else if (cmd_id == FUNCTION_FEEDBACK_INFO) SIYI_SDK::parse_function_feedback_msg(data, seq);
                else if (cmd_id == GIMBAL_ROTATION) SIYI_SDK::parse_gimbal_speed_msg(data, seq);
                else if (cmd_id == CONTROL_ANGLE) SIYI_SDK::parse_gimbal_angles_msg(data, seq);
                else if (cmd_id == AUTOFOCUS) SIYI_SDK::parse_autofocus_msg(data, seq);
                else if (cmd_id == MANUAL_FOCUS) SIYI_SDK::parse_manual_focus_msg(data, seq);
                else if (cmd_id == CENTER) SIYI_SDK::parse_gimbal_center_msg(data, seq);
                else if (cmd_id == ABSOLUTE_ZOOM) SIYI_SDK::parse_absolute_zoom_msg(data, seq);
                else if (cmd_id == ACQUIRE_MAX_ZOOM) SIYI_SDK::parse_maximum_zoom_msg(data, seq);
            } else {
                std::cerr << "Error: Incomplete data packet detected. Discarding buffer." << std::endl;
                buff_str = "";
                break;
            }
        }
    }
}


void SIYI_SDK::gimbal_attitude_loop(bool &connected) {
    while (connected) {
        SIYI_SDK::request_gimbal_attitude();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // set frequency to 100 Hz
    }
}

void SIYI_SDK::gimbal_info_loop(bool &connected) {
    while (connected) {
        // Initialize hardware ID once (takes some time)
        if (SIYI_SDK::get_hardware_id().empty()) SIYI_SDK::request_hardware_id();

        SIYI_SDK::request_firmware_version();
        SIYI_SDK::request_gimbal_info();
        std::this_thread::sleep_for(std::chrono::seconds(1));  // set frequency to 1 Hz
    }
}

/////////////////////////////////
//  REQUEST AND SET FUNCTIONS  //
/////////////////////////////////

bool SIYI_SDK::request_firmware_version() {
    std::string message = msg.firmware_version_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_hardware_id() {
    std::string message = msg.hardware_id_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_autofocus() {
    std::string message = msg.autofocus_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_zoom_in() {
    std::string message = msg.zoom_in_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_zoom_out() {
    std::string message = msg.zoom_out_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_zoom_halt() {
    std::string message = msg.zoom_halt_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::set_absolute_zoom(int integer, int fractional) {
    std::string message = msg.absolute_zoom_msg(integer, fractional);
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_maximum_zoom() {
    std::string message = msg.maximum_zoom_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_focus_far() {
    std::string message = msg.focus_far_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_focus_close() {
    std::string message = msg.focus_close_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_focus_halt() {
    std::string message = msg.focus_halt_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::set_gimbal_speed(int yaw_speed, int pitch_speed) {
    /// -100~0~100. Away from 0 rotates faster, close to 0 - slower. 0 halts rotation
    std::string message = msg.gimbal_speed_msg(yaw_speed, pitch_speed);
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_gimbal_center() {
    std::string message = msg.gimbal_center_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_gimbal_info() {
    std::string message = msg.gimbal_info_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_lock_mode() {
    std::string message = msg.lock_mode_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_follow_mode() {
    std::string message = msg.follow_mode_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_fpv_mode() {
    std::string message = msg.fpv_mode_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_function_feedback() {
    std::string message = msg.function_feedback_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_photo() {
    std::string message = msg.photo_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_record() {
    // Start/stop recording
    std::string message = msg.record_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::request_gimbal_attitude() {
    std::string message = msg.gimbal_attitude_msg();
    if (send_message(message)) return true;
    else return false;
}

bool SIYI_SDK::set_gimbal_angles(float yaw, float pitch) {
    std::string message = msg.gimbal_angles_msg(yaw, pitch);
    if (send_message(message)) return true;
    else return false;
}

///////////////////////
//  PARSE FUNCTIONS  //
///////////////////////

void SIYI_SDK::parse_firmware_version_msg(const std::string &parse_msg, int seq) {
    firmware_version_msg.seq = seq;
    firmware_version_msg.code_board_version = parse_msg.substr(0, 8);
    firmware_version_msg.gimbal_firmware_version = parse_msg.substr(8, 8);
    firmware_version_msg.zoom_firmware_version = parse_msg.substr(16, 8);
}

void SIYI_SDK::parse_hardware_id_msg(const std::string &parse_msg, int seq) {
    hardware_id_msg.seq = seq;
    hardware_id_msg.id = parse_msg;
}

void SIYI_SDK::parse_autofocus_msg(const std::string &parse_msg, int seq) {
    autofocus_msg.seq = seq;
    autofocus_msg.success = static_cast<bool>(std::stoi(parse_msg, nullptr, 16));
}

void SIYI_SDK::parse_manual_zoom_msg(const std::string &parse_msg, int seq) {
    int32_t level;

    std::stringstream ss;
    ss << std::hex << (parse_msg.substr(2, 2) + parse_msg.substr(0, 2));
    ss >> level;

    manual_zoom_msg.seq = seq;
    manual_zoom_msg.zoom_level = float(level / 10.);
}

void SIYI_SDK::parse_absolute_zoom_msg(const std::string &parse_msg, int seq) {
    absoluteZoom_msg.seq = seq;
    absoluteZoom_msg.success = static_cast<bool>(std::stoi(parse_msg, nullptr, 16));
}

void SIYI_SDK::parse_maximum_zoom_msg(const std::string &parse_msg, int seq) {
    int max_int = std::stoi(parse_msg.substr(0, 2), nullptr, 16);
    int max_float = std::stoi(parse_msg.substr(2, 2), nullptr, 16);

    max_zoom_msg.max_level = float((max_int * 10 + max_float) / 10.);
}

void SIYI_SDK::parse_manual_focus_msg(const std::string &parse_msg, int seq) {
    manual_focus_msg.seq = seq;
    manual_focus_msg.success = static_cast<bool>(std::stoi(parse_msg, nullptr, 16));
}

void SIYI_SDK::parse_gimbal_speed_msg(const std::string &parse_msg, int seq) {
    gimbal_speed_msg.seq = seq;
    gimbal_speed_msg.success = static_cast<bool>(std::stoi(parse_msg, nullptr, 16));
}

void SIYI_SDK::parse_gimbal_center_msg(const std::string &parse_msg, int seq) {
    gimbal_center_msg.seq = seq;
    gimbal_center_msg.success = static_cast<bool>(std::stoi(parse_msg, nullptr, 16));
}

void SIYI_SDK::parse_gimbal_info_msg(const std::string &parse_msg, int seq) {
    int state = std::stoi(parse_msg.substr(6, 2), nullptr, 16);
    int mode = std::stoi(parse_msg.substr(8, 2), nullptr, 16);
    int dir = std::stoi(parse_msg.substr(10, 2), nullptr, 16);

    recording_state_msg.seq = seq;
    mounting_direction_msg.seq = seq;
    motion_mode_msg.seq = seq;
    recording_state_msg.state = state;
    mounting_direction_msg.direction = dir;
    motion_mode_msg.mode = mode;
}

void SIYI_SDK::parse_function_feedback_msg(const std::string &parse_msg, int seq) {
    function_feedback_msg.seq = seq;
    function_feedback_msg.info_type = std::stoi(parse_msg, nullptr, 16);
}

void SIYI_SDK::parse_gimbal_attitude_msg(const std::string &parse_msg, int seq) {
    int yaw, pitch, roll, yaw_speed, pitch_speed, roll_speed;

    std::stringstream ss;
    ss << std::hex << (parse_msg.substr(2, 2) + parse_msg.substr(0, 2));
    ss >> yaw;
    if (yaw > 65536 / 2) yaw = yaw - 65536;
    yaw = -yaw;  // reverse yaw as for some reason it is positive when turning clockwise by default

    ss.clear();
    ss << std::hex << parse_msg.substr(6, 2) + parse_msg.substr(4, 2);
    ss >> pitch;
    if (pitch > 65536 / 2) pitch = pitch - 65536;

    ss.clear();
    ss << std::hex << parse_msg.substr(10, 2) + parse_msg.substr(8, 2);
    ss >> roll;
    if (roll > 65536 / 2) roll = roll - 65536;

    ss.clear();
    ss << std::hex << parse_msg.substr(4, 2) + parse_msg.substr(12, 2);
    ss >> yaw_speed;
    if (yaw_speed > 65536 / 2) yaw_speed = yaw_speed - 65536;

    ss.clear();
    ss << std::hex << parse_msg.substr(18, 2) + parse_msg.substr(16, 2);
    ss >> pitch_speed;
    if (pitch_speed > 65536 / 2) pitch_speed = pitch_speed - 65536;

    ss.clear();
    ss << std::hex << parse_msg.substr(22, 2) + parse_msg.substr(20, 2);
    ss >> roll_speed;
    if (roll_speed > 65536 / 2) roll_speed = roll_speed - 65536;

    gimbal_att_msg.seq = seq;
    gimbal_att_msg.yaw = float(yaw / 10.);
    gimbal_att_msg.pitch = float(pitch / 10.);
    gimbal_att_msg.roll = float(roll / 10.);
    gimbal_att_msg.yaw_speed = float(yaw_speed / 10.);
    gimbal_att_msg.pitch_speed = float(pitch_speed / 10.);
    gimbal_att_msg.roll_speed = float(roll_speed / 10.);
}

void SIYI_SDK::parse_gimbal_angles_msg(const std::string &parse_msg, int seq) {
    int yaw, pitch, roll;

    std::stringstream ss;
    ss << std::hex << (parse_msg.substr(2, 2) + parse_msg.substr(0, 2));
    ss >> yaw;
    if (yaw > 65536 / 2) yaw = yaw - 65536;
    yaw = -yaw;  // reverse yaw as for some reason it is positive when turning clockwise by default

    ss.clear();
    ss << std::hex << parse_msg.substr(6, 2) + parse_msg.substr(4, 2);
    ss >> pitch;
    if (pitch > 65536 / 2) pitch = pitch - 65536;

    ss.clear();
    ss << std::hex << parse_msg.substr(10, 2) + parse_msg.substr(8, 2);
    ss >> roll;
    if (roll > 65536 / 2) roll = roll - 65536;

    gimbal_angles_msg.seq = seq;
    gimbal_angles_msg.yaw = float(yaw / 10.);
    gimbal_angles_msg.pitch = float(pitch / 10.);
    gimbal_angles_msg.roll = float(roll / 10.);
}

/////////////////////
//  GET FUNCTIONS  //
/////////////////////

std::tuple<std::string, std::string, std::string> SIYI_SDK::get_firmware_version() const {
    return std::make_tuple(firmware_version_msg.code_board_version, firmware_version_msg.gimbal_firmware_version,
                           firmware_version_msg.zoom_firmware_version);
}

std::string SIYI_SDK::get_hardware_id() const {
    return hardware_id_msg.id;
}

float SIYI_SDK::get_zoom_level() const {
    return manual_zoom_msg.zoom_level;
}

float SIYI_SDK::get_maximum_zoom() const {
    return max_zoom_msg.max_level;
}

int SIYI_SDK::get_recording_state() const {
    // 0 - off, 1 - on, 2 - slot empty, 3 - data loss
    return recording_state_msg.state;
}

int SIYI_SDK::get_motion_mode() const {
    // 0 - lock, 1 - follow, 2 - FPV
    return motion_mode_msg.mode;
}

int SIYI_SDK::get_mounting_direction() const {
    // 0 - normal, 1 - upside
    return mounting_direction_msg.direction;
}

int SIYI_SDK::get_function_feedback() const {
    // 0 - successful, 1 - photo fail, 2 - HDR on, 3 - HDR off, 4 - record fail
    return function_feedback_msg.info_type;
}

std::tuple<float, float, float> SIYI_SDK::get_gimbal_attitude() const {
    // yaw, pitch, roll angles in degrees
    return std::make_tuple(gimbal_att_msg.yaw, gimbal_att_msg.pitch, gimbal_att_msg.roll);
}

std::tuple<float, float, float> SIYI_SDK::get_gimbal_attitude_speed() const {
    // yaw, pitch, roll speeds in degrees/second
    return std::make_tuple(gimbal_att_msg.yaw_speed, gimbal_att_msg.pitch_speed, gimbal_att_msg.roll_speed);
}
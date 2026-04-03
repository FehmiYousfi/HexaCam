#include "servo_client.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <regex>

namespace ServoControl {

    ServoClient::ServoClient(const std::string& ip, int port, int timeout_ms)
        : server_ip(ip), server_port(port), socket_fd(-1), timeout_ms(timeout_ms), connected(false) {
        server_addr = std::make_unique<sockaddr_in>();
        memset(server_addr.get(), 0, sizeof(sockaddr_in));
        server_addr->sin_family = AF_INET;
        server_addr->sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip.c_str(), &server_addr->sin_addr);
    }

    ServoClient::~ServoClient() {
        disconnect();
    }

    bool ServoClient::connect() {
        if (connected) {
            return true;
        }
        
        socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            last_error = "Failed to create socket: " + std::string(strerror(errno));
            return false;
        }
        
        // Set socket timeout
        struct timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        
        if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            last_error = "Failed to set socket timeout: " + std::string(strerror(errno));
            close(socket_fd);
            socket_fd = -1;
            return false;
        }
        
        connected = true;
        last_error.clear();
        return true;
    }

    void ServoClient::disconnect() {
        if (socket_fd >= 0) {
            close(socket_fd);
            socket_fd = -1;
        }
        connected = false;
    }

    bool ServoClient::isConnected() const {
        return connected;
    }

    ServoResponse ServoClient::getCurrentPosition(const GpioDefinition& gpio) {
        ServoRequest request(RequestType::GET_CURRENT_POSITION, gpio);
        return sendRequest(request);
    }

    ServoResponse ServoClient::setPosition(int position, const GpioDefinition& gpio) {
        ServoRequest request(RequestType::SET_NEW_POSITION, position, gpio);
        return sendRequest(request);
    }

    ServoResponse ServoClient::sendRequest(const ServoRequest& request) {
        ServoResponse response;
        
        if (!connected) {
            if (!connect()) {
                response.response_message = "Not connected to server: " + last_error;
                return response;
            }
        }
        
        // Serialize request
        std::string json_request = serializeRequest(request);
        if (json_request.empty()) {
            response.response_message = "Failed to serialize request";
            return response;
        }
        
        // Send request
        if (!sendUdpMessage(json_request)) {
            response.response_message = "Failed to send request: " + last_error;
            return response;
        }
        
        // Receive response
        std::string json_response = receiveUdpMessage();
        if (json_response.empty()) {
            response.response_message = "Failed to receive response: " + last_error;
            return response;
        }
        
        // Deserialize response
        response = deserializeResponse(json_response);
        return response;
    }

    void ServoClient::setTimeout(int timeout_ms) {
        this->timeout_ms = timeout_ms;
        
        if (connected && socket_fd >= 0) {
            struct timeval timeout;
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
            setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        }
    }

    std::string ServoClient::getLastError() const {
        return last_error;
    }

    std::string ServoClient::serializeRequest(const ServoRequest& request) {
        cJSON* json = cJSON_CreateObject();
        if (!json) {
            last_error = "Failed to create JSON object";
            return "";
        }
        
        // Add request type
        cJSON_AddNumberToObject(json, "request_type", static_cast<int>(request.request_type));
        
        // Add new_value if it's a SET_NEW_POSITION request
        if (request.request_type == RequestType::SET_NEW_POSITION) {
            cJSON_AddNumberToObject(json, "new_value", request.new_value);
        }
        
        // Add GPIO definition
        cJSON* gpio_def = cJSON_CreateObject();
        if (gpio_def) {
            cJSON_AddStringToObject(gpio_def, "consumer", request.gpio_def.consumer.c_str());
            cJSON_AddStringToObject(gpio_def, "gpio_chip", request.gpio_def.gpio_chip.c_str());
            cJSON_AddNumberToObject(gpio_def, "gpio_line", request.gpio_def.gpio_line);
            cJSON_AddItemToObject(json, "gpio_definition", gpio_def);
        }
        
        char* json_string = cJSON_Print(json);
        std::string result;
        if (json_string) {
            result = json_string;
            free(json_string);
        }
        
        cJSON_Delete(json);
        return result;
    }

    ServoResponse ServoClient::deserializeResponse(const std::string& json_str) {
        ServoResponse response;
        
        cJSON* json = cJSON_Parse(json_str.c_str());
        if (!json) {
            response.response_message = "Failed to parse JSON response";
            return response;
        }
        
        // Parse response type
        cJSON* response_type = cJSON_GetObjectItem(json, "response_type");
        if (response_type && cJSON_IsNumber(response_type)) {
            response.response_type = static_cast<ResponseType>(response_type->valueint);
        }
        
        // Parse response message
        cJSON* response_message = cJSON_GetObjectItem(json, "response_message");
        if (response_message && cJSON_IsString(response_message)) {
            response.response_message = response_message->valuestring;
        }
        
        cJSON_Delete(json);
        return response;
    }

    bool ServoClient::sendUdpMessage(const std::string& message) {
        ssize_t sent = sendto(socket_fd, message.c_str(), message.length(), 0,
                             (struct sockaddr*)server_addr.get(), sizeof(sockaddr_in));
        
        if (sent < 0) {
            last_error = "Failed to send UDP message: " + std::string(strerror(errno));
            return false;
        }
        
        if (static_cast<size_t>(sent) != message.length()) {
            last_error = "Incomplete message sent";
            return false;
        }
        
        return true;
    }

    std::string ServoClient::receiveUdpMessage() {
        char buffer[1024];
        sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t received = recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0,
                                   (struct sockaddr*)&from_addr, &from_len);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                last_error = "Timeout waiting for response";
            } else {
                last_error = "Failed to receive UDP message: " + std::string(strerror(errno));
            }
            return "";
        }
        
        buffer[received] = '\0';
        return std::string(buffer);
    }

    // Utility functions
    namespace Utils {
        int extractPositionFromResponse(const ServoResponse& response) {
            if (response.response_type != ResponseType::SUCCESS) {
                return -1;
            }
            
            // Use regex to extract position from message like "Current servo position: 90"
            std::regex position_regex(R"(.*position.*?(\d+))");
            std::smatch match;
            
            if (std::regex_search(response.response_message, match, position_regex)) {
                try {
                    return std::stoi(match[1].str());
                } catch (const std::exception&) {
                    return -1;
                }
            }
            
            return -1;
        }
        
        bool isSuccessResponse(const ServoResponse& response) {
            return response.response_type == ResponseType::SUCCESS;
        }
        
        std::string formatResponse(const ServoResponse& response) {
            std::ostringstream oss;
            oss << "Response Type: " << (response.response_type == ResponseType::SUCCESS ? "SUCCESS" : "FAILED") << "\n";
            oss << "Message: " << response.response_message;
            return oss.str();
        }
    }
}
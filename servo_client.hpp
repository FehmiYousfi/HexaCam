#ifndef SERVO_CLIENT_HPP
#define SERVO_CLIENT_HPP

#include <string>
#include <memory>
#include "cJSON.h"

// Forward declarations
struct sockaddr_in;

/**
 * Servo Client Library for UDP Communication
 * 
 * This library provides a C++ interface for communicating with the servo control UDP server.
 * It handles JSON serialization/deserialization and UDP communication automatically.
 */

namespace ServoControl {

    enum class RequestType {
        GET_CURRENT_POSITION = 0,
        SET_NEW_POSITION = 1
    };

    enum class ResponseType {
        SUCCESS = 0,
        FAILED = 1
    };

    struct GpioDefinition {
        std::string consumer;
        std::string gpio_chip;
        int gpio_line;
        
        GpioDefinition(const std::string& cons = "servo_client", 
                      const std::string& chip = "/dev/gpiochip0", 
                      int line = 32)
            : consumer(cons), gpio_chip(chip), gpio_line(line) {}
    };

    struct ServoRequest {
        RequestType request_type;
        int new_value;  // Only used for SET_NEW_POSITION
        GpioDefinition gpio_def;
        
        ServoRequest(RequestType type, const GpioDefinition& gpio = GpioDefinition())
            : request_type(type), new_value(0), gpio_def(gpio) {}
        
        ServoRequest(RequestType type, int value, const GpioDefinition& gpio = GpioDefinition())
            : request_type(type), new_value(value), gpio_def(gpio) {}
    };

    struct ServoResponse {
        ResponseType response_type;
        std::string response_message;
        
        ServoResponse() : response_type(ResponseType::FAILED) {}
    };

    class ServoClient {
    private:
        std::string server_ip;
        int server_port;
        int socket_fd;
        std::unique_ptr<sockaddr_in> server_addr;
        int timeout_ms;
        
        // Internal helper methods
        std::string serializeRequest(const ServoRequest& request);
        ServoResponse deserializeResponse(const std::string& json_str);
        bool sendUdpMessage(const std::string& message);
        std::string receiveUdpMessage();
        
    public:
        /**
         * Constructor
         * @param ip Server IP address (default: "127.0.0.1")
         * @param port Server port (default: 8000)
         * @param timeout_ms Timeout in milliseconds for UDP operations (default: 5000)
         */
        ServoClient(const std::string& ip = "10.14.11.1", int port = 8000, int timeout_ms = 5000);
        
        /**
         * Destructor - cleanup resources
         */
        ~ServoClient();
        
        /**
         * Connect to the servo server
         * @return true if connection successful, false otherwise
         */
        bool connect();
        
        /**
         * Disconnect from the servo server
         */
        void disconnect();
        
        /**
         * Check if client is connected
         * @return true if connected, false otherwise
         */
        bool isConnected() const;
        
        /**
         * Get current servo position
         * @param gpio GPIO configuration (optional)
         * @return ServoResponse with current position or error
         */
        ServoResponse getCurrentPosition(const GpioDefinition& gpio = GpioDefinition());
        
        /**
         * Set servo to new position
         * @param position New position (0-180 degrees)
         * @param gpio GPIO configuration (optional)
         * @return ServoResponse with success/failure status
         */
        ServoResponse setPosition(int position, const GpioDefinition& gpio = GpioDefinition());
        
        /**
         * Send custom request to server
         * @param request Custom servo request
         * @return ServoResponse from server
         */
        ServoResponse sendRequest(const ServoRequest& request);
        
        /**
         * Set timeout for UDP operations
         * @param timeout_ms Timeout in milliseconds
         */
        void setTimeout(int timeout_ms);
        
        /**
         * Get last error message
         * @return Error message string
         */
        std::string getLastError() const;
        
    private:
        std::string last_error;
        bool connected;
    };

    /**
     * Utility functions for working with servo responses
     */
    namespace Utils {
        /**
         * Extract position value from response message
         * @param response ServoResponse to parse
         * @return Position value, or -1 if not found
         */
        int extractPositionFromResponse(const ServoResponse& response);
        
        /**
         * Check if response indicates success
         * @param response ServoResponse to check
         * @return true if successful, false otherwise
         */
        bool isSuccessResponse(const ServoResponse& response);
        
        /**
         * Pretty print a ServoResponse
         * @param response Response to print
         * @return Formatted string
         */
        std::string formatResponse(const ServoResponse& response);
    }
}

#endif // SERVO_CLIENT_HPP

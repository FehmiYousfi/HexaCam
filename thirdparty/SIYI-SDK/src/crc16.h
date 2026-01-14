#ifndef CRC16_H
#define CRC16_H

#include <vector>
#include <iomanip>

class CRC16 {
public:
    static uint16_t compute_crc16(const std::vector<uint8_t> &data);

    static std::string compute_str_swap(const std::string &val);
};

#endif // CRC16_H
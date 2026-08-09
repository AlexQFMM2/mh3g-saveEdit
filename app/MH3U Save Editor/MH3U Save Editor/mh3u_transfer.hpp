#ifndef MH3U_TRANSFER_HPP
#define MH3U_TRANSFER_HPP

#include "mh3u_se.hpp"

#include <array>
#include <string>
#include <vector>

namespace MH3U_Transfer
{
    struct chest_entry_t
    {
        uint32_t panel;
        uint32_t slot;
        uint16_t itemId;
        uint16_t count;
    };

    struct equipment_entry_t
    {
        uint32_t panel;
        uint32_t slot;
        std::array<uint8_t, EQUIPMENT_SIZE> bytes;
    };

    std::string exportChest(const save_t &save);
    std::string exportEquipmentBox(const save_t &save);

    bool parseChest(const std::string &form, std::vector<chest_entry_t> &entries, std::string &error);
    bool parseEquipmentBox(const std::string &form, std::vector<equipment_entry_t> &entries, std::string &error);

    void applyChest(const std::vector<chest_entry_t> &entries, save_t &save);
    void applyEquipmentBox(const std::vector<equipment_entry_t> &entries, save_t &save);
}

#endif // MH3U_TRANSFER_HPP

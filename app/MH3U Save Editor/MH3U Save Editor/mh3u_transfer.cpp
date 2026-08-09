#include "mh3u_transfer.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

namespace
{
    const char *TRANSFER_MAGIC = "MH3U_TRANSFER";
    const char *TRANSFER_VERSION = "1";
    const char *CHEST_KIND = "ITEM_CHEST";
    const char *EQUIPMENT_KIND = "EQUIPMENT_BOX";

    std::string trim(const std::string &value)
    {
        const std::string whitespace = " \t\r\n";
        size_t first = value.find_first_not_of(whitespace);
        if (first == std::string::npos)
        {
            return std::string();
        }
        size_t last = value.find_last_not_of(whitespace);
        return value.substr(first, last - first + 1);
    }

    bool parseCsvLine(const std::string &line, std::vector<std::string> &fields)
    {
        fields.clear();
        std::string field;
        bool quoted = false;

        for (size_t i = 0; i < line.size(); i++)
        {
            char current = line[i];
            if (quoted)
            {
                if (current == '"')
                {
                    if (i + 1 < line.size() && line[i + 1] == '"')
                    {
                        field += '"';
                        i++;
                    }
                    else
                    {
                        quoted = false;
                    }
                }
                else
                {
                    field += current;
                }
            }
            else if (current == ',')
            {
                fields.push_back(trim(field));
                field.clear();
            }
            else if (current == '"')
            {
                if (!trim(field).empty())
                {
                    return false;
                }
                field.clear();
                quoted = true;
            }
            else
            {
                field += current;
            }
        }

        if (quoted)
        {
            return false;
        }
        fields.push_back(trim(field));
        return true;
    }

    bool parseUnsigned(const std::string &text, uint32_t maximum, uint32_t &value)
    {
        std::string clean = trim(text);
        if (clean.empty())
        {
            return false;
        }
        for (size_t i = 0; i < clean.size(); i++)
        {
            if (clean[i] < '0' || clean[i] > '9')
            {
                return false;
            }
        }

        errno = 0;
        char *end = NULL;
        unsigned long parsed = std::strtoul(clean.c_str(), &end, 10);
        if (errno == ERANGE || end == clean.c_str() || *end != '\0' || parsed > maximum)
        {
            return false;
        }
        value = (uint32_t) parsed;
        return true;
    }

    std::string lineError(size_t lineNumber, const std::string &message)
    {
        std::ostringstream output;
        output << "Line " << lineNumber << ": " << message;
        return output.str();
    }

    bool readNonEmptyLine(std::istringstream &stream, std::string &line, size_t &lineNumber)
    {
        while (std::getline(stream, line))
        {
            lineNumber++;
            if (!trim(line).empty())
            {
                return true;
            }
        }
        return false;
    }

    bool validatePreamble(std::istringstream &stream, const char *kind, const std::vector<std::string> &expectedHeader,
        size_t &lineNumber, std::string &error)
    {
        std::string line;
        std::vector<std::string> fields;
        if (!readNonEmptyLine(stream, line, lineNumber))
        {
            error = "The transfer form is empty.";
            return false;
        }

        if (line.size() >= 3 && (uint8_t) line[0] == 0xef && (uint8_t) line[1] == 0xbb && (uint8_t) line[2] == 0xbf)
        {
            line.erase(0, 3);
        }
        if (!parseCsvLine(line, fields) || fields.size() != 3 || fields[0] != TRANSFER_MAGIC ||
            fields[1] != TRANSFER_VERSION || fields[2] != kind)
        {
            error = lineError(lineNumber, std::string("expected ") + TRANSFER_MAGIC + "," + TRANSFER_VERSION + "," + kind + ".");
            return false;
        }

        if (!readNonEmptyLine(stream, line, lineNumber))
        {
            error = "The transfer form has no column header.";
            return false;
        }
        if (!parseCsvLine(line, fields) || fields != expectedHeader)
        {
            error = lineError(lineNumber, "the column header does not match this transfer form version.");
            return false;
        }
        return true;
    }

    std::vector<std::string> chestHeader()
    {
        std::vector<std::string> header;
        header.push_back("page");
        header.push_back("slot");
        header.push_back("item_id");
        header.push_back("count");
        return header;
    }

    std::vector<std::string> equipmentHeader()
    {
        std::vector<std::string> header;
        header.push_back("page");
        header.push_back("slot");
        header.push_back("equipment_type");
        for (uint32_t i = 0; i < EQUIPMENT_SIZE; i++)
        {
            std::ostringstream name;
            name << "byte_" << i;
            header.push_back(name.str());
        }
        return header;
    }
}

namespace MH3U_Transfer
{
    std::string exportChest(const save_t &save)
    {
        std::ostringstream output;
        output << TRANSFER_MAGIC << ',' << TRANSFER_VERSION << ',' << CHEST_KIND << "\r\n";
        output << "page,slot,item_id,count\r\n";
        for (uint32_t panel = 0; panel < 10; panel++)
        {
            for (uint32_t slot = 0; slot < 100; slot++)
            {
                const item_t &item = save.chest[panel][slot];
                output << panel + 1 << ',' << slot + 1 << ',' << item.id << ',' << item.count << "\r\n";
            }
        }
        return output.str();
    }

    std::string exportEquipmentBox(const save_t &save)
    {
        std::ostringstream output;
        output << TRANSFER_MAGIC << ',' << TRANSFER_VERSION << ',' << EQUIPMENT_KIND << "\r\n";
        output << "page,slot,equipment_type";
        for (uint32_t i = 0; i < EQUIPMENT_SIZE; i++)
        {
            output << ",byte_" << i;
        }
        output << "\r\n";

        for (uint32_t panel = 0; panel < 10; panel++)
        {
            for (uint32_t slot = 0; slot < 100; slot++)
            {
                const equipment_t &equipment = save.box[panel][slot];
                output << panel + 1 << ',' << slot + 1 << ',' << (uint32_t) equipment[0];
                for (uint32_t i = 0; i < EQUIPMENT_SIZE; i++)
                {
                    output << ',' << (uint32_t) equipment[i];
                }
                output << "\r\n";
            }
        }
        return output.str();
    }

    bool parseChest(const std::string &form, std::vector<chest_entry_t> &entries, std::string &error)
    {
        entries.clear();
        error.clear();
        std::istringstream stream(form);
        size_t lineNumber = 0;
        if (!validatePreamble(stream, CHEST_KIND, chestHeader(), lineNumber, error))
        {
            return false;
        }

        bool seen[10][100] = {};
        std::string line;
        std::vector<std::string> fields;
        while (std::getline(stream, line))
        {
            lineNumber++;
            if (trim(line).empty())
            {
                continue;
            }
            if (!parseCsvLine(line, fields) || fields.size() != 4)
            {
                error = lineError(lineNumber, "expected 4 comma-separated columns.");
                entries.clear();
                return false;
            }

            uint32_t page = 0, slot = 0, itemId = 0, count = 0;
            if (!parseUnsigned(fields[0], 10, page) || page == 0)
            {
                error = lineError(lineNumber, "page must be between 1 and 10.");
            }
            else if (!parseUnsigned(fields[1], 100, slot) || slot == 0)
            {
                error = lineError(lineNumber, "slot must be between 1 and 100.");
            }
            else if (!parseUnsigned(fields[2], std::numeric_limits<uint16_t>::max(), itemId))
            {
                error = lineError(lineNumber, "item_id must be between 0 and 65535.");
            }
            else if (!parseUnsigned(fields[3], std::numeric_limits<uint16_t>::max(), count))
            {
                error = lineError(lineNumber, "count must be between 0 and 65535.");
            }
            else if (seen[page - 1][slot - 1])
            {
                error = lineError(lineNumber, "this page and slot are duplicated.");
            }

            if (!error.empty())
            {
                entries.clear();
                return false;
            }

            seen[page - 1][slot - 1] = true;
            chest_entry_t entry = { page - 1, slot - 1, (uint16_t) itemId, (uint16_t) count };
            entries.push_back(entry);
        }

        if (entries.empty())
        {
            error = "The transfer form contains no item slots.";
            return false;
        }
        return true;
    }

    bool parseEquipmentBox(const std::string &form, std::vector<equipment_entry_t> &entries, std::string &error)
    {
        entries.clear();
        error.clear();
        std::istringstream stream(form);
        size_t lineNumber = 0;
        if (!validatePreamble(stream, EQUIPMENT_KIND, equipmentHeader(), lineNumber, error))
        {
            return false;
        }

        bool seen[10][100] = {};
        std::string line;
        std::vector<std::string> fields;
        const size_t expectedColumns = 3 + EQUIPMENT_SIZE;
        while (std::getline(stream, line))
        {
            lineNumber++;
            if (trim(line).empty())
            {
                continue;
            }
            if (!parseCsvLine(line, fields) || fields.size() != expectedColumns)
            {
                std::ostringstream message;
                message << "expected " << expectedColumns << " comma-separated columns.";
                error = lineError(lineNumber, message.str());
                entries.clear();
                return false;
            }

            uint32_t page = 0, slot = 0, equipmentType = 0;
            if (!parseUnsigned(fields[0], 10, page) || page == 0)
            {
                error = lineError(lineNumber, "page must be between 1 and 10.");
            }
            else if (!parseUnsigned(fields[1], 100, slot) || slot == 0)
            {
                error = lineError(lineNumber, "slot must be between 1 and 100.");
            }
            else if (!parseUnsigned(fields[2], MH3U_Type::HHType, equipmentType))
            {
                error = lineError(lineNumber, "equipment_type must be between 0 and 19.");
            }
            else if (seen[page - 1][slot - 1])
            {
                error = lineError(lineNumber, "this page and slot are duplicated.");
            }

            equipment_entry_t entry;
            entry.panel = page > 0 ? page - 1 : 0;
            entry.slot = slot > 0 ? slot - 1 : 0;
            entry.bytes.fill(0);
            if (error.empty())
            {
                for (uint32_t i = 0; i < EQUIPMENT_SIZE; i++)
                {
                    uint32_t byte = 0;
                    if (!parseUnsigned(fields[3 + i], std::numeric_limits<uint8_t>::max(), byte))
                    {
                        std::ostringstream message;
                        message << "byte_" << i << " must be between 0 and 255.";
                        error = lineError(lineNumber, message.str());
                        break;
                    }
                    entry.bytes[i] = (uint8_t) byte;
                }
            }
            if (error.empty() && entry.bytes[0] != equipmentType)
            {
                error = lineError(lineNumber, "equipment_type must match byte_0.");
            }
            if (!error.empty())
            {
                entries.clear();
                return false;
            }

            seen[page - 1][slot - 1] = true;
            entries.push_back(entry);
        }

        if (entries.empty())
        {
            error = "The transfer form contains no equipment slots.";
            return false;
        }
        return true;
    }

    void applyChest(const std::vector<chest_entry_t> &entries, save_t &save)
    {
        for (size_t i = 0; i < entries.size(); i++)
        {
            const chest_entry_t &entry = entries[i];
            save.chest[entry.panel][entry.slot].id = entry.itemId;
            save.chest[entry.panel][entry.slot].count = entry.count;
        }
    }

    void applyEquipmentBox(const std::vector<equipment_entry_t> &entries, save_t &save)
    {
        for (size_t i = 0; i < entries.size(); i++)
        {
            const equipment_entry_t &entry = entries[i];
            std::memcpy(save.box[entry.panel][entry.slot], entry.bytes.data(), EQUIPMENT_SIZE);
        }
    }
}

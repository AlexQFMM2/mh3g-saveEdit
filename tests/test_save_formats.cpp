#include "mh3u_se.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static std::vector<uint8_t> readFile(const std::string &path)
{
    std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);
    if (!stream)
    {
        throw std::runtime_error("cannot open " + path);
    }
    std::streamoff size = stream.tellg();
    std::vector<uint8_t> bytes((size_t) size);
    stream.seekg(0, stream.beg);
    stream.read((char*) bytes.data(), size);
    if (!stream)
    {
        throw std::runtime_error("cannot read " + path);
    }
    return bytes;
}

static void require(bool condition, const std::string &message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

static void testSave(const std::string &path, save_format_e expectedFormat)
{
    const std::string output = path + ".roundtrip-test.tmp";
    std::remove(output.c_str());

    try
    {
        const std::vector<uint8_t> original = readFile(path);
        MH3U_SE editor;
        require(editor.load(path), editor.lastError());
        require(editor.format() == expectedFormat, "wrong format detected for " + path);
        require(editor.save(output), editor.lastError());
        require(readFile(output) == original, "unchanged round trip differs for " + path);

        editor.savedata->money = 0x00020304;
        editor.savedata->mogapoint = 0x00050607;
        editor.savedata->inventory[0][0].id = 0x1234;
        editor.savedata->inventory[0][0].count = 0x0042;
        editor.savedata->box[0][0][0] = MH3U_Type::GSType;
        editor.savedata->box[0][0][2] = 0x34;
        editor.savedata->box[0][0][3] = 0x12;
        editor.savedata->box[0][0][8] = 0x78;
        editor.savedata->box[0][0][9] = 0x56;
        require(editor.save(output), editor.lastError());

        const std::vector<uint8_t> edited = readFile(output);
        const size_t base = expectedFormat == SAVE_FORMAT_WIIU ? WIIU_HEADER_SIZE : 0;
        if (expectedFormat == SAVE_FORMAT_WIIU)
        {
            require(std::equal(original.begin(), original.begin() + WIIU_HEADER_SIZE, edited.begin()),
                "Wii U header changed");
            require(edited[base + MONEY_OFFSET] == 0x00 && edited[base + MONEY_OFFSET + 1] == 0x02 &&
                edited[base + MONEY_OFFSET + 2] == 0x03 && edited[base + MONEY_OFFSET + 3] == 0x04,
                "Wii U money was not written big-endian");
            require(edited[base + INVENTORY_OFFSET] == 0x12 && edited[base + INVENTORY_OFFSET + 1] == 0x34,
                "Wii U item ID was not written big-endian");
            require(edited[base + BOX_OFFSET + 2] == 0x12 && edited[base + BOX_OFFSET + 3] == 0x34,
                "Wii U equipment ID was not written big-endian");
            require(edited[base + BOX_OFFSET + 8] == 0x56 && edited[base + BOX_OFFSET + 9] == 0x78,
                "Wii U jewel ID was not written big-endian");
        }
        else
        {
            require(edited[MONEY_OFFSET] == 0x04 && edited[MONEY_OFFSET + 1] == 0x03 &&
                edited[MONEY_OFFSET + 2] == 0x02 && edited[MONEY_OFFSET + 3] == 0x00,
                "3DS money was not written little-endian");
            require(edited[INVENTORY_OFFSET] == 0x34 && edited[INVENTORY_OFFSET + 1] == 0x12,
                "3DS item ID was not written little-endian");
            require(edited[BOX_OFFSET + 2] == 0x34 && edited[BOX_OFFSET + 3] == 0x12,
                "3DS equipment ID was not written little-endian");
            require(edited[BOX_OFFSET + 8] == 0x78 && edited[BOX_OFFSET + 9] == 0x56,
                "3DS jewel ID was not written little-endian");
        }

        MH3U_SE reloaded;
        require(reloaded.load(output), reloaded.lastError());
        require(reloaded.format() == expectedFormat, "format changed after save");
        require(reloaded.savedata->money == 0x00020304, "money byte order is wrong");
        require(reloaded.savedata->mogapoint == 0x00050607, "Moga Point byte order is wrong");
        require(reloaded.savedata->inventory[0][0].id == 0x1234, "item ID byte order is wrong");
        require(reloaded.savedata->inventory[0][0].count == 0x0042, "item count byte order is wrong");
        require(reloaded.savedata->box[0][0][2] == 0x34 && reloaded.savedata->box[0][0][3] == 0x12,
            "equipment ID byte order is wrong");
        require(reloaded.savedata->box[0][0][8] == 0x78 && reloaded.savedata->box[0][0][9] == 0x56,
            "jewel ID byte order is wrong");
    }
    catch (...)
    {
        std::remove(output.c_str());
        throw;
    }

    std::remove(output.c_str());
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: test_save_formats <3ds-user-file> <wiiu-user-file>" << std::endl;
        return 2;
    }

    try
    {
        testSave(argv[1], SAVE_FORMAT_N3DS);
        testSave(argv[2], SAVE_FORMAT_WIIU);
        std::cout << "3DS and Wii U save format tests passed" << std::endl;
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "test failed: " << error.what() << std::endl;
        return 1;
    }
}

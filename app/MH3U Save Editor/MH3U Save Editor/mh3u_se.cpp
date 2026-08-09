#include "mh3u_se.hpp"

#include <algorithm>
#include <cstring>

namespace
{
	void swapEquipmentByteOrder(equipment_t &equipment)
	{
		std::swap(equipment[2], equipment[3]);
		std::swap(equipment[8], equipment[9]);
		std::swap(equipment[10], equipment[11]);
		std::swap(equipment[12], equipment[13]);
	}
}


MH3U_SE::MH3U_SE()
{
	savedata = NULL;
	saveFormat = SAVE_FORMAT_UNKNOWN;
	dataOffset = 0;
}


MH3U_SE::~MH3U_SE()
{
	cdelete(savedata);
}


void MH3U_SE::setFilename(std::string output)
{
	this->filename = output;
}


bool MH3U_SE::loaded()
{
	return (this->savedata != NULL);
}


save_format_e MH3U_SE::format() const
{
	return saveFormat;
}


std::string MH3U_SE::formatName() const
{
	switch (saveFormat)
	{
		case SAVE_FORMAT_N3DS:
			return "Nintendo 3DS";
		case SAVE_FORMAT_WIIU:
			return "Wii U";
		case SAVE_FORMAT_UNKNOWN:
		default:
			return "Unknown";
	}
}


std::string MH3U_SE::lastError() const
{
	return errorMessage;
}


uint32_t MH3U_SE::nameSize() const
{
	return saveFormat == SAVE_FORMAT_WIIU ? WIIU_NAME_SIZE : N3DS_NAME_SIZE;
}


bool MH3U_SE::load(std::string input)
{
	std::ifstream fs;
	errorMessage.clear();

	try
	{
		fs.open(input.c_str(), std::fstream::in | std::fstream::binary | std::fstream::ate);
	
		if (!fs)
		{
			setError("Unable to open the selected file.");
			return false;
		}

		std::streamoff fileLength = fs.tellg();
		save_format_e detectedFormat = SAVE_FORMAT_UNKNOWN;
		uint32_t detectedDataOffset = 0;
		if (fileLength == SAVEFILE_SIZE)
		{
			detectedFormat = SAVE_FORMAT_N3DS;
		}
		else if (fileLength == WIIU_SAVEFILE_SIZE)
		{
			detectedFormat = SAVE_FORMAT_WIIU;
			detectedDataOffset = WIIU_HEADER_SIZE;
		}
		else
		{
			std::stringstream message;
			message << "Unsupported save size: " << fileLength
			        << " bytes. Expected " << SAVEFILE_SIZE
			        << " (3DS) or " << WIIU_SAVEFILE_SIZE << " (Wii U).";
			setError(message.str());
			return false;
		}

		std::vector<uint8_t> fileBuffer((size_t) fileLength);
		fs.seekg(0, fs.beg);
		fs.read((char*) fileBuffer.data(), fileLength);
		if (!fs || fs.gcount() != fileLength)
		{
			setError("The save file could not be read completely.");
			return false;
		}
		fs.close();

		if (detectedFormat == SAVE_FORMAT_WIIU &&
			!(fileBuffer[0x1c] == 0x00 && fileBuffer[0x1d] == 0x00 &&
			  fileBuffer[0x1e] == 0x8a && fileBuffer[0x1f] == 0x00))
		{
			setError("The file size matches Wii U, but its header is invalid.");
			return false;
		}

		buffer.swap(fileBuffer);
		saveFormat = detectedFormat;
		dataOffset = detectedDataOffset;
		this->filename = input;

		cdelete(savedata);
		savedata = new save_t();
		std::memset(savedata, 0, sizeof(save_t));

		savedata->sex = buffer[physicalOffset(SEX_OFFSET)];
		savedata->face = buffer[physicalOffset(FACE_OFFSET)];
		savedata->hair = buffer[physicalOffset(HAIR_OFFSET)];
		std::memcpy(savedata->name, &buffer[physicalOffset(NAME_OFFSET)], nameSize());
		savedata->name[nameSize()] = 0;
		savedata->money = readUInt32(MONEY_OFFSET);
		savedata->voice = buffer[physicalOffset(VOICE_OFFSET)];

		for (uint32_t i = 0; i < 3; i++)
		{
			for (uint32_t j = 0; j < 8; j++)
			{
				uint32_t pos = INVENTORY_OFFSET + ITEM_SIZE * (j + i * 8);
				savedata->inventory[i][j].id = readUInt16(pos);
				savedata->inventory[i][j].count = readUInt16(pos + SHORT_SIZE);
			}
		}

		for (uint32_t i = 0; i < 4; i++)
		{
			for (uint32_t j = 0; j < 8; j++)
			{
				uint32_t pos = POUCH_OFFSET + ITEM_SIZE * (j + i * 8);
				savedata->pouch[i][j].id = readUInt16(pos);
				savedata->pouch[i][j].count = readUInt16(pos + SHORT_SIZE);
			}
		}
	
		for (uint32_t i = 0; i < 10; i++)
		{
			for (uint32_t j = 0; j < 100; j++)
			{
				uint32_t pos = CHEST_OFFSET + ITEM_SIZE * (j + i * 100);
				savedata->chest[i][j].id = readUInt16(pos);
				savedata->chest[i][j].count = readUInt16(pos + SHORT_SIZE);
			}
		}

		for (uint32_t i = 0; i < 10; i++)
		{
			for (uint32_t j = 0; j < 100; j++)
			{
				uint32_t pos = physicalOffset(BOX_OFFSET + EQUIPMENT_SIZE * (j + i * 100));
				std::memcpy(savedata->box[i][j], &buffer[pos], EQUIPMENT_SIZE);
				if (saveFormat == SAVE_FORMAT_WIIU)
				{
					swapEquipmentByteOrder(savedata->box[i][j]);
				}
			}
		}

		savedata->mogapoint = readUInt32(MOGAPOINT_OFFSET);
		
		return true;
	}
	catch (const std::exception &e)
	{
		cdelete(savedata);
		buffer.clear();
		saveFormat = SAVE_FORMAT_UNKNOWN;
		dataOffset = 0;
		setError(std::string("Problem loading save: ") + e.what());
		return false;
	}
}


bool MH3U_SE::save()
{
	return save(filename);
}


bool MH3U_SE::save(std::string output)
{
	std::ofstream fs;
	errorMessage.clear();

	try
	{
		if (!writeBuffer())
		{
			if (errorMessage.empty()) setError("No save is currently loaded.");
			return false;
		}

		fs.open(output.c_str(), std::fstream::out | std::fstream::binary | std::fstream::trunc);

		if (!fs)
		{
			setError("Unable to open the output file for writing.");
			return false;
		}

		fs.write((char*) buffer.data(), buffer.size());
		fs.close();
		if (!fs)
		{
			setError("The save file could not be written completely.");
			return false;
		}

		filename = output;
		return true;
	}
	catch(const std::exception &e)
	{
		setError(std::string("Problem saving file: ") + e.what());
		return false;
	}
}


bool MH3U_SE::writeBuffer()
{
	if (!savedata)
	{
		return false;
	}
	
	editBuffer(SEX_OFFSET, (uint8_t*)&(savedata->sex), SEX_SIZE);
	editBuffer(FACE_OFFSET, (uint8_t*)&(savedata->face), FACE_SIZE);
	editBuffer(HAIR_OFFSET, (uint8_t*)&(savedata->hair), HAIR_SIZE);
	editBuffer(NAME_OFFSET, (uint8_t*)(savedata->name), nameSize());
	writeUInt32(MONEY_OFFSET, savedata->money);
	editBuffer(VOICE_OFFSET, (uint8_t*)&(savedata->voice), VOICE_SIZE);

	
	for (uint32_t i = 0; i < 3; i++)
	{
		for (uint32_t j = 0; j < 8; j++)
		{
			uint32_t pos = INVENTORY_OFFSET + ITEM_SIZE * (j + i * 8);
			writeUInt16(pos, savedata->inventory[i][j].id);
			writeUInt16(pos + SHORT_SIZE, savedata->inventory[i][j].count);
		}
	}

	
	for (uint32_t i = 0; i < 4; i++)
	{
		for (uint32_t j = 0; j < 8; j++)
		{
			uint32_t pos = POUCH_OFFSET + ITEM_SIZE * (j + i * 8);
			writeUInt16(pos, savedata->pouch[i][j].id);
			writeUInt16(pos + SHORT_SIZE, savedata->pouch[i][j].count);
		}
	}

	
	for (uint32_t i = 0; i < 10; i++)
	{
		for (uint32_t j = 0; j < 100; j++)
		{
			uint32_t pos = CHEST_OFFSET + ITEM_SIZE * (j + i * 100);
			writeUInt16(pos, savedata->chest[i][j].id);
			writeUInt16(pos + SHORT_SIZE, savedata->chest[i][j].count);
		}
	}


	for (uint32_t i = 0; i < 10; i++)
	{
		for (uint32_t j = 0; j < 100; j++)
		{
			equipment_t diskEquipment;
			std::memcpy(diskEquipment, savedata->box[i][j], EQUIPMENT_SIZE);
			if (saveFormat == SAVE_FORMAT_WIIU)
			{
				swapEquipmentByteOrder(diskEquipment);
			}
			editBuffer(BOX_OFFSET + EQUIPMENT_SIZE * (j + i * 100), diskEquipment, EQUIPMENT_SIZE);
		}
	}
	
	writeUInt32(MOGAPOINT_OFFSET, savedata->mogapoint);

	return errorMessage.empty();
}


uint32_t MH3U_SE::physicalOffset(uint32_t logicalPos) const
{
	return dataOffset + logicalPos;
}


uint16_t MH3U_SE::readUInt16(uint32_t logicalPos) const
{
	uint32_t pos = physicalOffset(logicalPos);
	if (saveFormat == SAVE_FORMAT_WIIU)
	{
		return ((uint16_t) buffer[pos] << 8) | buffer[pos + 1];
	}
	return buffer[pos] | ((uint16_t) buffer[pos + 1] << 8);
}


uint32_t MH3U_SE::readUInt32(uint32_t logicalPos) const
{
	uint32_t pos = physicalOffset(logicalPos);
	if (saveFormat == SAVE_FORMAT_WIIU)
	{
		return ((uint32_t) buffer[pos] << 24) |
		       ((uint32_t) buffer[pos + 1] << 16) |
		       ((uint32_t) buffer[pos + 2] << 8) |
		       buffer[pos + 3];
	}
	return buffer[pos] |
	       ((uint32_t) buffer[pos + 1] << 8) |
	       ((uint32_t) buffer[pos + 2] << 16) |
	       ((uint32_t) buffer[pos + 3] << 24);
}


void MH3U_SE::writeUInt16(uint32_t logicalPos, uint16_t value)
{
	uint8_t bytes[SHORT_SIZE];
	if (saveFormat == SAVE_FORMAT_WIIU)
	{
		bytes[0] = (value >> 8) & 0xff;
		bytes[1] = value & 0xff;
	}
	else
	{
		bytes[0] = value & 0xff;
		bytes[1] = (value >> 8) & 0xff;
	}
	editBuffer(logicalPos, bytes, SHORT_SIZE);
}


void MH3U_SE::writeUInt32(uint32_t logicalPos, uint32_t value)
{
	uint8_t bytes[INT_SIZE];
	if (saveFormat == SAVE_FORMAT_WIIU)
	{
		bytes[0] = (value >> 24) & 0xff;
		bytes[1] = (value >> 16) & 0xff;
		bytes[2] = (value >> 8) & 0xff;
		bytes[3] = value & 0xff;
	}
	else
	{
		bytes[0] = value & 0xff;
		bytes[1] = (value >> 8) & 0xff;
		bytes[2] = (value >> 16) & 0xff;
		bytes[3] = (value >> 24) & 0xff;
	}
	editBuffer(logicalPos, bytes, INT_SIZE);
}


void MH3U_SE::editBuffer(uint32_t logicalPos, const uint8_t* ptr, uint32_t size)
{
	if (!savedata || ptr == NULL) return;

	uint32_t pos = physicalOffset(logicalPos);
	if (pos > buffer.size() || size > buffer.size() - pos)
	{
		setError("An edited field is outside the save file bounds.");
		return;
	}
	std::copy(ptr, ptr + size, buffer.begin() + pos);
}


void MH3U_SE::setError(const std::string &message)
{
	errorMessage = message;
}


equipment_subtype_e MH3U_Armory::convertSubtype(uint8_t equipmentType)
{
	return MH3U_Armory::convertSubtype((equipment_type_e) equipmentType);
}


equipment_subtype_e MH3U_Armory::convertSubtype(equipment_type_e equipmentType)
{
	equipment_subtype_e subtype;

	switch (equipmentType)
	{
		case MH3U_Type::ChestType:
		case MH3U_Type::ArmsType:
		case MH3U_Type::WaistType:
		case MH3U_Type::LegsType:
		case MH3U_Type::HeadType:
		{
			subtype = MH3U_Type::ArmorSubtype;
			break;
		}
		case MH3U_Type::CharmType:
		{
			subtype = MH3U_Type::CharmSubtype;
			break;
		}
		case MH3U_Type::GSType:
		case MH3U_Type::SNSType:
		case MH3U_Type::HType:
		case MH3U_Type::LType:
		case MH3U_Type::HBGType:
		case MH3U_Type::LBGType:
		case MH3U_Type::LSType:
		case MH3U_Type::SAType:
		case MH3U_Type::GLType:
		case MH3U_Type::BowType:
		case MH3U_Type::DBType:
		case MH3U_Type::HHType:
		{
			subtype = MH3U_Type::WeaponSubtype;
			break;
		}
		case MH3U_Type::UnknowType:
		default:
		{
			subtype = MH3U_Type::NoneSubtype;
			break;
		}
	}

	return subtype;
}


armor_t MH3U_Armory::convertEquipmentToArmor(equipment_t &equipment)
{
	armor_t armor;

	armor.equipmentType = equipment[0];
	armor.upgradeLevel = equipment[1];
	armor.identifier = equipment[2] + equipment[3] * 0x100;
	armor.foo31 = equipment[4];
	armor.blueComponent = equipment[5];
	armor.greenComponent = equipment[6];
	armor.redComponent = equipment[7];
	armor.firstJewelIdentifier = equipment[8] + equipment[9] * 0x100;
	armor.secondJewelIdentifier = equipment[10] + equipment[11] * 0x100;
	armor.thirdJewelIdentifier = equipment[12] + equipment[13] * 0x100;
	armor.foo81 = equipment[14];
	armor.foo82 = equipment[15];

	return armor;
}


void MH3U_Armory::convertArmorToEquipment(armor_t &armor, equipment_t &equipment)
{
	(equipment[0]) = armor.equipmentType;
	(equipment[1]) = armor.upgradeLevel;
	(equipment[2]) = armor.identifier % 0x100;
	(equipment[3]) = armor.identifier / 0x100;
	(equipment[4]) = armor.foo31;
	(equipment[5]) = armor.blueComponent;
	(equipment[6]) = armor.greenComponent;
	(equipment[7]) = armor.redComponent;
	(equipment[8]) = armor.firstJewelIdentifier % 0x100;
	(equipment[9]) = armor.firstJewelIdentifier / 0x100;
	(equipment[10]) = armor.secondJewelIdentifier % 0x100;
	(equipment[11]) = armor.secondJewelIdentifier / 0x100;
	(equipment[12]) = armor.thirdJewelIdentifier % 0x100;
	(equipment[13]) = armor.thirdJewelIdentifier / 0x100;
	(equipment[14]) = armor.foo81;
	(equipment[15]) = armor.foo82;
}


charm_t MH3U_Armory::convertEquipmentToCharm(equipment_t &equipment)
{
	charm_t charm;

	charm.equipmentType = equipment[0];
	charm.slotsCount = equipment[1];
	charm.identifier = equipment[2] + equipment[3] * 0x100;
	charm.firstSkillIdentifier = equipment[4];
	charm.firstSkillValue = equipment[5];
	charm.secondSkillIdentifier = equipment[6];
	charm.secondSkillValue = equipment[7];
	charm.firstJewelIdentifier = equipment[8] + equipment[9] * 0x100;
	charm.secondJewelIdentifier = equipment[10] + equipment[11] * 0x100;
	charm.thirdJewelIdentifier = equipment[12] + equipment[13] * 0x100;
	charm.foo81 = equipment[14];
	charm.foo82 = equipment[15];

	return charm;
}


void MH3U_Armory::convertCharmToEquipment(charm_t &charm, equipment_t &equipment)
{
	(equipment[0]) = charm.equipmentType;
	(equipment[1]) = charm.slotsCount;
	(equipment[2]) = charm.identifier % 0x100;
	(equipment[3]) = charm.identifier / 0x100;
	(equipment[4]) = charm.firstSkillIdentifier;
	(equipment[5]) = charm.firstSkillValue;
	(equipment[6]) = charm.secondSkillIdentifier;
	(equipment[7]) = charm.secondSkillValue;
	(equipment[8]) = charm.firstJewelIdentifier % 0x100;
	(equipment[9]) = charm.firstJewelIdentifier / 0x100;
	(equipment[10]) = charm.secondJewelIdentifier % 0x100;
	(equipment[11]) = charm.secondJewelIdentifier / 0x100;
	(equipment[12]) = charm.thirdJewelIdentifier % 0x100;
	(equipment[13]) = charm.thirdJewelIdentifier / 0x100;
	(equipment[14]) = charm.foo81;
	(equipment[15]) = charm.foo82;
}


weapon_t MH3U_Armory::convertEquipmentToWeapon(equipment_t &equipment)
{
	weapon_t weapon;

	weapon.equipmentType = equipment[0];
	weapon.foo12 = equipment[1];
	weapon.identifier = equipment[2] + equipment[3] * 0x100;
	weapon.foo31 = equipment[4];
	weapon.foo32 = equipment[5];
	weapon.foo41 = equipment[6];
	weapon.foo42 = equipment[7];
	weapon.firstJewelIdentifier = equipment[8] + equipment[9] * 0x100;
	weapon.secondJewelIdentifier = equipment[10] + equipment[11] * 0x100;
	weapon.thirdJewelIdentifier = equipment[12] + equipment[13] * 0x100;
	weapon.foo81 = equipment[14];
	weapon.foo82 = equipment[15];

	return weapon;
}


void MH3U_Armory::convertWeaponToEquipment(weapon_t &weapon, equipment_t &equipment)
{
	(equipment[0]) = weapon.equipmentType;
	(equipment[1]) = weapon.foo12;
	(equipment[2]) = weapon.identifier % 0x100;
	(equipment[3]) = weapon.identifier / 0x100;
	(equipment[4]) = weapon.foo31;
	(equipment[5]) = weapon.foo32;
	(equipment[6]) = weapon.foo41;
	(equipment[7]) = weapon.foo42;
	(equipment[8]) = weapon.firstJewelIdentifier % 0x100;
	(equipment[9]) = weapon.firstJewelIdentifier / 0x100;
	(equipment[10]) = weapon.secondJewelIdentifier % 0x100;
	(equipment[11]) = weapon.secondJewelIdentifier / 0x100;
	(equipment[12]) = weapon.thirdJewelIdentifier % 0x100;
	(equipment[13]) = weapon.thirdJewelIdentifier / 0x100;
	(equipment[14]) = weapon.foo81;
	(equipment[15]) = weapon.foo82;
}

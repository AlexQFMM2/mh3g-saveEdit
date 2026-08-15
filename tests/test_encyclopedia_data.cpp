#include "encyclopedia_data.hpp"

#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        EncyclopediaRepository repository;
        require(repository.open(), repository.error().toStdString().c_str());
        const QVector<EncyclopediaArmorSet> sets = repository.armorSets();
        require(sets.size() == 331, "unexpected armor set count");
        int members = 0;
        for (const EncyclopediaArmorSet &set : sets) members += set.members.size();
        require(members == 1651, "unexpected armor member count");
        const EncyclopediaArmor leather = repository.armor(1);
        require(leather.dexId == 1 && leather.saveType == 5 && leather.saveId == 1
            && leather.part == "head" && leather.name.contains(QString::fromUtf8("轻皮")),
            "Leather Headgear mapping mismatch");
        require(!repository.armorMaterials(1).isEmpty(), "armor production materials missing");
        require(!repository.armorSkills(1).isEmpty(), "armor skill points missing");
        const EncyclopediaArmorSet leatherSet = repository.armorSet(leather.setId);
        require(leatherSet.members.size() == 5, "Leather set grouping mismatch");
        const EncyclopediaArmorModel femaleHead = repository.armorModel(1, "female");
        require(femaleHead.modelId == 1 && femaleHead.modelKey == "armor-f-pl001-head"
            && femaleHead.arcRelativePath == "armor-mod/f/pl001/f_helm001.arc",
            "female armor model mapping mismatch");
        require(repository.armor(14).maleModelId == 2 && repository.armor(72).maleModelId == 3,
            "confirmed armor models do not match ExeFS");
        const EncyclopediaCharacterModel face = repository.characterModel("male", "face", 0);
        require(face.modelKey == "character-m-face000"
            && face.arcRelativePath == "character-mod/m/face000/m_face000.arc",
            "male face model mapping mismatch");
        require(repository.armor(1650).setId != repository.armor(966).setId,
            "Sword Saint Earring was merged into Chakra set");
        std::cout << "encyclopedia v3 data tests passed" << std::endl;
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "test failed: " << error.what() << std::endl;
        return 1;
    }
}

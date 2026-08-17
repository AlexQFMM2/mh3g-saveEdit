#include "mh3u_ds.hpp"

#include "game_data_repository.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

lang_t MH3U_DS::_lang = LANG_NONE;
std::string MH3U_DS::_lastError;

dataset_t* MH3U_DS::_faces = NULL;
dataset_t* MH3U_DS::_hairs = NULL;
dataset_t* MH3U_DS::_sexs = NULL;
dataset_t* MH3U_DS::_voices = NULL;
dataset_t* MH3U_DS::_items = NULL;
dataset_t* MH3U_DS::_skills = NULL;
dataset_t* MH3U_DS::_jewels = NULL;
dataset_t* MH3U_DS::_equipmentTypes = NULL;
dataset_t* MH3U_DS::_chestArmors = NULL;
dataset_t* MH3U_DS::_armsArmors = NULL;
dataset_t* MH3U_DS::_waistArmors = NULL;
dataset_t* MH3U_DS::_legsArmors = NULL;
dataset_t* MH3U_DS::_headArmors = NULL;
dataset_t* MH3U_DS::_charms = NULL;
dataset_t* MH3U_DS::_gsWeapons = NULL;
dataset_t* MH3U_DS::_snsWeapons = NULL;
dataset_t* MH3U_DS::_hWeapons = NULL;
dataset_t* MH3U_DS::_lWeapons = NULL;
dataset_t* MH3U_DS::_hbgWeapons = NULL;
dataset_t* MH3U_DS::_lbgWeapons = NULL;
dataset_t* MH3U_DS::_lsWeapons = NULL;
dataset_t* MH3U_DS::_saWeapons = NULL;
dataset_t* MH3U_DS::_glWeapons = NULL;
dataset_t* MH3U_DS::_bowWeapons = NULL;
dataset_t* MH3U_DS::_dbWeapons = NULL;
dataset_t* MH3U_DS::_hhWeapons = NULL;

namespace
{
void clearDataset(dataset_t *&dataset)
{
    delete dataset;
    dataset = NULL;
}

QString locateDatabase()
{
    const QString appPath = QDir(QCoreApplication::applicationDirPath()).filePath("data/mh3g.sqlite");
    if (QFileInfo(appPath).isFile()) return appPath;
    const QString workingPath = QDir::current().filePath("data/mh3g.sqlite");
    if (QFileInfo(workingPath).isFile()) return workingPath;
    return appPath;
}
}

bool MH3U_DS::readData(const lang_t lang)
{
    if (lang == LANG_NONE) return false;
    deleteData();
    GameDataRepository &repository = GameDataRepository::instance();
    if (!repository.open(locateDatabase()))
    {
        _lastError = repository.errorString().toStdString();
        return false;
    }
    _faces = repository.characterOptions("face");
    _hairs = repository.characterOptions("hair");
    _sexs = repository.characterOptions("sex");
    _voices = repository.characterOptions("voice");
    _items = repository.items();
    _skills = repository.skills();
    _jewels = repository.decorations();
    _equipmentTypes = repository.equipmentTypes();
    _chestArmors = repository.equipmentNames(1);
    _armsArmors = repository.equipmentNames(2);
    _waistArmors = repository.equipmentNames(3);
    _legsArmors = repository.equipmentNames(4);
    _headArmors = repository.equipmentNames(5);
    _charms = repository.charmClasses();
    _gsWeapons = repository.equipmentNames(7);
    _snsWeapons = repository.equipmentNames(8);
    _hWeapons = repository.equipmentNames(9);
    _lWeapons = repository.equipmentNames(10);
    _hbgWeapons = repository.equipmentNames(11);
    _lbgWeapons = repository.equipmentNames(13);
    _lsWeapons = repository.equipmentNames(14);
    _saWeapons = repository.equipmentNames(15);
    _glWeapons = repository.equipmentNames(16);
    _bowWeapons = repository.equipmentNames(17);
    _dbWeapons = repository.equipmentNames(18);
    _hhWeapons = repository.equipmentNames(19);
    dataset_t *all[] = {_faces,_hairs,_sexs,_voices,_items,_skills,_jewels,_equipmentTypes,
        _chestArmors,_armsArmors,_waistArmors,_legsArmors,_headArmors,_charms,_gsWeapons,
        _snsWeapons,_hWeapons,_lWeapons,_hbgWeapons,_lbgWeapons,_lsWeapons,_saWeapons,
        _glWeapons,_bowWeapons,_dbWeapons,_hhWeapons};
    for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
    {
        if (all[i] == NULL)
        {
            _lastError = "SQLite query failed while loading a required data table.";
            deleteData();
            return false;
        }
    }
    _lang = lang;
    _lastError.clear();
    return true;
}

bool MH3U_DS::deleteData(void)
{
    clearDataset(_faces); clearDataset(_hairs); clearDataset(_sexs); clearDataset(_voices);
    clearDataset(_items); clearDataset(_skills); clearDataset(_jewels); clearDataset(_equipmentTypes);
    clearDataset(_chestArmors); clearDataset(_armsArmors); clearDataset(_waistArmors);
    clearDataset(_legsArmors); clearDataset(_headArmors); clearDataset(_charms);
    clearDataset(_gsWeapons); clearDataset(_snsWeapons); clearDataset(_hWeapons); clearDataset(_lWeapons);
    clearDataset(_hbgWeapons); clearDataset(_lbgWeapons); clearDataset(_lsWeapons); clearDataset(_saWeapons);
    clearDataset(_glWeapons); clearDataset(_bowWeapons); clearDataset(_dbWeapons); clearDataset(_hhWeapons);
    _lang = LANG_NONE;
    GameDataRepository::instance().close();
    return true;
}

lang_t MH3U_DS::lang(void) { return _lang; }
std::string MH3U_DS::lastError(void) { return _lastError; }
const dataset_t* MH3U_DS::faces(void) { return _faces; }
const dataset_t* MH3U_DS::hairs(void) { return _hairs; }
const dataset_t* MH3U_DS::sexs(void) { return _sexs; }
const dataset_t* MH3U_DS::voices(void) { return _voices; }
const dataset_t* MH3U_DS::items(void) { return _items; }
const dataset_t* MH3U_DS::skills(void) { return _skills; }
const dataset_t* MH3U_DS::jewels(void) { return _jewels; }
const dataset_t* MH3U_DS::equipmentTypes(void) { return _equipmentTypes; }
const dataset_t* MH3U_DS::chestArmors(void) { return _chestArmors; }
const dataset_t* MH3U_DS::armsArmors(void) { return _armsArmors; }
const dataset_t* MH3U_DS::waistArmors(void) { return _waistArmors; }
const dataset_t* MH3U_DS::legsArmors(void) { return _legsArmors; }
const dataset_t* MH3U_DS::headArmors(void) { return _headArmors; }
const dataset_t* MH3U_DS::charms(void) { return _charms; }
const dataset_t* MH3U_DS::gsWeapons(void) { return _gsWeapons; }
const dataset_t* MH3U_DS::snsWeapons(void) { return _snsWeapons; }
const dataset_t* MH3U_DS::hWeapons(void) { return _hWeapons; }
const dataset_t* MH3U_DS::lWeapons(void) { return _lWeapons; }
const dataset_t* MH3U_DS::hbgWeapons(void) { return _hbgWeapons; }
const dataset_t* MH3U_DS::lbgWeapons(void) { return _lbgWeapons; }
const dataset_t* MH3U_DS::lsWeapons(void) { return _lsWeapons; }
const dataset_t* MH3U_DS::saWeapons(void) { return _saWeapons; }
const dataset_t* MH3U_DS::glWeapons(void) { return _glWeapons; }
const dataset_t* MH3U_DS::bowWeapons(void) { return _bowWeapons; }
const dataset_t* MH3U_DS::dbWeapons(void) { return _dbWeapons; }
const dataset_t* MH3U_DS::hhWeapons(void) { return _hhWeapons; }

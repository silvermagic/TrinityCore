/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file DBCStores.cpp
 * @brief DBC（Database Client）数据存储系统实现文件
 *
 * 本文件实现了游戏客户端数据库文件的加载和查询功能。DBC 文件是魔兽世界客户端使用的
 * 数据存储格式，包含了游戏中大部分静态数据，如法术、物品、地图、区域等信息。
 *
 * 主要功能包括：
 * - 加载所有必需的 DBC 文件
 * - 建立各种查询索引以支持快速数据访问
 * - 提供便捷的数据查询接口
 *
 * DBC 文件在服务器启动时加载，运行时数据是只读的。
 */

#include "DBCStores.h"
#include "DBCFileLoader.h"
#include "DBCfmt.h"
#include "Containers.h"
#include "Errors.h"
#include "IteratorPair.h"
#include "Log.h"
#include "ObjectDefines.h"
#include "Regex.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
#include "Timer.h"

// 临时解决方案，避免 Windows.h 中的 GetClassName 宏冲突
#ifdef GetClassName
#undef GetClassName
#endif

/**
 * @typedef AreaFlagByAreaID
 * @brief 区域ID到区域标志的映射类型
 *
 * 用于通过区域ID快速查找对应的区域标志
 */
typedef std::map<uint16, uint32> AreaFlagByAreaID;

/**
 * @typedef AreaFlagByMapID
 * @brief 地图ID到区域标志的映射类型
 *
 * 用于通过地图ID快速查找对应的区域标志
 */
typedef std::map<uint32, uint32> AreaFlagByMapID;

/**
 * @typedef WMOAreaTableKey
 * @brief WMO（World Map Object）区域表键类型
 *
 * 三元组键，包含 WMO ID、名称集ID 和 WMO 组ID
 * 用于唯一标识一个 WMO 区域条目
 */
typedef std::tuple<int16, int8, int32> WMOAreaTableKey;

/**
 * @typedef WMOAreaInfoByTripple
 * @brief WMO 区域信息映射类型
 *
 * 使用三元组键索引 WMO 区域表条目，支持快速查找
 */
typedef std::map<WMOAreaTableKey, WMOAreaTableEntry const*> WMOAreaInfoByTripple;

typedef std::map<uint16, uint32> AreaFlagByAreaID;
typedef std::map<uint32, uint32> AreaFlagByMapID;

typedef std::tuple<int16, int8, int32> WMOAreaTableKey;
typedef std::map<WMOAreaTableKey, WMOAreaTableEntry const*> WMOAreaInfoByTripple;

/**
 * @defgroup DBCStores DBC 存储对象
 * @brief 全局 DBC 存储实例声明
 *
 * 这些全局变量存储从 DBC 文件加载的所有游戏数据。
 * 服务器启动时加载，运行期间为只读。
 * @{
 */

/// 区域表存储 - 包含所有游戏区域信息
DBCStorage <AreaTableEntry> sAreaTableStore(AreaTableEntryfmt);
/// 区域组存储 - 区域分组信息
DBCStorage <AreaGroupEntry> sAreaGroupStore(AreaGroupEntryfmt);
/// 区域兴趣点存储 - 地图上的兴趣点信息
DBCStorage <AreaPOIEntry> sAreaPOIStore(AreaPOIEntryfmt);

/// WMO 区域信息映射（按三元组索引）
static WMOAreaInfoByTripple sWMOAreaInfoByTripple;

/// 成就存储 - 成就定义
DBCStorage <AchievementEntry> sAchievementStore(Achievementfmt);
/// 成就条件存储 - 成就完成条件
DBCStorage <AchievementCriteriaEntry> sAchievementCriteriaStore(AchievementCriteriafmt);
/// 区域触发器存储 - 区域触发点信息
DBCStorage <AreaTriggerEntry> sAreaTriggerStore(AreaTriggerEntryfmt);
/// 拍卖行存储 - 拍卖行定义
DBCStorage <AuctionHouseEntry> sAuctionHouseStore(AuctionHouseEntryfmt);
/// 银行背包槽价格存储 - 银行槽位价格
DBCStorage <BankBagSlotPricesEntry> sBankBagSlotPricesStore(BankBagSlotPricesEntryfmt);
/// 禁用插件存储 - 被禁用的插件列表
DBCStorage <BannedAddOnsEntry> sBannedAddOnsStore(BannedAddOnsfmt);
/// 战场管理员列表存储 - 战场入口信息
DBCStorage <BattlemasterListEntry> sBattlemasterListStore(BattlemasterListEntryfmt);
/// 理发店样式存储 - 理发店造型选项
DBCStorage <BarberShopStyleEntry> sBarberShopStyleStore(BarberShopStyleEntryfmt);
/// 角色面部毛发样式存储 - 角色面部毛发选项
DBCStorage <CharacterFacialHairStylesEntry> sCharacterFacialHairStylesStore(CharacterFacialHairStylesfmt);
/// 角色面部毛发映射（按种族、性别、样式ID索引）
std::unordered_map<uint32, CharacterFacialHairStylesEntry const*> sCharFacialHairMap;
/// 角色部位存储 - 角色外观部位信息
DBCStorage <CharSectionsEntry> sCharSectionsStore(CharSectionsEntryfmt);
/// 角色部位映射（按类型、性别、种族索引）
std::unordered_multimap<uint32, CharSectionsEntry const*> sCharSectionMap;
/// 角色初始装备存储 - 新角色初始装备
DBCStorage <CharStartOutfitEntry> sCharStartOutfitStore(CharStartOutfitEntryfmt);
/// 角色初始装备映射（按种族、职业、性别索引）
std::map<uint32, CharStartOutfitEntry const*> sCharStartOutfitMap;
/// 角色头衔存储 - 角色可获得头衔
DBCStorage <CharTitlesEntry> sCharTitlesStore(CharTitlesEntryfmt);
/// 聊天频道存储 - 游戏内聊天频道定义
DBCStorage <ChatChannelsEntry> sChatChannelsStore(ChatChannelsEntryfmt);
/// 职业类存储 - 职业定义信息
DBCStorage <ChrClassesEntry> sChrClassesStore(ChrClassesEntryfmt);
/// 种族存储 - 种族定义信息
DBCStorage <ChrRacesEntry> sChrRacesStore(ChrRacesEntryfmt);
/// 过场动画相机存储 - 过场动画相机位置
DBCStorage <CinematicCameraEntry> sCinematicCameraStore(CinematicCameraEntryfmt);
/// 过场动画序列存储 - 过场动画序列
DBCStorage <CinematicSequencesEntry> sCinematicSequencesStore(CinematicSequencesEntryfmt);
/// 生物显示信息存储 - 生物模型显示信息
DBCStorage <CreatureDisplayInfoEntry> sCreatureDisplayInfoStore(CreatureDisplayInfofmt);
/// 生物显示信息额外数据存储 - 生物显示额外信息
DBCStorage <CreatureDisplayInfoExtraEntry> sCreatureDisplayInfoExtraStore(CreatureDisplayInfoExtrafmt);
/// 生物科存储 - 生物分类信息
DBCStorage <CreatureFamilyEntry> sCreatureFamilyStore(CreatureFamilyfmt);
/// 生物模型数据存储 - 生物模型信息
DBCStorage <CreatureModelDataEntry> sCreatureModelDataStore(CreatureModelDatafmt);
/// 生物法术数据存储 - 生物默认法术
DBCStorage <CreatureSpellDataEntry> sCreatureSpellDataStore(CreatureSpellDatafmt);
/// 生物类型存储 - 生物类型定义
DBCStorage <CreatureTypeEntry> sCreatureTypeStore(CreatureTypefmt);
/// 货币类型存储 - 游戏内货币定义
DBCStorage <CurrencyTypesEntry> sCurrencyTypesStore(CurrencyTypesfmt);

DBCStorage <DestructibleModelDataEntry> sDestructibleModelDataStore(DestructibleModelDatafmt);
DBCStorage <DungeonEncounterEntry> sDungeonEncounterStore(DungeonEncounterfmt);
DBCStorage <DurabilityQualityEntry> sDurabilityQualityStore(DurabilityQualityfmt);
DBCStorage <DurabilityCostsEntry> sDurabilityCostsStore(DurabilityCostsfmt);

DBCStorage <EmotesEntry> sEmotesStore(EmotesEntryfmt);
DBCStorage <EmotesTextEntry> sEmotesTextStore(EmotesTextEntryfmt);
typedef std::tuple<uint32, uint32, uint32> EmotesTextSoundKey;
static std::map<EmotesTextSoundKey, EmotesTextSoundEntry const*> sEmotesTextSoundMap;
DBCStorage <EmotesTextSoundEntry> sEmotesTextSoundStore(EmotesTextSoundEntryfmt);

typedef std::map<uint32, SimpleFactionsList> FactionTeamMap;
static FactionTeamMap sFactionTeamMap;
DBCStorage <FactionEntry> sFactionStore(FactionEntryfmt);
DBCStorage <FactionTemplateEntry> sFactionTemplateStore(FactionTemplateEntryfmt);

// Used exclusively for data validation
DBCStorage <GameObjectArtKitEntry> sGameObjectArtKitStore(GameObjectArtKitfmt);

DBCStorage <GameObjectDisplayInfoEntry> sGameObjectDisplayInfoStore(GameObjectDisplayInfofmt);
DBCStorage <GemPropertiesEntry> sGemPropertiesStore(GemPropertiesEntryfmt);
DBCStorage <GlyphPropertiesEntry> sGlyphPropertiesStore(GlyphPropertiesfmt);
DBCStorage <GlyphSlotEntry> sGlyphSlotStore(GlyphSlotfmt);

DBCStorage <GtBarberShopCostBaseEntry>    sGtBarberShopCostBaseStore(GtBarberShopCostBasefmt);
DBCStorage <GtCombatRatingsEntry>         sGtCombatRatingsStore(GtCombatRatingsfmt);
DBCStorage <GtChanceToMeleeCritBaseEntry> sGtChanceToMeleeCritBaseStore(GtChanceToMeleeCritBasefmt);
DBCStorage <GtChanceToMeleeCritEntry>     sGtChanceToMeleeCritStore(GtChanceToMeleeCritfmt);
DBCStorage <GtChanceToSpellCritBaseEntry> sGtChanceToSpellCritBaseStore(GtChanceToSpellCritBasefmt);
DBCStorage <GtChanceToSpellCritEntry>     sGtChanceToSpellCritStore(GtChanceToSpellCritfmt);
DBCStorage <GtNPCManaCostScalerEntry>     sGtNPCManaCostScalerStore(GtNPCManaCostScalerfmt);
DBCStorage <GtOCTClassCombatRatingScalarEntry> sGtOCTClassCombatRatingScalarStore(GtOCTClassCombatRatingScalarfmt);
DBCStorage <GtOCTRegenHPEntry>            sGtOCTRegenHPStore(GtOCTRegenHPfmt);
//DBCStorage <GtOCTRegenMPEntry>            sGtOCTRegenMPStore(GtOCTRegenMPfmt);  -- not used currently
DBCStorage <GtRegenHPPerSptEntry>         sGtRegenHPPerSptStore(GtRegenHPPerSptfmt);
DBCStorage <GtRegenMPPerSptEntry>         sGtRegenMPPerSptStore(GtRegenMPPerSptfmt);

DBCStorage <HolidaysEntry>                sHolidaysStore(Holidaysfmt);

DBCStorage <ItemEntry>                    sItemStore(Itemfmt);
DBCStorage <ItemBagFamilyEntry>           sItemBagFamilyStore(ItemBagFamilyfmt);
//DBCStorage <ItemCondExtCostsEntry> sItemCondExtCostsStore(ItemCondExtCostsEntryfmt);
//DBCStorage <ItemDisplayInfoEntry> sItemDisplayInfoStore(ItemDisplayTemplateEntryfmt); -- not used currently
DBCStorage <ItemExtendedCostEntry> sItemExtendedCostStore(ItemExtendedCostEntryfmt);
DBCStorage <ItemLimitCategoryEntry> sItemLimitCategoryStore(ItemLimitCategoryEntryfmt);
DBCStorage <ItemRandomPropertiesEntry> sItemRandomPropertiesStore(ItemRandomPropertiesfmt);
DBCStorage <ItemRandomSuffixEntry> sItemRandomSuffixStore(ItemRandomSuffixfmt);
DBCStorage <ItemSetEntry> sItemSetStore(ItemSetEntryfmt);

DBCStorage <LFGDungeonEntry> sLFGDungeonStore(LFGDungeonEntryfmt);
DBCStorage <LightEntry> sLightStore(LightEntryfmt);
DBCStorage <LiquidTypeEntry> sLiquidTypeStore(LiquidTypefmt);
DBCStorage <LockEntry> sLockStore(LockEntryfmt);

DBCStorage <MailTemplateEntry> sMailTemplateStore(MailTemplateEntryfmt);
DBCStorage <MapEntry> sMapStore(MapEntryfmt);

// DBC used only for initialization sMapDifficultyMap at startup.
DBCStorage <MapDifficultyEntry> sMapDifficultyStore(MapDifficultyEntryfmt); // only for loading
MapDifficultyMap sMapDifficultyMap;

DBCStorage <MovieEntry> sMovieStore(MovieEntryfmt);

DBCStorage<NamesProfanityEntry> sNamesProfanityStore(NamesProfanityEntryfmt);
DBCStorage<NamesReservedEntry> sNamesReservedStore(NamesReservedEntryfmt);
typedef std::array<std::vector<Trinity::wregex>, TOTAL_LOCALES> NameValidationRegexContainer;
NameValidationRegexContainer NamesProfaneValidators;
NameValidationRegexContainer NamesReservedValidators;

DBCStorage <OverrideSpellDataEntry> sOverrideSpellDataStore(OverrideSpellDatafmt);

DBCStorage <PowerDisplayEntry> sPowerDisplayStore(PowerDisplayfmt);
DBCStorage <PvPDifficultyEntry> sPvPDifficultyStore(PvPDifficultyfmt);

DBCStorage <QuestSortEntry> sQuestSortStore(QuestSortEntryfmt);
DBCStorage <QuestXPEntry>   sQuestXPStore(QuestXPfmt);
DBCStorage <QuestFactionRewEntry>  sQuestFactionRewardStore(QuestFactionRewardfmt);
DBCStorage <RandPropPointsEntry> sRandPropPointsStore(RandPropPointsfmt);
DBCStorage <ScalingStatDistributionEntry> sScalingStatDistributionStore(ScalingStatDistributionfmt);
DBCStorage <ScalingStatValuesEntry> sScalingStatValuesStore(ScalingStatValuesfmt);

DBCStorage <SkillLineEntry> sSkillLineStore(SkillLinefmt);
DBCStorage <SkillLineAbilityEntry> sSkillLineAbilityStore(SkillLineAbilityfmt);
DBCStorage <SkillRaceClassInfoEntry> sSkillRaceClassInfoStore(SkillRaceClassInfofmt);
std::unordered_map<uint32, std::vector<SkillLineAbilityEntry const*>> SkillLineAbilitiesBySkill;
SkillRaceClassInfoMap SkillRaceClassInfoBySkill;

DBCStorage <SkillTiersEntry> sSkillTiersStore(SkillTiersfmt);

DBCStorage <SoundEntriesEntry> sSoundEntriesStore(SoundEntriesfmt);

DBCStorage <SpellItemEnchantmentEntry> sSpellItemEnchantmentStore(SpellItemEnchantmentfmt);
DBCStorage <SpellItemEnchantmentConditionEntry> sSpellItemEnchantmentConditionStore(SpellItemEnchantmentConditionfmt);
DBCStorage <SpellEntry> sSpellStore(SpellEntryfmt);
PetFamilySpellsStore sPetFamilySpellsStore;

DBCStorage <SpellCastTimesEntry> sSpellCastTimesStore(SpellCastTimefmt);
DBCStorage <SpellCategoryEntry> sSpellCategoryStore(SpellCategoryfmt);
DBCStorage <SpellDifficultyEntry> sSpellDifficultyStore(SpellDifficultyfmt);
DBCStorage <SpellDurationEntry> sSpellDurationStore(SpellDurationfmt);
DBCStorage <SpellFocusObjectEntry> sSpellFocusObjectStore(SpellFocusObjectfmt);
DBCStorage <SpellRadiusEntry> sSpellRadiusStore(SpellRadiusfmt);
DBCStorage <SpellRangeEntry> sSpellRangeStore(SpellRangefmt);
DBCStorage <SpellRuneCostEntry> sSpellRuneCostStore(SpellRuneCostfmt);
DBCStorage <SpellShapeshiftFormEntry> sSpellShapeshiftFormStore(SpellShapeshiftFormfmt);
DBCStorage <SpellVisualEntry> sSpellVisualStore(SpellVisualfmt);
DBCStorage <StableSlotPricesEntry> sStableSlotPricesStore(StableSlotPricesfmt);
DBCStorage <SummonPropertiesEntry> sSummonPropertiesStore(SummonPropertiesfmt);
DBCStorage <TalentEntry> sTalentStore(TalentEntryfmt);
TalentSpellPosMap sTalentSpellPosMap;
std::unordered_set<uint32> sPetTalentSpells;
DBCStorage <TalentTabEntry> sTalentTabStore(TalentTabEntryfmt);

// store absolute bit position for first rank for talent inspect
static uint32 sTalentTabPages[MAX_CLASSES][3];

DBCStorage <TaxiNodesEntry> sTaxiNodesStore(TaxiNodesEntryfmt);
TaxiMask sTaxiNodesMask;
TaxiMask sOldContinentsNodesMask;
TaxiMask sHordeTaxiNodesMask;
TaxiMask sAllianceTaxiNodesMask;
TaxiMask sDeathKnightTaxiNodesMask;

// DBC used only for initialization sTaxiPathSetBySource at startup.
TaxiPathSetBySource sTaxiPathSetBySource;
DBCStorage <TaxiPathEntry> sTaxiPathStore(TaxiPathEntryfmt);

// DBC used only for initialization sTaxiPathNodeStore at startup.
TaxiPathNodesByPath sTaxiPathNodesByPath;
static DBCStorage <TaxiPathNodeEntry> sTaxiPathNodeStore(TaxiPathNodeEntryfmt);

DBCStorage <TeamContributionPointsEntry> sTeamContributionPointsStore(TeamContributionPointsfmt);
DBCStorage <TotemCategoryEntry> sTotemCategoryStore(TotemCategoryEntryfmt);
DBCStorage <TransportAnimationEntry> sTransportAnimationStore(TransportAnimationfmt);
DBCStorage <TransportRotationEntry> sTransportRotationStore(TransportRotationfmt);
DBCStorage <VehicleEntry> sVehicleStore(VehicleEntryfmt);
DBCStorage <VehicleSeatEntry> sVehicleSeatStore(VehicleSeatEntryfmt);
DBCStorage <WMOAreaTableEntry> sWMOAreaTableStore(WMOAreaTableEntryfmt);
DBCStorage <WorldMapAreaEntry> sWorldMapAreaStore(WorldMapAreaEntryfmt);
DBCStorage <WorldMapOverlayEntry> sWorldMapOverlayStore(WorldMapOverlayEntryfmt);
DBCStorage <WorldSafeLocsEntry> sWorldSafeLocsStore(WorldSafeLocsEntryfmt);

typedef std::list<std::string> StoreProblemList;

uint32 DBCFileCount = 0;

static bool LoadDBC_assert_print(uint32 fsize, uint32 rsize, const std::string& filename)
{
    TC_LOG_ERROR("misc", "Size of '{}' set by format string ({}) not equal size of C++ structure ({}).", filename, fsize, rsize);

    // ASSERT must fail after function call
    return false;
}

template<class T>
inline void LoadDBC(uint32& availableDbcLocales, StoreProblemList& errors, DBCStorage<T>& storage, std::string const& dbcPath, std::string const& filename,
                    char const* dbTable = nullptr, char const* dbFormat = nullptr, char const* dbIndexName = nullptr)
{
    // compatibility format and C++ structure sizes
    ASSERT(DBCFileLoader::GetFormatRecordSize(storage.GetFormat()) == sizeof(T) || LoadDBC_assert_print(DBCFileLoader::GetFormatRecordSize(storage.GetFormat()), sizeof(T), filename));

    ++DBCFileCount;
    std::string dbcFilename = dbcPath + filename;

    if (storage.Load(dbcFilename.c_str()))
    {
        for (uint8 i = 0; i < TOTAL_LOCALES; ++i)
        {
            if (!(availableDbcLocales & (1 << i)))
                continue;

            std::string localizedName(dbcPath);
            localizedName.append(localeNames[i]);
            localizedName.push_back('/');
            localizedName.append(filename);

            if (!storage.LoadStringsFrom(localizedName.c_str()))
                availableDbcLocales &= ~(1 << i);             // mark as not available for speedup next checks
        }

        if (dbTable)
            storage.LoadFromDB(dbTable, dbFormat, dbIndexName);
    }
    else
    {
        // sort problematic dbc to (1) non compatible and (2) non-existed
        if (FILE* f = fopen(dbcFilename.c_str(), "rb"))
        {
            std::ostringstream stream;
            stream << dbcFilename << " exists, and has " << storage.GetFieldCount() << " field(s) (expected " << strlen(storage.GetFormat()) << "). Extracted file might be from wrong client version or a database-update has been forgotten. Search on forum for TCE00008 for more info.";
            std::string buf = stream.str();
            errors.push_back(buf);
            fclose(f);
        }
        else
            errors.push_back(dbcFilename);
    }
}

/**
 * @brief 加载所有 DBC 存储数据
 *
 * 这是服务器启动时加载所有游戏静态数据的主要函数。
 * 加载所有必需的 DBC 文件，并建立各种辅助索引结构以支持快速查询。
 *
 * 加载流程：
 * 1. 加载所有标准 DBC 文件
 * 2. 加载需要数据库覆盖的 DBC 文件（成就、法术、法术难度）
 * 3. 建立角色外观相关的映射索引
 * 4. 构建阵营团队关系映射
 * 5. 修正游戏对象显示信息
 * 6. 建立地图难度数据映射
 * 7. 编译名字验证正则表达式
 * 8. 构建 PvP 难度分组数据
 * 9. 建立技能相关索引
 * 10. 构建法术难度查找表
 * 11. 建立天赋相关索引
 * 12. 构建出租车路径网络
 * 13. 建立 WMO 区域信息索引
 * 14. 验证 DBC 文件版本
 *
 * @param dataPath 数据目录路径，DBC 文件位于 dataPath/dbc/
 */
void LoadDBCStores(const std::string& dataPath)
{
    uint32 oldMSTime = getMSTime();

    std::string dbcPath = dataPath + "dbc/";

    StoreProblemList bad_dbc_files;
    uint32 availableDbcLocales = 0xFFFFFFFF;

    /// 加载标准 DBC 文件的宏
#define LOAD_DBC(store, file) LoadDBC(availableDbcLocales, bad_dbc_files, store, dbcPath, file)

    LOAD_DBC(sAreaTableStore,                     "AreaTable.dbc");
    LOAD_DBC(sAchievementCriteriaStore,           "Achievement_Criteria.dbc");
    LOAD_DBC(sAreaTriggerStore,                   "AreaTrigger.dbc");
    LOAD_DBC(sAreaGroupStore,                     "AreaGroup.dbc");
    LOAD_DBC(sAreaPOIStore,                       "AreaPOI.dbc");
    LOAD_DBC(sAuctionHouseStore,                  "AuctionHouse.dbc");
    LOAD_DBC(sBankBagSlotPricesStore,             "BankBagSlotPrices.dbc");
    LOAD_DBC(sBannedAddOnsStore,                  "BannedAddOns.dbc");
    LOAD_DBC(sBattlemasterListStore,              "BattlemasterList.dbc");
    LOAD_DBC(sBarberShopStyleStore,               "BarberShopStyle.dbc");
    LOAD_DBC(sCharacterFacialHairStylesStore,     "CharacterFacialHairStyles.dbc");
    LOAD_DBC(sCharSectionsStore,                  "CharSections.dbc");
    LOAD_DBC(sCharStartOutfitStore,               "CharStartOutfit.dbc");
    LOAD_DBC(sCharTitlesStore,                    "CharTitles.dbc");
    LOAD_DBC(sChatChannelsStore,                  "ChatChannels.dbc");
    LOAD_DBC(sChrClassesStore,                    "ChrClasses.dbc");
    LOAD_DBC(sChrRacesStore,                      "ChrRaces.dbc");
    LOAD_DBC(sCinematicCameraStore,               "CinematicCamera.dbc");
    LOAD_DBC(sCinematicSequencesStore,            "CinematicSequences.dbc");
    LOAD_DBC(sCreatureDisplayInfoStore,           "CreatureDisplayInfo.dbc");
    LOAD_DBC(sCreatureDisplayInfoExtraStore,      "CreatureDisplayInfoExtra.dbc");
    LOAD_DBC(sCreatureFamilyStore,                "CreatureFamily.dbc");
    LOAD_DBC(sCreatureModelDataStore,             "CreatureModelData.dbc");
    LOAD_DBC(sCreatureSpellDataStore,             "CreatureSpellData.dbc");
    LOAD_DBC(sCreatureTypeStore,                  "CreatureType.dbc");
    LOAD_DBC(sCurrencyTypesStore,                 "CurrencyTypes.dbc");
    LOAD_DBC(sDestructibleModelDataStore,         "DestructibleModelData.dbc");
    LOAD_DBC(sDungeonEncounterStore,              "DungeonEncounter.dbc");
    LOAD_DBC(sDurabilityCostsStore,               "DurabilityCosts.dbc");
    LOAD_DBC(sDurabilityQualityStore,             "DurabilityQuality.dbc");
    LOAD_DBC(sEmotesStore,                        "Emotes.dbc");
    LOAD_DBC(sEmotesTextStore,                    "EmotesText.dbc");
    LOAD_DBC(sEmotesTextSoundStore,               "EmotesTextSound.dbc");
    LOAD_DBC(sFactionStore,                       "Faction.dbc");
    LOAD_DBC(sFactionTemplateStore,               "FactionTemplate.dbc");
    LOAD_DBC(sGameObjectArtKitStore,              "GameObjectArtKit.dbc");
    LOAD_DBC(sGameObjectDisplayInfoStore,         "GameObjectDisplayInfo.dbc");
    LOAD_DBC(sGemPropertiesStore,                 "GemProperties.dbc");
    LOAD_DBC(sGlyphPropertiesStore,               "GlyphProperties.dbc");
    LOAD_DBC(sGlyphSlotStore,                     "GlyphSlot.dbc");
    LOAD_DBC(sGtBarberShopCostBaseStore,          "gtBarberShopCostBase.dbc");
    LOAD_DBC(sGtCombatRatingsStore,               "gtCombatRatings.dbc");
    LOAD_DBC(sGtChanceToMeleeCritBaseStore,       "gtChanceToMeleeCritBase.dbc");
    LOAD_DBC(sGtChanceToMeleeCritStore,           "gtChanceToMeleeCrit.dbc");
    LOAD_DBC(sGtChanceToSpellCritBaseStore,       "gtChanceToSpellCritBase.dbc");
    LOAD_DBC(sGtChanceToSpellCritStore,           "gtChanceToSpellCrit.dbc");
    LOAD_DBC(sGtNPCManaCostScalerStore,           "gtNPCManaCostScaler.dbc");
    LOAD_DBC(sGtOCTClassCombatRatingScalarStore,  "gtOCTClassCombatRatingScalar.dbc");
    LOAD_DBC(sGtOCTRegenHPStore,                  "gtOCTRegenHP.dbc");
    //LOAD_DBC(sGtOCTRegenMPStore,                  "gtOCTRegenMP.dbc");       -- not used currently
    LOAD_DBC(sGtRegenHPPerSptStore,               "gtRegenHPPerSpt.dbc");
    LOAD_DBC(sGtRegenMPPerSptStore,               "gtRegenMPPerSpt.dbc");
    LOAD_DBC(sHolidaysStore,                      "Holidays.dbc");
    LOAD_DBC(sItemStore,                          "Item.dbc");
    LOAD_DBC(sItemBagFamilyStore,                 "ItemBagFamily.dbc");
    //LOAD_DBC(sItemDisplayInfoStore,               "ItemDisplayInfo.dbc");     -- not used currently
    //LOAD_DBC(sItemCondExtCostsStore,              "ItemCondExtCosts.dbc");
    LOAD_DBC(sItemExtendedCostStore,              "ItemExtendedCost.dbc");
    LOAD_DBC(sItemLimitCategoryStore,             "ItemLimitCategory.dbc");
    LOAD_DBC(sItemRandomPropertiesStore,          "ItemRandomProperties.dbc");
    LOAD_DBC(sItemRandomSuffixStore,              "ItemRandomSuffix.dbc");
    LOAD_DBC(sItemSetStore,                       "ItemSet.dbc");
    LOAD_DBC(sLFGDungeonStore,                    "LFGDungeons.dbc");
    LOAD_DBC(sLightStore,                         "Light.dbc");
    LOAD_DBC(sLiquidTypeStore,                    "LiquidType.dbc");
    LOAD_DBC(sLockStore,                          "Lock.dbc");
    LOAD_DBC(sMailTemplateStore,                  "MailTemplate.dbc");
    LOAD_DBC(sMapStore,                           "Map.dbc");
    LOAD_DBC(sMapDifficultyStore,                 "MapDifficulty.dbc");
    LOAD_DBC(sMovieStore,                         "Movie.dbc");
    LOAD_DBC(sNamesProfanityStore,                "NamesProfanity.dbc");
    LOAD_DBC(sNamesReservedStore,                 "NamesReserved.dbc");
    LOAD_DBC(sOverrideSpellDataStore,             "OverrideSpellData.dbc");
    LOAD_DBC(sPowerDisplayStore,                  "PowerDisplay.dbc");
    LOAD_DBC(sPvPDifficultyStore,                 "PvpDifficulty.dbc");
    LOAD_DBC(sQuestXPStore,                       "QuestXP.dbc");
    LOAD_DBC(sQuestFactionRewardStore,            "QuestFactionReward.dbc");
    LOAD_DBC(sQuestSortStore,                     "QuestSort.dbc");
    LOAD_DBC(sRandPropPointsStore,                "RandPropPoints.dbc");
    LOAD_DBC(sScalingStatDistributionStore,       "ScalingStatDistribution.dbc");
    LOAD_DBC(sScalingStatValuesStore,             "ScalingStatValues.dbc");
    LOAD_DBC(sSkillLineStore,                     "SkillLine.dbc");
    LOAD_DBC(sSkillLineAbilityStore,              "SkillLineAbility.dbc");
    LOAD_DBC(sSkillRaceClassInfoStore,            "SkillRaceClassInfo.dbc");
    LOAD_DBC(sSkillTiersStore,                    "SkillTiers.dbc");
    LOAD_DBC(sSoundEntriesStore,                  "SoundEntries.dbc");
    LOAD_DBC(sSpellCastTimesStore,                "SpellCastTimes.dbc");
    LOAD_DBC(sSpellCategoryStore,                 "SpellCategory.dbc");
    LOAD_DBC(sSpellDurationStore,                 "SpellDuration.dbc");
    LOAD_DBC(sSpellFocusObjectStore,              "SpellFocusObject.dbc");
    LOAD_DBC(sSpellItemEnchantmentStore,          "SpellItemEnchantment.dbc");
    LOAD_DBC(sSpellItemEnchantmentConditionStore, "SpellItemEnchantmentCondition.dbc");
    LOAD_DBC(sSpellRadiusStore,                   "SpellRadius.dbc");
    LOAD_DBC(sSpellRangeStore,                    "SpellRange.dbc");
    LOAD_DBC(sSpellRuneCostStore,                 "SpellRuneCost.dbc");
    LOAD_DBC(sSpellShapeshiftFormStore,           "SpellShapeshiftForm.dbc");
    LOAD_DBC(sSpellVisualStore,                   "SpellVisual.dbc");
    LOAD_DBC(sStableSlotPricesStore,              "StableSlotPrices.dbc");
    LOAD_DBC(sSummonPropertiesStore,              "SummonProperties.dbc");
    LOAD_DBC(sTalentStore,                        "Talent.dbc");
    LOAD_DBC(sTalentTabStore,                     "TalentTab.dbc");
    LOAD_DBC(sTaxiNodesStore,                     "TaxiNodes.dbc");
    LOAD_DBC(sTaxiPathStore,                      "TaxiPath.dbc");
    LOAD_DBC(sTaxiPathNodeStore,                  "TaxiPathNode.dbc");
    LOAD_DBC(sTeamContributionPointsStore,        "TeamContributionPoints.dbc");
    LOAD_DBC(sTotemCategoryStore,                 "TotemCategory.dbc");
    LOAD_DBC(sTransportAnimationStore,            "TransportAnimation.dbc");
    LOAD_DBC(sTransportRotationStore,             "TransportRotation.dbc");
    LOAD_DBC(sVehicleStore,                       "Vehicle.dbc");
    LOAD_DBC(sVehicleSeatStore,                   "VehicleSeat.dbc");
    LOAD_DBC(sWMOAreaTableStore,                  "WMOAreaTable.dbc");
    LOAD_DBC(sWorldMapAreaStore,                  "WorldMapArea.dbc");
    LOAD_DBC(sWorldMapOverlayStore,               "WorldMapOverlay.dbc");
    LOAD_DBC(sWorldSafeLocsStore,                 "WorldSafeLocs.dbc");

#undef LOAD_DBC

    /// 加载需要数据库覆盖的 DBC 文件的宏
#define LOAD_DBC_EXT(store, file, dbtable, dbformat, dbpk) LoadDBC(availableDbcLocales, bad_dbc_files, store, dbcPath, file, dbtable, dbformat, dbpk)

    // 加载可从数据库覆盖的 DBC 文件（允许服务器自定义修改）
    LOAD_DBC_EXT(sAchievementStore,     "Achievement.dbc",      "achievement_dbc",      CustomAchievementfmt,     CustomAchievementIndex);
    LOAD_DBC_EXT(sSpellStore,           "Spell.dbc",            "spell_dbc",            CustomSpellEntryfmt,      CustomSpellEntryIndex);
    LOAD_DBC_EXT(sSpellDifficultyStore, "SpellDifficulty.dbc",  "spelldifficulty_dbc",  CustomSpellDifficultyfmt, CustomSpellDifficultyIndex);

#undef LOAD_DBC_EXT

    // 构建角色面部毛发样式映射（忽略不可玩种族）
    for (CharacterFacialHairStylesEntry const* entry : sCharacterFacialHairStylesStore)
        if (entry->RaceID && ((1 << (entry->RaceID - 1)) & RACEMASK_ALL_PLAYABLE) != 0) // ignore nonplayable races
            sCharFacialHairMap.insert({ entry->RaceID | (entry->SexID << 8) | (entry->VariationID << 16), entry });

    // 构建角色部位映射（忽略不可玩种族）
    for (CharSectionsEntry const* entry : sCharSectionsStore)
        if (entry->RaceID && ((1 << (entry->RaceID - 1)) & RACEMASK_ALL_PLAYABLE) != 0) // ignore nonplayable races
            sCharSectionMap.insert({ entry->BaseSection | (entry->SexID << 8) | (entry->RaceID << 16), entry });

    // 构建角色初始装备映射
    for (CharStartOutfitEntry const* outfit : sCharStartOutfitStore)
        sCharStartOutfitMap[outfit->RaceID | (outfit->ClassID << 8) | (outfit->SexID << 16)] = outfit;

    // 构建表情声音映射
    for (EmotesTextSoundEntry const* entry : sEmotesTextSoundStore)
        sEmotesTextSoundMap[EmotesTextSoundKey(entry->EmotesTextID, entry->RaceID, entry->SexID)] = entry;

    // 构建阵营团队关系映射（父阵营 -> 子阵营列表）
    for (FactionEntry const* faction : sFactionStore)
    {
        if (faction->ParentFactionID)
        {
            SimpleFactionsList& flist = sFactionTeamMap[faction->ParentFactionID];
            flist.push_back(faction->ID);
        }
    }

    // 修正游戏对象显示信息的边界框（确保最大值大于最小值）
    for (GameObjectDisplayInfoEntry const* info : sGameObjectDisplayInfoStore)
    {
        if (info->GeoBoxMax.X < info->GeoBoxMin.X)
            std::swap(*(float*)(&info->GeoBoxMax.X), *(float*)(&info->GeoBoxMin.X));
        if (info->GeoBoxMax.Y < info->GeoBoxMin.Y)
            std::swap(*(float*)(&info->GeoBoxMax.Y), *(float*)(&info->GeoBoxMin.Y));
        if (info->GeoBoxMax.Z < info->GeoBoxMin.Z)
            std::swap(*(float*)(&info->GeoBoxMax.Z), *(float*)(&info->GeoBoxMin.Z));
    }

    // 填充地图难度映射
    for (MapDifficultyEntry const* entry : sMapDifficultyStore)
        sMapDifficultyMap[MAKE_PAIR32(entry->MapID, entry->Difficulty)] = MapDifficulty(entry->RaidDuration, entry->MaxPlayers, entry->Message[0] != '\0');

    // 编译亵渎名字正则表达式验证器（用于过滤不当名字）
    for (NamesProfanityEntry const* namesProfanity : sNamesProfanityStore)
    {
        ASSERT(namesProfanity->Language < TOTAL_LOCALES || namesProfanity->Language == -1);
        std::wstring wname;
        bool conversionResult = Utf8toWStr(namesProfanity->Name, wname);
        ASSERT(conversionResult);

        if (namesProfanity->Language != -1)
            NamesProfaneValidators[namesProfanity->Language].emplace_back(wname, Trinity::regex::perl | Trinity::regex::icase | Trinity::regex::optimize);
        else
            for (uint32 i = 0; i < TOTAL_LOCALES; ++i)
                NamesProfaneValidators[i].emplace_back(wname, Trinity::regex::perl | Trinity::regex::icase | Trinity::regex::optimize);
    }

    // 编译保留名字正则表达式验证器（用于过滤保留的名字）
    for (NamesReservedEntry const* namesReserved : sNamesReservedStore)
    {
        ASSERT(namesReserved->Language < TOTAL_LOCALES || namesReserved->Language == -1);
        std::wstring wname;
        bool conversionResult = Utf8toWStr(namesReserved->Name, wname);
        ASSERT(conversionResult);

        if (namesReserved->Language != -1)
            NamesReservedValidators[namesReserved->Language].emplace_back(wname, Trinity::regex::perl | Trinity::regex::icase | Trinity::regex::optimize);
        else
            for (uint32 i = 0; i < TOTAL_LOCALES; ++i)
                NamesReservedValidators[i].emplace_back(wname, Trinity::regex::perl | Trinity::regex::icase | Trinity::regex::optimize);
    }

    // 验证 PvP 难度分组索引在有效范围内
    for (PvPDifficultyEntry const* entry : sPvPDifficultyStore)
    {
        ASSERT(entry->RangeIndex < MAX_BATTLEGROUND_BRACKETS, "PvpDifficulty bracket (%d) exceeded max allowed value (%d)", entry->RangeIndex, MAX_BATTLEGROUND_BRACKETS);
    }

    // 构建技能种族职业信息映射
    for (SkillRaceClassInfoEntry const* entry : sSkillRaceClassInfoStore)
        if (sSkillLineStore.LookupEntry(entry->SkillID))
            SkillRaceClassInfoBySkill.emplace(entry->SkillID, entry);

    // 构建技能线能力映射和宠物科法术集合
    for (SkillLineAbilityEntry const* skillLine : sSkillLineAbilityStore)
    {
        SpellEntry const* spellInfo = sSpellStore.LookupEntry(skillLine->Spell);
        // 收集被动宠物法术到宠物科法术存储
        if (spellInfo && spellInfo->Attributes & SPELL_ATTR0_PASSIVE)
        {
            for (CreatureFamilyEntry const* cFamily : sCreatureFamilyStore)
            {
                if (skillLine->SkillLine != cFamily->SkillLine[0] && skillLine->SkillLine != cFamily->SkillLine[1])
                    continue;
                if (spellInfo->SpellLevel)
                    continue;

                if (skillLine->AcquireMethod != SKILL_LINE_ABILITY_LEARNED_ON_SKILL_LEARN)
                    continue;

                sPetFamilySpellsStore[cFamily->ID].insert(spellInfo->ID);
            }
        }

        SkillLineAbilitiesBySkill[skillLine->SkillLine].push_back(skillLine);
    }

    // 创建法术难度查找器，建立法术ID到难度ID的映射
    for (SpellDifficultyEntry const* spellDiff : sSpellDifficultyStore)
    {
        SpellDifficultyEntry newEntry;
        memset(newEntry.DifficultySpellID, 0, 4*sizeof(uint32));
        for (uint8 x = 0; x < MAX_DIFFICULTY; ++x)
        {
            if (spellDiff->DifficultySpellID[x] <= 0 || !sSpellStore.LookupEntry(spellDiff->DifficultySpellID[x]))
            {
                if (spellDiff->DifficultySpellID[x] > 0)//don't show error if spell is <= 0, not all modes have spells and there are unknown negative values
                    TC_LOG_ERROR("sql.sql", "spelldifficulty_dbc: spell {} at field id:{} at spellid{} does not exist in SpellStore (spell.dbc), loaded as 0", spellDiff->DifficultySpellID[x], spellDiff->ID, x);
                newEntry.DifficultySpellID[x] = 0;//spell was <= 0 or invalid, set to 0
            }
            else
                newEntry.DifficultySpellID[x] = spellDiff->DifficultySpellID[x];
        }
        if (newEntry.DifficultySpellID[0] <= 0 || newEntry.DifficultySpellID[1] <= 0)//id0-1 must be always set!
            continue;

        for (uint8 x = 0; x < MAX_DIFFICULTY; ++x)
            if (newEntry.DifficultySpellID[x])
                sSpellMgr->SetSpellDifficultyId(uint32(newEntry.DifficultySpellID[x]), spellDiff->ID);
    }

    // 创建天赋法术集合，建立法术ID到天赋位置的映射
    for (TalentEntry const* talentInfo : sTalentStore)
    {
        TalentTabEntry const* talentTab = sTalentTabStore.LookupEntry(talentInfo->TabID);
        for (uint8 j = 0; j < MAX_TALENT_RANK; ++j)
        {
            if (talentInfo->SpellRank[j])
            {
                sTalentSpellPosMap[talentInfo->SpellRank[j]] = TalentSpellPos(talentInfo->ID, j);
                if (talentTab && talentTab->PetTalentMask)
                    sPetTalentSpells.insert(talentInfo->SpellRank[j]);
            }
        }
    }

    // 准备天赋等级位位置数据，用于查看其他玩家天赋时的快速访问
    {
        // 现在拥有所有最大等级（以及用于存储查看天赋时天赋等级的位数量）
        for (TalentTabEntry const* talentTabInfo : sTalentTabStore)
        {
            // 防止内存损坏；否则 cls 会变成 12
            if ((talentTabInfo->ClassMask & CLASSMASK_ALL_PLAYABLE) == 0)
                continue;

            // 存储职业天赋标签页
            for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
                if (talentTabInfo->ClassMask & (1 << (cls - 1)))
                    sTalentTabPages[cls][talentTabInfo->OrderIndex] = talentTabInfo->ID;
        }
    }

    // 构建出租车路径索引（起点 -> 终点 -> 路径信息）
    for (TaxiPathEntry const* entry : sTaxiPathStore)
        sTaxiPathSetBySource[entry->FromTaxiNode][entry->ToTaxiNode] = TaxiPathBySourceAndDestination(entry->ID, entry->Cost);

    uint32 pathCount = sTaxiPathStore.GetNumRows();
    // 计算路径节点数量
    std::vector<uint32> pathLength;
    pathLength.resize(pathCount);                           // 0 和其他索引未使用
    for (TaxiPathNodeEntry const* entry : sTaxiPathNodeStore)
    {
        if (pathLength[entry->PathID] < entry->NodeIndex + 1)
            pathLength[entry->PathID] = entry->NodeIndex + 1;
    }

    // 设置路径长度
    sTaxiPathNodesByPath.resize(pathCount);                 // 0 和其他索引未使用
    for (uint32 i = 1; i < sTaxiPathNodesByPath.size(); ++i)
        sTaxiPathNodesByPath[i].resize(pathLength[i]);
    // 填充数据
    for (TaxiPathNodeEntry const* entry : sTaxiPathNodeStore)
        sTaxiPathNodesByPath[entry->PathID][entry->NodeIndex] = entry;

    // 初始化全局出租车节点掩码
    // 包含至少有一条非法术基础（脚本化）路径的现有节点
    {
        std::set<uint32> spellPaths;
        // 收集所有法术触发的出租车路径
        for (SpellEntry const* sInfo : sSpellStore)
            for (uint8 j = 0; j < MAX_SPELL_EFFECTS; ++j)
                if (sInfo->Effect[j] == SPELL_EFFECT_SEND_TAXI)
                    spellPaths.insert(sInfo->EffectMiscValue[j]);

        sTaxiNodesMask.fill(0);
        sOldContinentsNodesMask.fill(0);
        sHordeTaxiNodesMask.fill(0);
        sAllianceTaxiNodesMask.fill(0);
        sDeathKnightTaxiNodesMask.fill(0);
        for (TaxiNodesEntry const* node : sTaxiNodesStore)
        {
            TaxiPathSetBySource::const_iterator src_i = sTaxiPathSetBySource.find(node->ID);
            if (src_i != sTaxiPathSetBySource.end() && !src_i->second.empty())
            {
                bool ok = false;
                for (TaxiPathSetForSource::const_iterator dest_i = src_i->second.begin(); dest_i != src_i->second.end(); ++dest_i)
                {
                    // 非法术路径
                    if (dest_i->second.price || spellPaths.find(dest_i->second.ID) == spellPaths.end())
                    {
                        ok = true;
                        break;
                    }
                }

                if (!ok)
                    continue;
            }

            // 有效的出租车网络节点
            uint8  field   = (uint8)((node->ID - 1) / 32);
            uint32 submask = 1  <<  ((node->ID - 1) % 32);
            sTaxiNodesMask[field] |= submask;

            // 根据坐骑生物ID设置阵营掩码
            if (node->MountCreatureID[0] && node->MountCreatureID[0] != 32981)
                sHordeTaxiNodesMask[field] |= submask;
            if (node->MountCreatureID[1] && node->MountCreatureID[1] != 32981)
                sAllianceTaxiNodesMask[field] |= submask;
            if (node->MountCreatureID[0] == 32981 || node->MountCreatureID[1] == 32981)
                sDeathKnightTaxiNodesMask[field] |= submask;

            // 旧大陆节点（+虚拟位于旧大陆的节点，显式检查以避免加载地图文件获取区域信息）
            if (node->ContinentID < 2 || node->ID == 82 || node->ID == 83 || node->ID == 93 || node->ID == 94)
                sOldContinentsNodesMask[field] |= submask;

            // 修复阿彻鲁斯和暗影穹顶飞行管理员的死亡骑士节点
            if (node->ID == 315 || node->ID == 333)
                const_cast<TaxiNodesEntry*>(node)->MountCreatureID[1] = 32981;
        }
    }

    // 建立 WMO 区域信息索引
    for (WMOAreaTableEntry const* entry : sWMOAreaTableStore)
        sWMOAreaInfoByTripple[WMOAreaTableKey(entry->WMOID, entry->NameSetID, entry->WMOGroupID)] = entry;

    // 错误检查
    if (bad_dbc_files.size() >= DBCFileCount)
    {
        TC_LOG_ERROR("misc", "Incorrect DataDir value in worldserver.conf or ALL required *.dbc files ({}) not found by path: {}dbc", DBCFileCount, dataPath);
        exit(1);
    }
    else if (!bad_dbc_files.empty())
    {
        std::string str;
        for (StoreProblemList::iterator i = bad_dbc_files.begin(); i != bad_dbc_files.end(); ++i)
            str += *i + "\n";

        TC_LOG_ERROR("misc", "Some required *.dbc files ({} from {}) not found or not compatible:\n{}", (uint32)bad_dbc_files.size(), DBCFileCount, str);
        exit(1);
    }

    // 检查加载的 DBC 文件版本是否正确（通过检查 3.3.5a 最后添加的条目）
    if (!sAreaTableStore.LookupEntry(4987)         ||       // 3.3.5a 最后添加的区域
        !sCharTitlesStore.LookupEntry(177)         ||       // 3.3.5a 最后添加的角色头衔
        !sGemPropertiesStore.LookupEntry(1629)     ||       // 3.3.5a 最后添加的宝石属性
        !sItemStore.LookupEntry(56806)             ||       // 3.3.5a 客户端最后已知的物品
        !sItemExtendedCostStore.LookupEntry(2997)  ||       // 3.3.5a 最后添加的物品扩展花费
        !sMapStore.LookupEntry(724)                ||       // 3.3.5a 最后添加的地图
        !sSpellStore.LookupEntry(80864)            )        // 3.3.5a 最后添加的法术
    {
        TC_LOG_ERROR("misc", "You have _outdated_ DBC files. Please extract correct versions from current using client.");
        exit(1);
    }

    TC_LOG_INFO("server.loading", ">> Initialized {} data stores in {} ms", DBCFileCount, GetMSTimeDiffToNow(oldMSTime));

}

/**
 * @brief 获取阵营团队列表
 *
 * 返回指定阵营的所有下属阵营ID列表。
 * 用于处理阵营关系，如阵营声望等。
 *
 * @param faction 父阵营ID
 * @return 指向下属阵营列表的指针，如果阵营没有下属阵营则返回 nullptr
 */
SimpleFactionsList const* GetFactionTeamList(uint32 faction)
{
    FactionTeamMap::const_iterator itr = sFactionTeamMap.find(faction);
    if (itr != sFactionTeamMap.end())
        return &itr->second;

    return nullptr;
}

/**
 * @brief 获取宠物名称
 *
 * 根据宠物科和语言获取宠物类型的名称。
 *
 * @param petfamily 宠物科ID（来自 CreatureFamily.dbc）
 * @param dbclang 数据库语言索引
 * @return 宠物名称字符串，如果无效则返回 nullptr
 */
char const* GetPetName(uint32 petfamily, uint32 dbclang)
{
    if (!petfamily)
        return nullptr;
    CreatureFamilyEntry const* pet_family = sCreatureFamilyStore.LookupEntry(petfamily);
    if (!pet_family)
        return nullptr;
    return pet_family->Name[dbclang];
}

/**
 * @brief 获取天赋法术位置
 *
 * 根据法术ID查找对应的天赋位置信息（天赋ID和等级）。
 *
 * @param spellId 法术ID
 * @return 指向天赋法术位置结构的指针，如果法术不是天赋则返回 nullptr
 */
TalentSpellPos const* GetTalentSpellPos(uint32 spellId)
{
    TalentSpellPosMap::const_iterator itr = sTalentSpellPosMap.find(spellId);
    if (itr == sTalentSpellPosMap.end())
        return nullptr;

    return &itr->second;
}

/**
 * @brief 获取天赋法术消耗
 *
 * 返回学习天赋法术所需的天赋点数。
 * 天赋点消耗 = 等级 + 1（等级从0开始）。
 *
 * @param spellId 法术ID
 * @return 天赋点消耗，如果法术不是天赋则返回 0
 */
uint32 GetTalentSpellCost(uint32 spellId)
{
    if (TalentSpellPos const* pos = GetTalentSpellPos(spellId))
        return pos->rank+1;

    return 0;
}

/**
 * @brief 通过三元组获取 WMO 区域表条目
 *
 * 使用 WMO ID、名称集ID 和 WMO 组ID 查找 WMO 区域信息。
 * WMO（World Map Object）是世界地图对象的缩写，用于室内区域。
 *
 * @param rootid WMO 根ID
 * @param adtid ADT（地图块）ID
 * @param groupid WMO 组ID
 * @return 指向 WMO 区域表条目的指针，如果未找到则返回 nullptr
 */
WMOAreaTableEntry const* GetWMOAreaTableEntryByTripple(int32 rootid, int32 adtid, int32 groupid)
{
    auto i = sWMOAreaInfoByTripple.find(WMOAreaTableKey(int16(rootid), int8(adtid), groupid));
    if (i != sWMOAreaInfoByTripple.end())
        return i->second;

    return nullptr;
}

/**
 * @brief 获取种族名称
 *
 * 根据种族ID和语言获取种族的本地化名称。
 *
 * @param race 种族ID
 * @param locale 语言区域索引
 * @return 种族名称字符串，如果无效则返回 nullptr
 */
char const* GetRaceName(uint8 race, uint8 locale)
{
    ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race);
    return raceEntry ? raceEntry->Name[locale] : nullptr;
}

/**
 * @brief 获取职业名称
 *
 * 根据职业ID和语言获取职业的本地化名称。
 *
 * @param class_ 职业ID
 * @param locale 语言区域索引
 * @return 职业名称字符串，如果无效则返回 nullptr
 */
char const* GetClassName(uint8 class_, uint8 locale)
{
    ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(class_);
    return classEntry ? classEntry->Name[locale] : nullptr;
}

/**
 * @brief 获取地图和区域对应的虚拟地图ID
 *
 * 对于外域（530）和诺森德（571）地图，根据区域ID查找显示地图ID。
 * 这用于在地图界面显示正确的背景地图。
 *
 * @param mapid 原始地图ID
 * @param zoneId 区域ID
 * @return 虚拟/显示地图ID
 */
uint32 GetVirtualMapForMapAndZone(uint32 mapid, uint32 zoneId)
{
    if (mapid != 530 && mapid != 571)                        // speed for most cases
        return mapid;

    if (WorldMapAreaEntry const* wma = sWorldMapAreaStore.LookupEntry(zoneId))
        return wma->DisplayMapID >= 0 ? wma->DisplayMapID : wma->MapID;

    return mapid;
}

/**
 * @brief 获取地图和区域对应的内容等级
 *
 * 根据地图ID和区域ID确定内容的等级范围。
 * 用于确定玩家在不同区域应该获得的奖励等级。
 *
 * @param mapid 地图ID
 * @param zoneId 区域ID
 * @return 内容等级枚举值
 */
ContentLevels GetContentLevelsForMapAndZone(uint32 mapid, uint32 zoneId)
{
    mapid = GetVirtualMapForMapAndZone(mapid, zoneId);
    if (mapid < 2)
        return CONTENT_1_60;

    MapEntry const* mapEntry = sMapStore.LookupEntry(mapid);
    if (!mapEntry)
        return CONTENT_1_60;

    switch (mapEntry->Expansion())
    {
        default: return CONTENT_1_60;
        case 1:  return CONTENT_61_70;
        case 2:  return CONTENT_71_80;
    }
}

/**
 * @brief 检查图腾类别是否兼容
 *
 * 判断物品的图腾类别是否满足要求的图腾类别。
 * 用于武器和技能的图腾需求检查。
 *
 * @param itemTotemCategoryId 物品的图腾类别ID
 * @param requiredTotemCategoryId 要求的图腾类别ID
 * @return 如果兼容返回 true，否则返回 false
 */
bool IsTotemCategoryCompatiableWith(uint32 itemTotemCategoryId, uint32 requiredTotemCategoryId)
{
    if (requiredTotemCategoryId == 0)
        return true;
    if (itemTotemCategoryId == 0)
        return false;

    TotemCategoryEntry const* itemEntry = sTotemCategoryStore.LookupEntry(itemTotemCategoryId);
    if (!itemEntry)
        return false;
    TotemCategoryEntry const* reqEntry = sTotemCategoryStore.LookupEntry(requiredTotemCategoryId);
    if (!reqEntry)
        return false;

    if (itemEntry->TotemCategoryType != reqEntry->TotemCategoryType)
        return false;

    return (itemEntry->TotemCategoryMask & reqEntry->TotemCategoryMask) == reqEntry->TotemCategoryMask;
}

/**
 * @brief 将区域坐标转换为地图坐标
 *
 * 将世界地图上的区域坐标（0-100百分比）转换为游戏世界坐标。
 * 用于在小地图和世界地图之间转换坐标。
 *
 * @param x X坐标（输入/输出）
 * @param y Y坐标（输入/输出）
 * @param zone 区域ID
 */
void Zone2MapCoordinates(float& x, float& y, uint32 zone)
{
    WorldMapAreaEntry const* maEntry = sWorldMapAreaStore.LookupEntry(zone);

    // if not listed then map coordinates (instance)
    if (!maEntry)
        return;

    std::swap(x, y);                                         // at client map coords swapped
    x = x*((maEntry->LocBottom-maEntry->LocTop)/100)+maEntry->LocTop;
    y = y*((maEntry->LocRight-maEntry->LocLeft)/100)+maEntry->LocLeft;      // client y coord from top to down
}

/**
 * @brief 将地图坐标转换为区域坐标
 *
 * 将游戏世界坐标转换为世界地图上的区域坐标（0-100百分比）。
 * 用于在小地图和世界地图之间转换坐标。
 *
 * @param x X坐标（输入/输出）
 * @param y Y坐标（输入/输出）
 * @param zone 区域ID
 */
void Map2ZoneCoordinates(float& x, float& y, uint32 zone)
{
    WorldMapAreaEntry const* maEntry = sWorldMapAreaStore.LookupEntry(zone);

    // if not listed then map coordinates (instance)
    if (!maEntry)
        return;

    x = (x-maEntry->LocTop)/((maEntry->LocBottom-maEntry->LocTop)/100);
    y = (y-maEntry->LocLeft)/((maEntry->LocRight-maEntry->LocLeft)/100);    // client y coord from top to down
    std::swap(x, y);                                         // client have map coords swapped
}

/**
 * @brief 获取地图难度数据
 *
 * 根据地图ID和难度获取地图难度配置信息。
 *
 * @param mapId 地图ID
 * @param difficulty 难度枚举值
 * @return 指向地图难度结构的指针，如果未找到则返回 nullptr
 */
MapDifficulty const* GetMapDifficultyData(uint32 mapId, Difficulty difficulty)
{
    MapDifficultyMap::const_iterator itr = sMapDifficultyMap.find(MAKE_PAIR32(mapId, difficulty));
    return itr != sMapDifficultyMap.end() ? &itr->second : nullptr;
}

/**
 * @brief 获取降级的地图难度数据
 *
 * 如果请求的难度不存在，尝试降级到较低难度。
 * 例如：英雄 -> 普通 -> 10人普通
 *
 * @param mapId 地图ID
 * @param difficulty 难度枚举值（输入/输出，可能被降级）
 * @return 指向地图难度结构的指针，如果未找到则返回 nullptr
 */
MapDifficulty const* GetDownscaledMapDifficultyData(uint32 mapId, Difficulty &difficulty)
{
    uint32 tmpDiff = difficulty;
    MapDifficulty const* mapDiff = GetMapDifficultyData(mapId, Difficulty(tmpDiff));
    if (!mapDiff)
    {
        if (tmpDiff > RAID_DIFFICULTY_25MAN_NORMAL) // heroic, downscale to normal
            tmpDiff -= 2;
        else
            tmpDiff -= 1;   // any non-normal mode for raids like tbc (only one mode)

        // pull new data
        mapDiff = GetMapDifficultyData(mapId, Difficulty(tmpDiff)); // we are 10 normal or 25 normal
        if (!mapDiff)
        {
            tmpDiff -= 1;
            mapDiff = GetMapDifficultyData(mapId, Difficulty(tmpDiff)); // 10 normal
        }
    }

    difficulty = Difficulty(tmpDiff);
    return mapDiff;
}

/**
 * @brief 根据等级获取战场分组
 *
 * 根据玩家等级和战场地图ID查找对应的 PvP 难度分组。
 * 如果玩家等级超过最大定义的等级，返回最高等级的分组。
 *
 * @param mapid 战场地图ID
 * @param level 玩家等级
 * @return 指向 PvP 难度条目的指针，如果未找到则返回 nullptr
 */
PvPDifficultyEntry const* GetBattlegroundBracketByLevel(uint32 mapid, uint32 level)
{
    PvPDifficultyEntry const* maxEntry = nullptr;              // used for level > max listed level case
    for (uint32 i = 0; i < sPvPDifficultyStore.GetNumRows(); ++i)
    {
        if (PvPDifficultyEntry const* entry = sPvPDifficultyStore.LookupEntry(i))
        {
            // skip unrelated and too-high brackets
            if (entry->MapID != mapid || entry->MinLevel > level)
                continue;

            // exactly fit
            if (entry->MaxLevel >= level)
                return entry;

            // remember for possible out-of-range case (search higher from existed)
            if (!maxEntry || maxEntry->MaxLevel < entry->MaxLevel)
                maxEntry = entry;
        }
    }

    return maxEntry;
}

/**
 * @brief 根据 ID 获取战场分组
 *
 * 根据战场地图ID和分组ID查找对应的 PvP 难度条目。
 *
 * @param mapid 战场地图ID
 * @param id 战场分组ID
 * @return 指向 PvP 难度条目的指针，如果未找到则返回 nullptr
 */
PvPDifficultyEntry const* GetBattlegroundBracketById(uint32 mapid, BattlegroundBracketId id)
{
    for (uint32 i = 0; i < sPvPDifficultyStore.GetNumRows(); ++i)
        if (PvPDifficultyEntry const* entry = sPvPDifficultyStore.LookupEntry(i))
            if (entry->MapID == mapid && entry->GetBracketId() == id)
                return entry;

    return nullptr;
}

/**
 * @brief 获取天赋标签页数组
 *
 * 返回指定职业的天赋标签页ID数组。
 * 数组包含三个元素，对应三个天赋树。
 *
 * @param cls 职业ID
 * @return 指向天赋标签页ID数组的指针
 */
uint32 const* GetTalentTabPages(uint8 cls)
{
    return sTalentTabPages[cls];
}

/**
 * @brief 获取液体类型标志
 *
 * 根据液体类型ID获取液体标志位。
 * 用于确定液体的声音类型和视觉特征。
 *
 * @param liquidType 液体类型ID
 * @return 液体标志位，如果液体类型无效则返回 0
 */
uint32 GetLiquidFlags(uint32 liquidType)
{
    if (LiquidTypeEntry const* liq = sLiquidTypeStore.LookupEntry(liquidType))
        return 1 << liq->SoundBank;

    return 0;
}

/**
 * @brief 获取角色面部毛发条目
 *
 * 根据种族、性别和面部毛发ID查找角色面部毛发样式条目。
 *
 * @param race 种族ID
 * @param gender 性别（0=男性，1=女性）
 * @param facialHairID 面部毛发样式ID
 * @return 指向角色面部毛发样式条目的指针，如果未找到则返回 nullptr
 */
CharacterFacialHairStylesEntry const* GetCharFacialHairEntry(uint8 race, uint8 gender, uint8 facialHairID)
{
    auto itr = sCharFacialHairMap.find(uint32(race) | uint32(gender << 8) | uint32(facialHairID << 16));
    if (itr == sCharFacialHairMap.end())
        return nullptr;

    return itr->second;
}

/**
 * @brief 获取角色部位条目
 *
 * 根据种族、部位类型、性别、变体索引和颜色索引查找角色部位条目。
 * 用于角色外观自定义。
 *
 * @param race 种族ID
 * @param genType 部位类型枚举
 * @param gender 性别（0=男性，1=女性）
 * @param type 变体索引
 * @param color 颜色索引
 * @return 指向角色部位条目的指针，如果未找到则返回 nullptr
 */
CharSectionsEntry const* GetCharSectionEntry(uint8 race, CharSectionType genType, uint8 gender, uint8 type, uint8 color)
{
    uint32 const key = uint32(genType) | uint32(gender << 8) | uint32(race << 16);
    for (auto const& section : Trinity::Containers::MapEqualRange(sCharSectionMap, key))
    {
        if (section.second->VariationIndex == type && section.second->ColorIndex == color)
            return section.second;
    }

    return nullptr;
}

/**
 * @brief 获取角色初始装备条目
 *
 * 根据种族、职业和性别查找新角色的初始装备配置。
 *
 * @param race 种族ID
 * @param class_ 职业ID
 * @param gender 性别（0=男性，1=女性）
 * @return 指向角色初始装备条目的指针，如果未找到则返回 nullptr
 */
CharStartOutfitEntry const* GetCharStartOutfitEntry(uint8 race, uint8 class_, uint8 gender)
{
    std::map<uint32, CharStartOutfitEntry const*>::const_iterator itr = sCharStartOutfitMap.find(race | (class_ << 8) | (gender << 16));
    if (itr == sCharStartOutfitMap.end())
        return nullptr;

    return itr->second;
}

/**
 * @brief 获取随机副本条目
 *
 * 根据地图ID和难度获取随机副本（LFG）条目。
 * 如果多个副本使用同一地图（如血色修道院），返回第一个找到的条目。
 *
 * @param mapId 地图ID
 * @param difficulty 难度枚举值
 * @return 指向 LFG 副本条目的指针，如果未找到则返回 nullptr
 */
LFGDungeonEntry const* GetLFGDungeon(uint32 mapId, Difficulty difficulty)
{
    for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
    {
        LFGDungeonEntry const* dungeon = sLFGDungeonStore.LookupEntry(i);
        if (!dungeon)
            continue;

        if (dungeon->MapID == int32(mapId) && Difficulty(dungeon->Difficulty) == difficulty)
            return dungeon;
    }

    return nullptr;
}

/**
 * @brief 获取地图默认光照
 *
 * 获取指定地图的默认光照ID。
 * 默认光照是坐标在原点（0,0,0）的光源。
 *
 * @param mapId 地图ID
 * @return 光照ID，如果未找到则返回 0
 */
uint32 GetDefaultMapLight(uint32 mapId)
{
    for (int32 i = sLightStore.GetNumRows(); i >= 0; --i)
    {
        LightEntry const* light = sLightStore.LookupEntry(uint32(i));
        if (!light)
            continue;

        if (light->ContinentID == mapId && light->GameCoords.X == 0.0f && light->GameCoords.Y == 0.0f && light->GameCoords.Z == 0.0f)
            return light->ID;
    }

    return 0;
}

/**
 * @brief 获取技能对应的能力列表
 *
 * 返回指定技能ID关联的所有技能能力条目。
 *
 * @param skillLine 技能ID
 * @return 指向技能能力条目向量的指针，如果技能没有能力则返回 nullptr
 */
std::vector<SkillLineAbilityEntry const*> const* GetSkillLineAbilitiesBySkill(uint32 skillLine)
{
    return Trinity::Containers::MapGetValuePtr(SkillLineAbilitiesBySkill, skillLine);
}

/**
 * @brief 获取技能种族职业信息
 *
 * 查找指定技能对特定种族和职业的限制信息。
 * 用于确定某个种族/职业组合是否可以学习该技能。
 *
 * @param skill 技能ID
 * @param race 种族ID
 * @param class_ 职业ID
 * @return 指向技能种族职业信息条目的指针，如果不适用则返回 nullptr
 */
SkillRaceClassInfoEntry const* GetSkillRaceClassInfo(uint32 skill, uint8 race, uint8 class_)
{
    SkillRaceClassInfoBounds bounds = SkillRaceClassInfoBySkill.equal_range(skill);
    for (SkillRaceClassInfoMap::iterator itr = bounds.first; itr != bounds.second; ++itr)
    {
        if (itr->second->RaceMask && !(itr->second->RaceMask & (1 << (race - 1))))
            continue;
        if (itr->second->ClassMask && !(itr->second->ClassMask & (1 << (class_ - 1))))
            continue;

        return itr->second;
    }

    return nullptr;
}

/**
 * @brief 验证角色名称
 *
 * 检查角色名称是否包含亵渎词汇或保留词汇。
 * 根据语言区域使用相应的正则表达式进行验证。
 *
 * @param name 要验证的名称（宽字符串）
 * @param locale 语言区域常量
 * @return 响应代码：CHAR_NAME_SUCCESS（成功）、CHAR_NAME_PROFANE（亵渎）或 CHAR_NAME_RESERVED（保留）
 */
ResponseCodes ValidateName(std::wstring const& name, LocaleConstant locale)
{
    if (locale >= TOTAL_LOCALES)
        return RESPONSE_FAILURE;

    for (Trinity::wregex const& regex : NamesProfaneValidators[locale])
        if (Trinity::regex_search(name, regex))
            return CHAR_NAME_PROFANE;

    // regexes at TOTAL_LOCALES are loaded from NamesReserved which is not locale specific
    for (Trinity::wregex const& regex : NamesReservedValidators[locale])
        if (Trinity::regex_search(name, regex))
            return CHAR_NAME_RESERVED;

    return CHAR_NAME_SUCCESS;
}

/**
 * @brief 查找表情对应的声效
 *
 * 根据表情ID、种族和性别查找对应的表情声效条目。
 *
 * @param emote 表情文本ID
 * @param race 种族ID
 * @param gender 性别ID
 * @return 指向表情文本声效条目的指针，如果未找到则返回 nullptr
 */
EmotesTextSoundEntry const* FindTextSoundEmoteFor(uint32 emote, uint32 race, uint32 gender)
{
    auto itr = sEmotesTextSoundMap.find(EmotesTextSoundKey(emote, race, gender));
    return itr != sEmotesTextSoundMap.end() ? itr->second : nullptr;
}

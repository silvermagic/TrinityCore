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

#ifndef TRINITY_DBCSTORES_H
#define TRINITY_DBCSTORES_H

/**
 * @file DBCStores.h
 * @brief DBC（数据库客户端）数据存储系统头文件
 *
 * 本文件声明了用于加载、存储和访问客户端数据库文件（DBC）的系统和接口。
 * DBC 文件包含游戏的静态数据，如法术信息、物品数据、地图信息、区域数据等。
 *
 * 主要功能包括：
 * - 定义各种 DBC 数据的存储容器
 * - 提供访问 DBC 数据的辅助函数
 * - 管理游戏静态数据的加载和查询
 *
 * DBC 存储系统是游戏服务器的核心数据源之一，提供了游戏运行所需的基础数据。
 */

#include "DBCStore.h"
#include "DBCStructure.h"
#include "SharedDefines.h"
#include <list>
#include <map>
#include <unordered_map>
#include <unordered_set>

enum LocaleConstant : uint8;

/**
 * @brief 临时解决方案，避免 Windows.h 中的宏定义冲突
 *
 * Windows.h 中定义了 GetClassName 宏，与游戏中的 GetClassName 函数冲突。
 * 在此处取消该宏定义，确保后续代码可以正常使用 GetClassName 函数名。
 */
#ifdef GetClassName
#undef GetClassName
#endif

/**
 * @typedef SimpleFactionsList
 * @brief 简单阵营列表类型，用于存储阵营ID列表
 */
typedef std::list<uint32> SimpleFactionsList;

/**
 * @brief 获取指定阵营的团队阵营列表
 * @param faction 阵营ID
 * @return 返回阵营团队列表的指针，如果不存在则返回 nullptr
 *
 * 该函数返回包含指定阵营所属团队的所有阵营ID的列表。
 * 例如，某些阵营可能属于同一个阵营团队（如部落阵营、联盟阵营等）。
 */
TC_GAME_API SimpleFactionsList const* GetFactionTeamList(uint32 faction);

/**
 * @brief 获取宠物名称
 * @param petfamily 宠物家族ID
 * @param dbclang DBC语言标识
 * @return 返回宠物名称的字符串指针
 *
 * 根据宠物家族和语言设置返回对应的宠物名称。
 */
TC_GAME_API char const* GetPetName(uint32 petfamily, uint32 dbclang);

/**
 * @brief 获取天赋法术的消耗点数
 * @param spellId 法术ID
 * @return 返回该天赋法术需要的天赋点数
 *
 * 该函数查询天赋树中某个法术需要消耗的天赋点数。
 */
TC_GAME_API uint32 GetTalentSpellCost(uint32 spellId);

/**
 * @brief 获取天赋法术在天赋树中的位置信息
 * @param spellId 法术ID
 * @return 返回天赋法术位置信息的指针，如果不是天赋法术则返回 nullptr
 *
 * 该函数返回指定法术在天赋树中的位置，包括天赋树标签页和位置索引。
 */
TC_GAME_API TalentSpellPos const* GetTalentSpellPos(uint32 spellId);

/**
 * @brief 获取种族名称
 * @param race 种族ID（RaceIds）
 * @param locale 语言标识（LocaleConstant）
 * @return 返回种族名称的本地化字符串指针
 *
 * 根据种族ID和语言设置返回对应的种族名称（如"人类"、"兽人"等）。
 */
TC_GAME_API char const* GetRaceName(uint8 race, uint8 locale);

/**
 * @brief 获取职业名称
 * @param class_ 职业ID（Classes）
 * @param locale 语言标识（LocaleConstant）
 * @return 返回职业名称的本地化字符串指针
 *
 * 根据职业ID和语言设置返回对应的职业名称（如"战士"、"法师"等）。
 */
TC_GAME_API char const* GetClassName(uint8 class_, uint8 locale);

/**
 * @brief 通过三元组获取WMO（世界模型对象）区域表条目
 * @param rootid WMO根ID
 * @param adtid ADT文件ID
 * @param groupid WMO组ID
 * @return 返回对应的WMO区域表条目，如果未找到则返回 nullptr
 *
 * WMO是游戏中的复杂建筑和地形对象，该函数用于定位特定的WMO区域数据。
 */
TC_GAME_API WMOAreaTableEntry const* GetWMOAreaTableEntryByTripple(int32 rootid, int32 adtid, int32 groupid);

/**
 * @brief 获取指定地图和区域的虚拟地图ID
 * @param mapid 地图ID
 * @param zoneId 区域ID
 * @return 返回虚拟地图ID
 *
 * 某些地图（如副本）可能需要映射到其他地图ID，此函数处理这种映射关系。
 */
TC_GAME_API uint32 GetVirtualMapForMapAndZone(uint32 mapid, uint32 zoneId);

/**
 * @enum ContentLevels
 * @brief 游戏内容等级枚举
 *
 * 定义游戏内容的不同等级范围，用于区分不同资料片的内容。
 */
enum ContentLevels : uint8
{
    CONTENT_1_60 = 0,   ///< 经典旧世内容（1-60级）
    CONTENT_61_70,      ///< 燃烧的远征内容（61-70级）
    CONTENT_71_80       ///< 巫妖王之怒内容（71-80级）
};

/**
 * @brief 获取指定地图和区域的内容等级
 * @param mapid 地图ID
 * @param zoneId 区域ID
 * @return 返回内容等级枚举值
 *
 * 根据地图和区域信息确定其属于哪个资料片的内容范围。
 */
TC_GAME_API ContentLevels GetContentLevelsForMapAndZone(uint32 mapid, uint32 zoneId);

/**
 * @brief 检查物品图腾类别是否与所需图腾类别兼容
 * @param itemTotemCategoryId 物品的图腾类别ID
 * @param requiredTotemCategoryId 所需的图腾类别ID
 * @return 如果兼容返回 true，否则返回 false
 *
 * 某些物品或技能需要特定类型的图腾才能使用，此函数检查兼容性。
 */
TC_GAME_API bool IsTotemCategoryCompatiableWith(uint32 itemTotemCategoryId, uint32 requiredTotemCategoryId);

/**
 * @brief 将区域坐标转换为地图坐标
 * @param x [输入/输出] X坐标
 * @param y [输入/输出] Y坐标
 * @param zone 区域ID
 *
 * 区域坐标系统和地图坐标系统可能不同，此函数进行转换。
 * 坐标参数会被原地修改。
 */
TC_GAME_API void Zone2MapCoordinates(float &x, float &y, uint32 zone);

/**
 * @brief 将地图坐标转换为区域坐标
 * @param x [输入/输出] X坐标
 * @param y [输入/输出] Y坐标
 * @param zone 区域ID
 *
 * 地图坐标系统和区域坐标系统可能不同，此函数进行转换。
 * 坐标参数会被原地修改。
 */
TC_GAME_API void Map2ZoneCoordinates(float &x, float &y, uint32 zone);

/**
 * @typedef MapDifficultyMap
 * @brief 地图难度映射类型
 *
 * 键为 map 和 difficulty 的组合（使用 pair32），值为 MapDifficulty 结构。
 * 用于存储各个地图在不同难度下的配置信息。
 */
typedef std::map<uint32/*pair32(map, diff)*/, MapDifficulty> MapDifficultyMap;

/**
 * @brief 获取指定地图和难度的难度数据
 * @param mapId 地图ID
 * @param difficulty 难度枚举值
 * @return 返回地图难度数据指针，如果不存在则返回 nullptr
 *
 * 查询特定地图在指定难度下的配置信息（如重置时间、最大玩家数等）。
 */
TC_GAME_API MapDifficulty const* GetMapDifficultyData(uint32 mapId, Difficulty difficulty);

/**
 * @brief 获取降级的地图难度数据
 * @param mapId 地图ID
 * @param difficulty [输入/输出] 难度枚举值，可能被修改为实际可用的难度
 * @return 返回地图难度数据指针
 *
 * 当指定的难度不可用时，自动降级到可用的难度级别。
 * 例如，如果英雄难度不可用，可能会降级到普通难度。
 */
TC_GAME_API MapDifficulty const* GetDownscaledMapDifficultyData(uint32 mapId, Difficulty &difficulty);

/**
 * @brief 获取职业的天赋标签页ID数组
 * @param cls 职业ID
 * @return 返回包含 MAX_TALENT_TABS 个天赋标签页ID的数组指针
 *
 * 每个职业有多个天赋树（如战士有武器、狂怒、防护三个天赋树），
 * 此函数返回这些天赋树对应的标签页ID。
 */
TC_GAME_API uint32 const* /*[MAX_TALENT_TABS]*/ GetTalentTabPages(uint8 cls);

/**
 * @brief 获取液体类型标志
 * @param liquidType 液体类型ID
 * @return 返回液体类型的标志位
 *
 * 液体类型包括水、岩浆、淤泥等，每种类型有不同的标志属性。
 */
TC_GAME_API uint32 GetLiquidFlags(uint32 liquidType);

/**
 * @brief 根据等级获取战场分组
 * @param mapid 地图ID
 * @param level 玩家等级
 * @return 返回战场难度条目指针
 *
 * 战场按等级分为不同的分组，此函数查找适合指定等级的战场分组。
 */
TC_GAME_API PvPDifficultyEntry const* GetBattlegroundBracketByLevel(uint32 mapid, uint32 level);

/**
 * @brief 根据分组ID获取战场分组
 * @param mapid 地图ID
 * @param id 战场分组ID
 * @return 返回战场难度条目指针
 *
 * 通过分组ID直接查询战场分组配置。
 */
TC_GAME_API PvPDifficultyEntry const* GetBattlegroundBracketById(uint32 mapid, BattlegroundBracketId id);

/**
 * @brief 获取角色面部毛发样式条目
 * @param race 种族ID
 * @param gender 性别（0=男性，1=女性）
 * @param facialHairID 面部毛发样式ID
 * @return 返回面部毛发样式条目指针
 *
 * 查询特定种族和性别的面部毛发样式数据（如胡须、胡子等）。
 */
TC_GAME_API CharacterFacialHairStylesEntry const* GetCharFacialHairEntry(uint8 race, uint8 gender, uint8 facialHairID);

/**
 * @brief 获取角色外观部分条目
 * @param race 种族ID
 * @param genType 外观部分类型（CharSectionType）
 * @param gender 性别（0=男性，1=女性）
 * @param type 外观类型
 * @param color 颜色ID
 * @return 返回角色外观部分条目指针
 *
 * 角色外观包括皮肤、脸型、发型、发色等多个部分，此函数查询具体的外观配置。
 */
TC_GAME_API CharSectionsEntry const* GetCharSectionEntry(uint8 race, CharSectionType genType, uint8 gender, uint8 type, uint8 color);

/**
 * @brief 获取角色初始装备条目
 * @param race 种族ID
 * @param class_ 职业ID
 * @param gender 性别（0=男性，1=女性）
 * @return 返回初始装备条目指针
 *
 * 新建角色时会获得一套初始装备，此函数查询特定种族、职业和性别的初始装备配置。
 */
TC_GAME_API CharStartOutfitEntry const* GetCharStartOutfitEntry(uint8 race, uint8 class_, uint8 gender);

/**
 * @brief 获取地下城查找器（LFG）的地下城条目
 * @param mapId 地图ID
 * @param difficulty 难度枚举值
 * @return 返回LFG地下城条目指针
 *
 * 用于查找特定地图和难度对应的地下城查找器配置。
 */
TC_GAME_API LFGDungeonEntry const* GetLFGDungeon(uint32 mapId, Difficulty difficulty);

/**
 * @brief 获取地图的默认光照ID
 * @param mapId 地图ID
 * @return 返回默认光照ID
 *
 * 每个地图都有一个默认的光照设置，用于渲染场景。
 */
TC_GAME_API uint32 GetDefaultMapLight(uint32 mapId);

/**
 * @typedef SkillRaceClassInfoMap
 * @brief 技能种族职业信息映射类型
 *
 * 多重映射，键为技能ID，值为技能的种族职业限制信息。
 */
typedef std::unordered_multimap<uint32, SkillRaceClassInfoEntry const*> SkillRaceClassInfoMap;

/**
 * @typedef SkillRaceClassInfoBounds
 * @brief 技能种族职业信息迭代器范围类型
 *
 * 表示多重映射中某个键对应的所有值的迭代器范围。
 */
typedef std::pair<SkillRaceClassInfoMap::iterator, SkillRaceClassInfoMap::iterator> SkillRaceClassInfoBounds;

/**
 * @brief 根据技能ID获取技能线能力列表
 * @param skill 技能ID
 * @return 返回技能线能力条目向量的指针
 *
 * 返回指定技能包含的所有技能线能力（如专业技能的配方列表）。
 */
TC_GAME_API std::vector<SkillLineAbilityEntry const*> const* GetSkillLineAbilitiesBySkill(uint32 skill);

/**
 * @brief 获取技能的种族职业限制信息
 * @param skill 技能ID
 * @param race 种族ID
 * @param class_ 职业ID
 * @return 返回技能种族职业信息条目指针
 *
 * 某些技能只能被特定种族或职业学习，此函数查询这些限制信息。
 */
TC_GAME_API SkillRaceClassInfoEntry const* GetSkillRaceClassInfo(uint32 skill, uint8 race, uint8 class_);

/**
 * @brief 验证名称是否符合规则
 * @param name 待验证的名称（宽字符串）
 * @param locale 语言标识
 * @return 返回验证结果代码（ResponseCodes）
 *
 * 检查角色名称是否符合命名规则（如长度、禁用词、特殊字符等）。
 */
TC_GAME_API ResponseCodes ValidateName(std::wstring const& name, LocaleConstant locale);

/**
 * @brief 查找表情文本声音条目
 * @param emote 表情ID
 * @param race 种族ID
 * @param gender 性别
 * @return 返回表情文本声音条目指针
 *
 * 角色做表情时可能播放特定的声音，此函数查找对应的声音配置。
 */
TC_GAME_API EmotesTextSoundEntry const* FindTextSoundEmoteFor(uint32 emote, uint32 race, uint32 gender);

/* ==================== 成就和角色相关存储 ==================== */

/// 成就条目存储，包含所有成就的定义和属性
TC_GAME_API extern DBCStorage <AchievementEntry>             sAchievementStore;

/// 成就条件条目存储，定义达成成就所需的条件
TC_GAME_API extern DBCStorage <AchievementCriteriaEntry>     sAchievementCriteriaStore;

/// 区域表条目存储，包含所有游戏区域的信息
TC_GAME_API extern DBCStorage <AreaTableEntry>               sAreaTableStore;

/// 区域组条目存储，定义区域的分组关系
TC_GAME_API extern DBCStorage <AreaGroupEntry>               sAreaGroupStore;

/// 区域兴趣点（POI）条目存储，地图上的标记点信息
TC_GAME_API extern DBCStorage <AreaPOIEntry>                 sAreaPOIStore;

/// 区域触发器条目存储，定义进入区域时触发的效果
TC_GAME_API extern DBCStorage <AreaTriggerEntry>             sAreaTriggerStore;

/// 拍卖行条目存储，定义各个拍卖行的配置
TC_GAME_API extern DBCStorage <AuctionHouseEntry>            sAuctionHouseStore;

/// 银行背包槽价格条目存储，定义购买银行槽位的价格
TC_GAME_API extern DBCStorage <BankBagSlotPricesEntry>       sBankBagSlotPricesStore;

/// 禁用插件条目存储，记录被禁止使用的插件列表
TC_GAME_API extern DBCStorage <BannedAddOnsEntry>            sBannedAddOnsStore;

/// 理发店样式条目存储，定义可用的发型和外观选项
TC_GAME_API extern DBCStorage <BarberShopStyleEntry>         sBarberShopStyleStore;

/// 战场管理员列表条目存储，定义各个战场的NPC入口
TC_GAME_API extern DBCStorage <BattlemasterListEntry>        sBattlemasterListStore;

/// 聊天频道条目存储，定义游戏中的聊天频道
TC_GAME_API extern DBCStorage <ChatChannelsEntry>            sChatChannelsStore;

/// 角色面部毛发样式条目存储，定义各种族的面部毛发选项
TC_GAME_API extern DBCStorage <CharacterFacialHairStylesEntry> sCharacterFacialHairStylesStore;

/// 角色外观部分条目存储，定义角色的各种外观组件
TC_GAME_API extern DBCStorage <CharSectionsEntry>            sCharSectionsStore;

/// 角色初始装备条目存储，定义新建角色的初始装备
TC_GAME_API extern DBCStorage <CharStartOutfitEntry>         sCharStartOutfitStore;

/// 角色头衔条目存储，定义游戏中可获得的头衔
TC_GAME_API extern DBCStorage <CharTitlesEntry>              sCharTitlesStore;

/// 职业条目存储，包含所有职业的定义和属性
TC_GAME_API extern DBCStorage <ChrClassesEntry>              sChrClassesStore;

/// 种族条目存储，包含所有种族的定义和属性
TC_GAME_API extern DBCStorage <ChrRacesEntry>                sChrRacesStore;

/* ==================== 过场动画和生物相关存储 ==================== */

/// 过场动画摄像机条目存储，定义过场动画的摄像机位置和路径
TC_GAME_API extern DBCStorage <CinematicCameraEntry>         sCinematicCameraStore;

/// 过场动画序列条目存储，定义过场动画的播放顺序
TC_GAME_API extern DBCStorage <CinematicSequencesEntry>      sCinematicSequencesStore;

/// 生物显示信息条目存储，定义生物的模型和外观
TC_GAME_API extern DBCStorage <CreatureDisplayInfoEntry>     sCreatureDisplayInfoStore;

/// 生物显示信息扩展条目存储，提供额外的生物显示数据
TC_GAME_API extern DBCStorage <CreatureDisplayInfoExtraEntry> sCreatureDisplayInfoExtraStore;

/// 生物家族条目存储，定义生物的分类家族（如野兽、恶魔等）
TC_GAME_API extern DBCStorage <CreatureFamilyEntry>          sCreatureFamilyStore;

/// 生物模型数据条目存储，定义生物模型的属性和文件路径
TC_GAME_API extern DBCStorage <CreatureModelDataEntry>       sCreatureModelDataStore;

/// 生物法术数据条目存储，定义生物使用的法术列表
TC_GAME_API extern DBCStorage <CreatureSpellDataEntry>       sCreatureSpellDataStore;

/// 生物类型条目存储，定义生物的基本类型（如野兽、人形生物等）
TC_GAME_API extern DBCStorage <CreatureTypeEntry>            sCreatureTypeStore;

/// 货币类型条目存储，定义游戏中的各种货币（如徽章、点数等）
TC_GAME_API extern DBCStorage <CurrencyTypesEntry>           sCurrencyTypesStore;

/// 可破坏模型数据条目存储，定义可破坏物体的模型数据
TC_GAME_API extern DBCStorage <DestructibleModelDataEntry>   sDestructibleModelDataStore;

/// 地下城遭遇条目存储，定义地下城中的Boss遭遇战
TC_GAME_API extern DBCStorage <DungeonEncounterEntry>        sDungeonEncounterStore;

/* ==================== 耐久度、表情和阵营相关存储 ==================== */

/// 耐久度费用条目存储，定义装备修理的费用
TC_GAME_API extern DBCStorage <DurabilityCostsEntry>         sDurabilityCostsStore;

/// 耐久度质量条目存储，定义不同质量物品的耐久度
TC_GAME_API extern DBCStorage <DurabilityQualityEntry>       sDurabilityQualityStore;

/// 表情条目存储，定义角色可以做的各种表情动作
TC_GAME_API extern DBCStorage <EmotesEntry>                  sEmotesStore;

/// 表情文本条目存储，定义表情动作对应的文本命令
TC_GAME_API extern DBCStorage <EmotesTextEntry>              sEmotesTextStore;

/// 表情文本声音条目存储，定义表情动作播放的声音
TC_GAME_API extern DBCStorage <EmotesTextSoundEntry>         sEmotesTextSoundStore;

/// 阵营条目存储，包含所有阵营的定义和关系
TC_GAME_API extern DBCStorage <FactionEntry>                 sFactionStore;

/// 阵营模板条目存储，定义实体的阵营属性和敌对关系
TC_GAME_API extern DBCStorage <FactionTemplateEntry>         sFactionTemplateStore;

/* ==================== 游戏对象和宝石相关存储 ==================== */

/// 游戏对象外观包条目存储，定义游戏对象的外观变体
TC_GAME_API extern DBCStorage <GameObjectArtKitEntry>        sGameObjectArtKitStore;

/// 游戏对象显示信息条目存储，定义游戏对象的模型显示数据
TC_GAME_API extern DBCStorage <GameObjectDisplayInfoEntry>   sGameObjectDisplayInfoStore;

/// 宝石属性条目存储，定义宝石的镶嵌属性
TC_GAME_API extern DBCStorage <GemPropertiesEntry>           sGemPropertiesStore;

/// 雕文属性条目存储，定义雕文的效果和类型
TC_GAME_API extern DBCStorage <GlyphPropertiesEntry>         sGlyphPropertiesStore;

/// 雕文槽位条目存储，定义各个雕文槽位的类型
TC_GAME_API extern DBCStorage <GlyphSlotEntry>               sGlyphSlotStore;

/* ==================== 游戏数据表（Gt）相关存储 ==================== */

/// 理发店基础费用表存储，定义理发店服务的基础价格
TC_GAME_API extern DBCStorage <GtBarberShopCostBaseEntry>    sGtBarberShopCostBaseStore;

/// 战斗评级表存储，定义战斗属性的转换系数
TC_GAME_API extern DBCStorage <GtCombatRatingsEntry>         sGtCombatRatingsStore;

/// 近战暴击基础几率表存储，定义基础近战暴击几率
TC_GAME_API extern DBCStorage <GtChanceToMeleeCritBaseEntry> sGtChanceToMeleeCritBaseStore;

/// 近战暴击几率表存储，定义敏捷对近战暴击的加成
TC_GAME_API extern DBCStorage <GtChanceToMeleeCritEntry>     sGtChanceToMeleeCritStore;

/// 法术暴击基础几率表存储，定义基础法术暴击几率
TC_GAME_API extern DBCStorage <GtChanceToSpellCritBaseEntry> sGtChanceToSpellCritBaseStore;

/// 法术暴击几率表存储，定义智力对法术暴击的加成
TC_GAME_API extern DBCStorage <GtChanceToSpellCritEntry>     sGtChanceToSpellCritStore;

/// NPC法力消耗缩放表存储，定义NPC法术的法力消耗缩放
TC_GAME_API extern DBCStorage <GtNPCManaCostScalerEntry>     sGtNPCManaCostScalerStore;

/// 职业战斗评级缩放表存储，定义各职业战斗评级的缩放系数
TC_GAME_API extern DBCStorage <GtOCTClassCombatRatingScalarEntry> sGtOCTClassCombatRatingScalarStore;

/// 生命值回复表存储，定义生命值回复速率
TC_GAME_API extern DBCStorage <GtOCTRegenHPEntry>            sGtOCTRegenHPStore;

/// 法力值回复表存储（当前未使用）
//TC_GAME_API extern DBCStorage <GtOCTRegenMPEntry>            sGtOCTRegenMPStore; -- not used currently

/// 每秒生命值回复表存储，定义每秒生命值回复量
TC_GAME_API extern DBCStorage <GtRegenHPPerSptEntry>         sGtRegenHPPerSptStore;

/// 每秒法力值回复表存储，定义每秒法力值回复量
TC_GAME_API extern DBCStorage <GtRegenMPPerSptEntry>         sGtRegenMPPerSptStore;

/* ==================== 节日和物品相关存储 ==================== */

/// 节日条目存储，定义游戏中的各种节日活动
TC_GAME_API extern DBCStorage <HolidaysEntry>                sHolidaysStore;

/// 物品条目存储，包含所有物品的定义和属性
TC_GAME_API extern DBCStorage <ItemEntry>                    sItemStore;

/// 物品背包家族条目存储，定义物品的背包分类（如弹药袋、草药袋等）
TC_GAME_API extern DBCStorage <ItemBagFamilyEntry>           sItemBagFamilyStore;

/// 物品显示信息条目存储（当前未使用）
//TC_GAME_API extern DBCStorage <ItemDisplayInfoEntry>      sItemDisplayInfoStore; -- not used currently

/// 物品扩展成本条目存储，定义物品的特殊购买代价（如徽章、货币等）
TC_GAME_API extern DBCStorage <ItemExtendedCostEntry>        sItemExtendedCostStore;

/// 物品限制类别条目存储，定义物品的携带限制（如唯一装备等）
TC_GAME_API extern DBCStorage <ItemLimitCategoryEntry>       sItemLimitCategoryStore;

/// 物品随机属性条目存储，定义物品的随机附魔属性
TC_GAME_API extern DBCStorage <ItemRandomPropertiesEntry>    sItemRandomPropertiesStore;

/// 物品随机后缀条目存储，定义物品的随机后缀属性（如"of the Bear"）
TC_GAME_API extern DBCStorage <ItemRandomSuffixEntry>        sItemRandomSuffixStore;

/// 物品套装条目存储，定义套装物品的组合和套装奖励
TC_GAME_API extern DBCStorage <ItemSetEntry>                 sItemSetStore;

/* ==================== 地下城查找器、光照和锁定相关存储 ==================== */

/// LFG地下城条目存储，定义地下城查找器中的地下城列表
TC_GAME_API extern DBCStorage <LFGDungeonEntry>              sLFGDungeonStore;

/// 光照条目存储，定义游戏场景的光照设置
TC_GAME_API extern DBCStorage <LightEntry>                   sLightStore;

/// 液体类型条目存储，定义各种液体（水、岩浆等）的属性
TC_GAME_API extern DBCStorage <LiquidTypeEntry>              sLiquidTypeStore;

/// 锁定条目存储，定义各种锁的解锁要求（如需要钥匙或技能）
TC_GAME_API extern DBCStorage <LockEntry>                    sLockStore;

/// 邮件模板条目存储，定义系统邮件的模板内容
TC_GAME_API extern DBCStorage <MailTemplateEntry>            sMailTemplateStore;

/* ==================== 地图相关存储 ==================== */

/// 地图条目存储，包含所有地图的定义和属性
TC_GAME_API extern DBCStorage <MapEntry>                     sMapStore;

/// 地图难度条目存储（不直接使用，请使用 GetMapDifficultyData 函数）
//TC_GAME_API extern DBCStorage <MapDifficultyEntry>           sMapDifficultyStore; -- use GetMapDifficultyData insteed

/// 地图难度映射存储，提供地图难度的快速查询
TC_GAME_API extern MapDifficultyMap                          sMapDifficultyMap;

/// 电影条目存储，定义游戏内播放的电影文件
TC_GAME_API extern DBCStorage <MovieEntry>                   sMovieStore;

/// 覆盖法术数据条目存储，定义覆盖默认法术的特殊数据
TC_GAME_API extern DBCStorage <OverrideSpellDataEntry>       sOverrideSpellDataStore;

/// 能量显示条目存储，定义不同能量类型的显示方式
TC_GAME_API extern DBCStorage <PowerDisplayEntry>            sPowerDisplayStore;

/* ==================== 任务相关存储 ==================== */

/// 任务排序条目存储，定义任务的分类排序
TC_GAME_API extern DBCStorage <QuestSortEntry>               sQuestSortStore;

/// 任务经验值条目存储，定义任务奖励的经验值
TC_GAME_API extern DBCStorage <QuestXPEntry>                 sQuestXPStore;

/// 任务阵营奖励条目存储，定义任务的阵营声望奖励
TC_GAME_API extern DBCStorage <QuestFactionRewEntry>         sQuestFactionRewardStore;

/* ==================== 属性缩放和技能相关存储 ==================== */

/// 随机属性点数条目存储，定义随机属性的点数分配
TC_GAME_API extern DBCStorage <RandPropPointsEntry>          sRandPropPointsStore;

/// 缩放属性分布条目存储，定义属性的缩放规则
TC_GAME_API extern DBCStorage <ScalingStatDistributionEntry> sScalingStatDistributionStore;

/// 缩放属性值条目存储，定义不同等级的属性值
TC_GAME_API extern DBCStorage <ScalingStatValuesEntry>       sScalingStatValuesStore;

/// 技能线条目存储，包含所有技能的定义和属性
TC_GAME_API extern DBCStorage <SkillLineEntry>               sSkillLineStore;

/// 技能线能力条目存储，定义技能包含的能力和法术
TC_GAME_API extern DBCStorage <SkillLineAbilityEntry>        sSkillLineAbilityStore;

/// 技能层级条目存储，定义技能的最大值和层级划分
TC_GAME_API extern DBCStorage <SkillTiersEntry>              sSkillTiersStore;

/* ==================== 声音和法术相关存储 ==================== */

/// 声音条目存储，包含所有游戏声音的定义
TC_GAME_API extern DBCStorage <SoundEntriesEntry>            sSoundEntriesStore;

/// 法术施法时间条目存储，定义法术的施法时间
TC_GAME_API extern DBCStorage <SpellCastTimesEntry>          sSpellCastTimesStore;

/// 法术类别条目存储，定义法术的分类和共享冷却
TC_GAME_API extern DBCStorage <SpellCategoryEntry>           sSpellCategoryStore;

/// 法术难度条目存储，定义法术在不同难度下的变体
TC_GAME_API extern DBCStorage <SpellDifficultyEntry>         sSpellDifficultyStore;

/// 法术持续时间条目存储，定义法术效果的持续时间
TC_GAME_API extern DBCStorage <SpellDurationEntry>           sSpellDurationStore;

/// 法术聚焦对象条目存储，定义法术所需的聚焦对象
TC_GAME_API extern DBCStorage <SpellFocusObjectEntry>        sSpellFocusObjectStore;

/// 法术物品附魔条目存储，定义物品附魔效果
TC_GAME_API extern DBCStorage <SpellItemEnchantmentEntry>    sSpellItemEnchantmentStore;

/// 法术物品附魔条件条目存储，定义附魔效果的触发条件
TC_GAME_API extern DBCStorage <SpellItemEnchantmentConditionEntry> sSpellItemEnchantmentConditionStore;

/// 宠物家族法术存储，定义各个宠物家族的法术列表
TC_GAME_API extern PetFamilySpellsStore                      sPetFamilySpellsStore;

/// 宠物天赋法术集合，存储所有宠物天赋法术的ID
TC_GAME_API extern std::unordered_set<uint32>                sPetTalentSpells;

/// 法术半径条目存储，定义法术效果的作用半径
TC_GAME_API extern DBCStorage <SpellRadiusEntry>             sSpellRadiusStore;

/// 法术射程条目存储，定义法术的施法射程
TC_GAME_API extern DBCStorage <SpellRangeEntry>              sSpellRangeStore;

/// 法术符文消耗条目存储，定义死亡骑士符文消耗
TC_GAME_API extern DBCStorage <SpellRuneCostEntry>           sSpellRuneCostStore;

/// 法术变形形态条目存储，定义德鲁伊等职业的变形形态
TC_GAME_API extern DBCStorage <SpellShapeshiftFormEntry>     sSpellShapeshiftFormStore;

/// 法术条目存储，包含所有法术的定义和属性
TC_GAME_API extern DBCStorage <SpellEntry>                   sSpellStore;

/// 法术视觉效果条目存储，定义法术的视觉表现
TC_GAME_API extern DBCStorage <SpellVisualEntry>             sSpellVisualStore;

/* ==================== 天赋和出租车相关存储 ==================== */

/// 兽栏槽位价格条目存储，定义购买兽栏槽位的价格
TC_GAME_API extern DBCStorage <StableSlotPricesEntry>        sStableSlotPricesStore;

/// 召唤属性条目存储，定义召唤生物的属性
TC_GAME_API extern DBCStorage <SummonPropertiesEntry>        sSummonPropertiesStore;

/// 天赋条目存储，包含所有天赋的定义和属性
TC_GAME_API extern DBCStorage <TalentEntry>                  sTalentStore;

/// 天赋标签页条目存储，定义天赋树的分类
TC_GAME_API extern DBCStorage <TalentTabEntry>               sTalentTabStore;

/// 出租车节点条目存储，定义飞行点的位置信息
TC_GAME_API extern DBCStorage <TaxiNodesEntry>               sTaxiNodesStore;

/// 出租车路径条目存储，定义飞行路线
TC_GAME_API extern DBCStorage <TaxiPathEntry>                sTaxiPathStore;

/// 出租车节点掩码，标记可用的飞行点
TC_GAME_API extern TaxiMask                                  sTaxiNodesMask;

/// 旧大陆出租车节点掩码，标记旧大陆的飞行点
TC_GAME_API extern TaxiMask                                  sOldContinentsNodesMask;

/// 部落出租车节点掩码，标记部落专用的飞行点
TC_GAME_API extern TaxiMask                                  sHordeTaxiNodesMask;

/// 联盟出租车节点掩码，标记联盟专用的飞行点
TC_GAME_API extern TaxiMask                                  sAllianceTaxiNodesMask;

/// 死亡骑士出租车节点掩码，标记死亡骑士专用的飞行点
TC_GAME_API extern TaxiMask                                  sDeathKnightTaxiNodesMask;

/// 按源节点分组的出租车路径集合，用于快速查询飞行路线
TC_GAME_API extern TaxiPathSetBySource                       sTaxiPathSetBySource;

/// 按路径索引分组的出租车节点集合，存储每条路线的所有节点
TC_GAME_API extern TaxiPathNodesByPath                       sTaxiPathNodesByPath;

/* ==================== 传输、载具和图腾相关存储 ==================== */

/// 传输动画条目存储，定义传输工具（如船只）的动画
TC_GAME_API extern DBCStorage <TransportAnimationEntry>      sTransportAnimationStore;

/// 传输旋转条目存储，定义传输工具的旋转动画
TC_GAME_API extern DBCStorage <TransportRotationEntry>       sTransportRotationStore;

/// 团队贡献点数条目存储，定义团队贡献点数系统
TC_GAME_API extern DBCStorage <TeamContributionPointsEntry>  sTeamContributionPointsStore;

/// 图腾类别条目存储，定义图腾的类型和属性
TC_GAME_API extern DBCStorage <TotemCategoryEntry>           sTotemCategoryStore;

/// 载具条目存储，包含所有载具的定义和属性
TC_GAME_API extern DBCStorage <VehicleEntry>                 sVehicleStore;

/// 载具座位条目存储，定义载具的座位配置
TC_GAME_API extern DBCStorage <VehicleSeatEntry>             sVehicleSeatStore;

/* ==================== 世界地图和安全位置相关存储 ==================== */

/// WMO区域表条目存储，定义世界模型对象的区域归属
TC_GAME_API extern DBCStorage <WMOAreaTableEntry>            sWMOAreaTableStore;

/// 世界地图区域条目存储（不直接使用，请使用 Zone2MapCoordinates 和 Map2ZoneCoordinates 函数）
//TC_GAME_API extern DBCStorage <WorldMapAreaEntry>           sWorldMapAreaStore; -- use Zone2MapCoordinates and Map2ZoneCoordinates

/// 世界地图覆盖层条目存储，定义地图上的覆盖层显示
TC_GAME_API extern DBCStorage <WorldMapOverlayEntry>         sWorldMapOverlayStore;

/// 世界安全位置条目存储，定义各种安全传送位置
TC_GAME_API extern DBCStorage <WorldSafeLocsEntry>           sWorldSafeLocsStore;

/**
 * @brief 加载所有 DBC 存储数据
 * @param dataPath 数据文件路径，指向包含 DBC 文件的目录
 *
 * 该函数是 DBC 加载系统的入口点，负责：
 * - 加载所有客户端数据库文件（DBC）
 * - 解析 DBC 文件并填充相应的存储容器
 * - 建立索引以支持快速查询
 * - 验证数据的完整性和一致性
 *
 * 此函数在服务器启动时调用，必须在其他游戏逻辑运行之前完成。
 * DBC 文件通常位于客户端数据目录中，包含游戏的所有静态数据。
 */
TC_GAME_API void LoadDBCStores(const std::string& dataPath);

#endif

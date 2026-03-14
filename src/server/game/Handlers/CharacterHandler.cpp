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
 * @file CharacterHandler.cpp
 * @brief 角色管理处理模块
 *
 * 本模块负责处理游戏中所有与角色管理相关的网络消息，包括：
 * - 角色列表查询（角色选择界面）
 * - 角色创建和删除
 * - 角色登录流程
 * - 角色重命名
 * - 角色外观自定义（理发店、付费服务）
 * - 角色阵营/种族转换
 * - 装备方案管理
 * - 声望、教程、雕文等角色设置
 *
 * 主要职责：
 * 1. 处理角色生命周期管理（创建、删除、登录）
 * 2. 验证角色操作的合法性（权限、限制条件等）
 * 3. 加载角色数据并初始化玩家对象
 * 4. 处理付费服务（外观自定义、阵营转换等）
 * 5. 管理角色相关配置（声望、教程、装备方案等）
 *
 * 关键类：
 * - LoginQueryHolder: 登录查询持有器，批量执行角色登录所需的数据库查询
 *
 * 性能考虑：
 * - 登录流程使用异步查询避免阻塞
 * - 角色数据批量加载以提高效率
 * - 使用角色缓存减少数据库访问
 */

#include "WorldSession.h"
#include "ArenaTeamMgr.h"
#include "CalendarMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GitRevision.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "InstanceSaveMgr.h"
#include "Item.h"
#include "Language.h"
#include "Log.h"
#include "Map.h"
#include "Metric.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerDump.h"
#include "RBAC.h"
#include "Realm.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "ServerMotd.h"
#include "SocialMgr.h"
#include "StringConvert.h"
#include "SystemPackets.h"
#include "QueryHolder.h"
#include "World.h"

/**
 * @class LoginQueryHolder
 * @brief 登录查询持有器类
 *
 * 用于在玩家登录过程中批量执行所有必要的数据库查询。
 * 继承自CharacterDatabaseQueryHolder，提供了账号ID和角色GUID的存储与访问。
 */
class LoginQueryHolder : public CharacterDatabaseQueryHolder
{
    private:
        uint32 m_accountId;     ///< 账号ID
        ObjectGuid m_guid;      ///< 角色全局唯一标识符
    public:
        /**
         * @brief 构造函数
         * @param accountId 账号ID
         * @param guid 角色GUID
         */
        LoginQueryHolder(uint32 accountId, ObjectGuid guid)
            : m_accountId(accountId), m_guid(guid) { }

        /**
         * @brief 获取角色GUID
         * @return 角色的全局唯一标识符
         */
        ObjectGuid GetGuid() const { return m_guid; }

        /**
         * @brief 获取账号ID
         * @return 账号的数字ID
         */
        uint32 GetAccountId() const { return m_accountId; }

        /**
         * @brief 初始化所有登录查询
         * @return 初始化是否成功
         */
        bool Initialize();
};

/**
 * @brief 初始化玩家登录所需的所有数据库查询
 *
 * 设置并准备所有登录时需要执行的数据库查询语句，包括：
 * - 角色基本信息加载
 * - 组队信息
 * - 实例绑定信息
 * - 光环效果
 * - 技能列表
 * - 任务状态（日常、周常、月常、季节性）
 * - 声望数据
 * - 背包物品
 * - 动作条
 * - 邮件系统
 * - 社交列表
 * - 炉石绑定位置
 * - 技能冷却
 * - 公会信息
 * - 竞技场信息
 * - 成就系统
 * - 装备方案
 * - 战场数据
 * - 雕文和天赋
 * - 技能熟练度
 * - 尸体位置
 * - 宠物槽位
 *
 * @return 所有查询是否成功初始化
 */
bool LoginQueryHolder::Initialize()
{
    SetSize(MAX_PLAYER_LOGIN_QUERY);

    bool res = true;
    ObjectGuid::LowType lowGuid = m_guid.GetCounter();

    // 加载角色基础数据
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_FROM, stmt);

    // 加载组队成员信息
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GROUP_MEMBER);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_GROUP, stmt);

    // 加载角色绑定的副本信息
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_INSTANCE);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_BOUND_INSTANCES, stmt);

    // 加载角色身上的光环效果
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_AURAS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_AURAS, stmt);

    // 加载角色已学会的法术
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_SPELL);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_SPELLS, stmt);

    // 加载任务状态
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_QUEST_STATUS, stmt);

    // 加载日常任务状态
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUS_DAILY);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_DAILY_QUEST_STATUS, stmt);

    // 加载周常任务状态
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUS_WEEKLY);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_WEEKLY_QUEST_STATUS, stmt);

    // 加载月常任务状态
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUS_MONTHLY);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_MONTHLY_QUEST_STATUS, stmt);

    // 加载季节性任务状态
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUS_SEASONAL);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_SEASONAL_QUEST_STATUS, stmt);

    // 加载声望数据
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_REPUTATION);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_REPUTATION, stmt);

    // 加载背包物品
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_INVENTORY);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_INVENTORY, stmt);

    // 加载动作条按钮
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_ACTIONS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_ACTIONS, stmt);

    // 加载邮件
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_MAIL);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_MAILS, stmt);

    // 加载邮件附件物品
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_MAILITEMS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_MAIL_ITEMS, stmt);

    // 加载社交列表（好友/忽略）
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_SOCIALLIST);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_SOCIAL_LIST, stmt);

    // 加载炉石绑定位置
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_HOMEBIND);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_HOME_BIND, stmt);

    // 加载法术冷却时间
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_SPELLCOOLDOWNS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_SPELL_COOLDOWNS, stmt);

    // 如果启用了名字变格功能（俄语等），加载变格名字
    if (sWorld->getBoolConfig(CONFIG_DECLINED_NAMES_USED))
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_DECLINEDNAMES);
        stmt->setUInt32(0, lowGuid);
        res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_DECLINED_NAMES, stmt);
    }

    // 加载公会成员信息
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GUILD_MEMBER);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_GUILD, stmt);

    // 加载竞技场队伍信息
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_ARENAINFO);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_ARENA_INFO, stmt);

    // 加载成就数据
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_ACHIEVEMENTS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_ACHIEVEMENTS, stmt);

    // 加载成就进度
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_CRITERIAPROGRESS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_CRITERIA_PROGRESS, stmt);

    // 加载装备方案
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_EQUIPMENTSETS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_EQUIPMENT_SETS, stmt);

    // 加载战场数据
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_BGDATA);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_BG_DATA, stmt);

    // 加载雕文配置
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_GLYPHS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_GLYPHS, stmt);

    // 加载天赋配置
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_TALENTS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_TALENTS, stmt);

    // 加载账号数据
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PLAYER_ACCOUNT_DATA);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_ACCOUNT_DATA, stmt);

    // 加载专业技能熟练度
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_SKILLS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_SKILLS, stmt);

    // 加载随机战场数据
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_RANDOMBG);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_RANDOM_BG, stmt);

    // 检查角色是否被封禁
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_BANNED);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_BANNED, stmt);

    // 加载已奖励的任务状态
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_QUESTSTATUSREW);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_QUEST_STATUS_REW, stmt);

    // 加载账号副本锁定时间
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ACCOUNT_INSTANCELOCKTIMES);
    stmt->setUInt32(0, m_accountId);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_INSTANCE_LOCK_TIMES, stmt);

    // 加载尸体位置（死亡角色）
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CORPSE_LOCATION);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_CORPSE_LOCATION, stmt);

    // 加载宠物槽位信息
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_PETS);
    stmt->setUInt32(0, lowGuid);
    res &= SetPreparedQuery(PLAYER_LOGIN_QUERY_LOAD_PET_SLOTS, stmt);

    return res;
}

/**
 * @brief 处理角色枚举结果
 *
 * 处理从数据库获取的角色列表查询结果，构建并发送角色枚举数据包给客户端。
 * 主要流程：
 * 1. 清空合法角色列表缓存
 * 2. 遍历查询结果，构建每个角色的枚举数据
 * 3. 过滤被封禁的角色，不加入合法列表
 * 4. 更新角色缓存
 * 5. 发送角色列表数据包
 *
 * @param result 数据库查询结果，包含账号下所有角色信息
 */
void WorldSession::HandleCharEnum(PreparedQueryResult result)
{
    // 创建角色枚举数据包，预估大小为100字节
    WorldPacket data(SMSG_CHAR_ENUM, 100);

    uint8 num = 0;  // 角色计数器

    data << num;    // 先写入占位符，最后更新实际数量

    // 清空合法角色列表
    _legitCharacters.clear();
    if (result)
    {
        do
        {
            // 从查询结果构建玩家GUID
            ObjectGuid guid(HighGuid::Player, (*result)[0].GetUInt32());
            TC_LOG_INFO("network", "Loading {} from account {}.", guid.ToString(), GetAccountId());

            // 构建单个角色的枚举数据
            if (Player::BuildEnumData(result, &data))
            {
                // 不允许被封禁的角色登录，只有未封禁的角色才加入合法列表
                if (!(*result)[23].GetUInt32())
                    _legitCharacters.insert(guid);

                // 如果角色缓存中没有该角色信息（可能是手动插入数据库的情况），添加缓存条目
                if (!sCharacterCache->HasCharacterCacheEntry(guid))
                    sCharacterCache->AddCharacterCacheEntry(guid, GetAccountId(), (*result)[1].GetString(), (*result)[4].GetUInt8(), (*result)[2].GetUInt8(), (*result)[3].GetUInt8(), (*result)[10].GetUInt8());
                ++num;
            }
        }
        while (result->NextRow() && num < MAX_CHARACTERS_PER_REALM); // 客户端最多显示10个角色，超过会报错
    }

    // 更新实际的角色数量
    data.put<uint8>(0, num);

    // 发送角色枚举数据包
    SendPacket(&data);
}

/**
 * @brief 处理角色枚举请求操作码
 *
 * 响应客户端请求角色列表的操作码(CMSG_CHAR_ENUM)。
 * 主要流程：
 * 1. 清理过期的角色封禁记录
 * 2. 根据配置决定是否加载名字变格信息
 * 3. 异步查询账号下所有角色信息
 * 4. 通过回调函数处理查询结果
 *
 * @param recvData 接收到的网络数据包（此操作码不携带额外数据）
 */
void WorldSession::HandleCharEnumOpcode(WorldPacket& /*recvData*/)
{
    // 删除过期的封禁记录
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_EXPIRED_BANS);
    CharacterDatabase.Execute(stmt);

    /// 获取加载账号下所有角色（及其宠物）所需的全部数据

    // 根据配置决定是否使用带变格名字的查询
    if (sWorld->getBoolConfig(CONFIG_DECLINED_NAMES_USED))
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ENUM_DECLINED_NAME);
    else
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ENUM);

    // 设置查询参数：当前宠物状态和账号ID
    stmt->setUInt8(0, PET_SAVE_AS_CURRENT);
    stmt->setUInt32(1, GetAccountId());

    // 异步执行查询，结果通过HandleCharEnum回调处理
    _queryProcessor.AddCallback(CharacterDatabase.AsyncQuery(stmt).WithPreparedCallback(std::bind(&WorldSession::HandleCharEnum, this, std::placeholders::_1)));
}

/**
 * @brief 处理角色创建请求操作码
 *
 * 响应客户端创建新角色的请求(CMSG_CHAR_CREATE)。
 * 主要流程：
 * 1. 解析客户端发送的角色创建信息（名字、种族、职业、性别、外观等）
 * 2. 进行一系列合法性验证：
 *    - 阵营创建限制检查
 *    - 种族/职业DBC数据有效性验证
 *    - 资料片限制检查
 *    - 种族/职业禁用检查
 *    - 名字合法性和保留名检查
 *    - 死亡骑士特殊限制检查
 * 3. 异步检查名字唯一性
 * 4. 检查账号角色数量限制
 * 5. 创建角色并保存到数据库
 * 6. 发送创建结果给客户端
 *
 * @param recvData 接收到的网络数据包，包含角色创建信息
 */
void WorldSession::HandleCharCreateOpcode(WorldPacket& recvData)
{
    // 创建角色信息结构体，用于存储解析后的创建数据
    std::shared_ptr<CharacterCreateInfo> createInfo = std::make_shared<CharacterCreateInfo>();

    // 从数据包中解析角色创建信息
    recvData >> createInfo->Name           // 角色名称
             >> createInfo->Race           // 种族
             >> createInfo->Class          // 职业
             >> createInfo->Gender         // 性别
             >> createInfo->Skin           // 肤色
             >> createInfo->Face           // 脸型
             >> createInfo->HairStyle      // 发型
             >> createInfo->HairColor      // 发色
             >> createInfo->FacialHair     // 面部毛发（胡须等）
             >> createInfo->OutfitId;      // 初始装备方案ID

    // 检查阵营创建限制（联盟/部落是否被禁用创建）
    if (!HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_TEAMMASK))
    {
        if (uint32 mask = sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_DISABLED))
        {
            bool disabled = false;

            // 根据种族判断阵营，检查该阵营是否被禁用创建
            switch (Player::TeamForRace(createInfo->Race))
            {
                case ALLIANCE:
                    disabled = (mask & (1 << 0)) != 0;  // 第0位表示联盟禁用
                    break;
                case HORDE:
                    disabled = (mask & (1 << 1)) != 0;  // 第1位表示部落禁用
                    break;
            }

            if (disabled)
            {
                SendCharCreate(CHAR_CREATE_DISABLED);
                return;
            }
        }
    }

    // 从DBC文件中查找职业信息，验证职业是否有效
    ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(createInfo->Class);
    if (!classEntry)
    {
        TC_LOG_ERROR("network", "Class ({}) not found in DBC while creating new char for account (ID: {}): wrong DBC files or cheater?", createInfo->Class, GetAccountId());
        SendCharCreate(CHAR_CREATE_FAILED);
        return;
    }

    // 从DBC文件中查找种族信息，验证种族是否有效
    ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(createInfo->Race);
    if (!raceEntry)
    {
        TC_LOG_ERROR("network", "Race ({}) not found in DBC while creating new char for account (ID: {}): wrong DBC files or cheater?", createInfo->Race, GetAccountId());
        SendCharCreate(CHAR_CREATE_FAILED);
        return;
    }

    // 防止未开通对应资料片的账号创建资料片种族（如德莱尼、血精灵需要TBC）
    if (raceEntry->RequiredExpansion > Expansion())
    {
        TC_LOG_ERROR("entities.player.cheat", "Expansion {} account:[{}] tried to Create character with expansion {} race ({})", Expansion(), GetAccountId(), raceEntry->RequiredExpansion, createInfo->Race);
        SendCharCreate(CHAR_CREATE_EXPANSION);
        return;
    }

    // 防止未开通对应资料片的账号创建资料片职业（如死亡骑士需要WLK）
    if (classEntry->RequiredExpansion > Expansion())
    {
        TC_LOG_ERROR("entities.player.cheat", "Expansion {} account:[{}] tried to Create character with expansion {} class ({})", Expansion(), GetAccountId(), classEntry->RequiredExpansion, createInfo->Class);
        SendCharCreate(CHAR_CREATE_EXPANSION_CLASS);
        return;
    }

    // 检查种族是否被禁用或不可玩
    if (!HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_RACEMASK))
    {
        // 检查种族是否标记为不可玩（如GM种族、怪物种族等）
        if (raceEntry->Alliance == CHRRACES_ALLIANCE_TYPE_NOT_PLAYABLE || raceEntry->HasFlag(CHRRACES_FLAGS_NOT_PLAYABLE))
        {
            TC_LOG_ERROR("network", "Race ({}) was not playable but requested while creating new char for account (ID: {}): wrong DBC files or cheater?", createInfo->Race, GetAccountId());
            SendCharCreate(CHAR_CREATE_DISABLED);
            return;
        }

        // 检查种族是否在配置的禁用掩码中
        uint32 raceMaskDisabled = sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_DISABLED_RACEMASK);
        if ((1 << (createInfo->Race - 1)) & raceMaskDisabled)
        {
            SendCharCreate(CHAR_CREATE_DISABLED);
            return;
        }
    }

    // 检查职业是否被禁用
    if (!HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_CLASSMASK))
    {
        uint32 classMaskDisabled = sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_DISABLED_CLASSMASK);
        if ((1 << (createInfo->Class - 1)) & classMaskDisabled)
        {
            SendCharCreate(CHAR_CREATE_DISABLED);
            return;
        }
    }

    // 验证角色名称有效性（规范化处理）
    if (!normalizePlayerName(createInfo->Name))
    {
        TC_LOG_ERROR("entities.player.cheat", "Account:[{}] but tried to Create character with empty [name] ", GetAccountId());
        SendCharCreate(CHAR_NAME_NO_NAME);
        return;
    }

    // 检查名字是否符合命名规则（长度、字符、违禁词等）
    ResponseCodes res = ObjectMgr::CheckPlayerName(createInfo->Name, GetSessionDbcLocale(), true);
    if (res != CHAR_NAME_SUCCESS)
    {
        SendCharCreate(res);
        return;
    }

    // 检查是否使用了保留名字（通常给GM或特殊用途）
    if (!HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_RESERVEDNAME) && sObjectMgr->IsReservedName(createInfo->Name))
    {
        SendCharCreate(CHAR_NAME_RESERVED);
        return;
    }

    // 死亡骑士特殊限制检查
    if (createInfo->Class == CLASS_DEATH_KNIGHT && !HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_DEATH_KNIGHT))
    {
        // 快速检查：服务器是否完全禁用了死亡骑士
        if (sWorld->getIntConfig(CONFIG_DEATH_KNIGHTS_PER_REALM) == 0)
        {
            SendCharCreate(CHAR_CREATE_UNIQUE_CLASS_LIMIT);
            return;
        }

        // 快速检查：所需等级是否超过了服务器最高等级（条件不可能满足）
        if (sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_MIN_LEVEL_FOR_DEATH_KNIGHT) > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        {
            SendCharCreate(CHAR_CREATE_LEVEL_REQUIREMENT);
            return;
        }
    }

    // 异步检查名字是否已被使用
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
    stmt->setString(0, createInfo->Name);

    // 使用链式回调处理多个异步查询
    _queryProcessor.AddCallback(CharacterDatabase.AsyncQuery(stmt)
        .WithChainingPreparedCallback([this](QueryCallback& queryCallback, PreparedQueryResult result)
    {
        // 如果查询有结果，说明名字已被使用
        if (result)
        {
            SendCharCreate(CHAR_CREATE_NAME_IN_USE);
            return;
        }

        // 名字可用，继续检查账号角色总数限制
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_SUM_REALM_CHARACTERS);
        stmt->setUInt32(0, GetAccountId());
        queryCallback.SetNextQuery(LoginDatabase.AsyncQuery(stmt));
    })
        .WithChainingPreparedCallback([this](QueryCallback& queryCallback, PreparedQueryResult result)
    {
        // 检查账号在所有服务器的角色总数
        uint64 acctCharCount = 0;
        if (result)
        {
            Field* fields = result->Fetch();
            acctCharCount = uint64(fields[0].GetDouble());
        }

        // 超过账号角色数量限制
        if (acctCharCount >= sWorld->getIntConfig(CONFIG_CHARACTERS_PER_ACCOUNT))
        {
            SendCharCreate(CHAR_CREATE_ACCOUNT_LIMIT);
            return;
        }

        // 继续检查当前服务器的角色数量
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_SUM_CHARS);
        stmt->setUInt32(0, GetAccountId());
        queryCallback.SetNextQuery(CharacterDatabase.AsyncQuery(stmt));
    })
        .WithChainingPreparedCallback([this, createInfo](QueryCallback& queryCallback, PreparedQueryResult result)
    {
        // 检查当前服务器的角色数量
        if (result)
        {
            Field* fields = result->Fetch();
            createInfo->CharCount = uint8(fields[0].GetUInt64()); // SQL的COUNT()返回uint64，但实际值不会超过uint8最大值

            // 超过服务器角色数量限制
            if (createInfo->CharCount >= sWorld->getIntConfig(CONFIG_CHARACTERS_PER_REALM))
            {
                SendCharCreate(CHAR_CREATE_SERVER_LIMIT);
                return;
            }
        }

        // 获取阵营和过场动画配置
        bool allowTwoSideAccounts = !sWorld->IsPvPRealm() || HasPermission(rbac::RBAC_PERM_TWO_SIDE_CHARACTER_CREATION);
        uint32 skipCinematics = sWorld->getIntConfig(CONFIG_SKIP_CINEMATICS);

        // 定义最终创建角色的Lambda函数
        std::function<void(PreparedQueryResult)> finalizeCharacterCreation = [this, createInfo](PreparedQueryResult result)
        {
            bool haveSameRace = false;
            uint32 deathKnightReqLevel = sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_MIN_LEVEL_FOR_DEATH_KNIGHT);
            bool hasDeathKnightReqLevel = (deathKnightReqLevel == 0);
            bool allowTwoSideAccounts = !sWorld->IsPvPRealm() || HasPermission(rbac::RBAC_PERM_TWO_SIDE_CHARACTER_CREATION);
            uint32 skipCinematics = sWorld->getIntConfig(CONFIG_SKIP_CINEMATICS);
            bool checkDeathKnightReqs = createInfo->Class == CLASS_DEATH_KNIGHT && !HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_DEATH_KNIGHT);

            if (result)
            {
                uint32 team = Player::TeamForRace(createInfo->Race);
                uint32 freeDeathKnightSlots = sWorld->getIntConfig(CONFIG_DEATH_KNIGHTS_PER_REALM);

                Field* field = result->Fetch();
                uint8 accRace = field[1].GetUInt8();

                // 死亡骑士相关检查
                if (checkDeathKnightReqs)
                {
                    uint8 accClass = field[2].GetUInt8();
                    if (accClass == CLASS_DEATH_KNIGHT)
                    {
                        if (freeDeathKnightSlots > 0)
                            --freeDeathKnightSlots;

                        if (freeDeathKnightSlots == 0)
                        {
                            SendCharCreate(CHAR_CREATE_UNIQUE_CLASS_LIMIT);
                            return;
                        }
                    }

                    if (!hasDeathKnightReqLevel)
                    {
                        uint8 accLevel = field[0].GetUInt8();
                        if (accLevel >= deathKnightReqLevel)
                            hasDeathKnightReqLevel = true;
                    }
                }

                // 仅对第一个角色检查阵营（PvP服务器限制）
                /// @todo 如果账号已经有两个阵营的角色怎么办？
                if (!allowTwoSideAccounts)
                {
                    uint32 accTeam = 0;
                    if (accRace > 0)
                        accTeam = Player::TeamForRace(accRace);

                    if (accTeam != team)
                    {
                        SendCharCreate(CHAR_CREATE_PVP_TEAMS_VIOLATION);
                        return;
                    }
                }

                // 搜索同种族角色以决定是否跳过开场动画，或检查死亡骑士限制
                /// @todo 检查开场动画是否已显示？（已经登录过的角色？cinematic字段）
                while ((skipCinematics == 1 && !haveSameRace) || createInfo->Class == CLASS_DEATH_KNIGHT)
                {
                    if (!result->NextRow())
                        break;

                    field = result->Fetch();
                    accRace = field[1].GetUInt8();

                    if (!haveSameRace)
                        haveSameRace = createInfo->Race == accRace;

                    if (checkDeathKnightReqs)
                    {
                        uint8 acc_class = field[2].GetUInt8();
                        if (acc_class == CLASS_DEATH_KNIGHT)
                        {
                            if (freeDeathKnightSlots > 0)
                                --freeDeathKnightSlots;

                            if (freeDeathKnightSlots == 0)
                            {
                                SendCharCreate(CHAR_CREATE_UNIQUE_CLASS_LIMIT);
                                return;
                            }
                        }

                        if (!hasDeathKnightReqLevel)
                        {
                            uint8 acc_level = field[0].GetUInt8();
                            if (acc_level >= deathKnightReqLevel)
                                hasDeathKnightReqLevel = true;
                        }
                    }
                }
            }

            // 死亡骑士等级要求检查
            if (checkDeathKnightReqs && !hasDeathKnightReqLevel)
            {
                SendCharCreate(CHAR_CREATE_LEVEL_REQUIREMENT);
                return;
            }

            // 再次检查名字唯一性（在保存到数据库的同一步骤中）
            if (sCharacterCache->GetCharacterCacheByName(createInfo->Name))
            {
                SendCharCreate(CHAR_CREATE_NAME_IN_USE);
                return;
            }

            // 创建玩家对象，使用智能指针管理生命周期
            std::shared_ptr<Player> newChar(new Player(this), [](Player* ptr)
            {
                ptr->CleanupsBeforeDelete();
                delete ptr;
            });
            newChar->GetMotionMaster()->Initialize();

            // 尝试创建角色
            if (!newChar->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), createInfo.get()))
            {
                // 角色创建失败（种族/职业等问题）
                SendCharCreate(CHAR_CREATE_ERROR);
                return;
            }

            // 设置是否跳过开场动画
            if ((haveSameRace && skipCinematics == 1) || skipCinematics == 2)
                newChar->setCinematic(1);                         // 不显示开场动画

            newChar->SetAtLoginFlag(AT_LOGIN_FIRST);              // 标记为首次登录

            // 开始数据库事务
            CharacterDatabaseTransaction characterTransaction = CharacterDatabase.BeginTransaction();
            LoginDatabaseTransaction trans = LoginDatabase.BeginTransaction();

            // 保存角色到数据库
            newChar->SaveToDB(characterTransaction, true);
            createInfo->CharCount += 1;

            // 更新账号角色计数
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_REP_REALM_CHARACTERS);
            stmt->setUInt32(0, createInfo->CharCount);
            stmt->setUInt32(1, GetAccountId());
            stmt->setUInt32(2, realm.Id.Realm);
            trans->Append(stmt);

            LoginDatabase.CommitTransaction(trans);

            // 异步提交角色创建事务
            AddTransactionCallback(CharacterDatabase.AsyncCommitTransaction(characterTransaction)).AfterComplete([this, newChar = std::move(newChar)](bool success)
            {
                if (success)
                {
                    // 记录创建日志
                    TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Create Character: {} {}", GetAccountId(), GetRemoteAddress(), newChar->GetName(), newChar->GetGUID().ToString());
                    // 触发脚本事件
                    sScriptMgr->OnPlayerCreate(newChar.get());
                    // 添加到角色缓存
                    sCharacterCache->AddCharacterCacheEntry(newChar->GetGUID(), GetAccountId(), newChar->GetName(), newChar->GetNativeGender(), newChar->GetRace(), newChar->GetClass(), newChar->GetLevel());

                    SendCharCreate(CHAR_CREATE_SUCCESS);
                }
                else
                    SendCharCreate(CHAR_CREATE_ERROR);
            });
        };

        // 根据配置决定是否需要查询现有角色信息
        // 如果允许双阵营账号、不禁用动画、且不是死亡骑士，则无需查询现有角色
        if (allowTwoSideAccounts && !skipCinematics && createInfo->Class != CLASS_DEATH_KNIGHT)
        {
            finalizeCharacterCreation(PreparedQueryResult(nullptr));
            return;
        }

        // 查询账号现有角色信息（用于检查阵营、死亡骑士数量等）
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_CREATE_INFO);
        stmt->setUInt32(0, GetAccountId());
        stmt->setUInt32(1, (skipCinematics == 1 || createInfo->Class == CLASS_DEATH_KNIGHT) ? 10 : 1);
        queryCallback.WithPreparedCallback(std::move(finalizeCharacterCreation)).SetNextQuery(CharacterDatabase.AsyncQuery(stmt));
    }));
}

/**
 * @brief 处理角色删除请求操作码
 *
 * 响应客户端删除角色的请求(CMSG_CHAR_DELETE)。
 * 主要流程：
 * 1. 从数据包解析要删除的角色GUID
 * 2. 检查角色是否在线（在线角色无法删除）
 * 3. 检查角色是否为公会会长（会长需先转让或解散公会）
 * 4. 检查角色是否为竞技场队长
 * 5. 验证角色是否属于当前账号（防止越权删除）
 * 6. 记录删除日志并创建角色备份（如果启用）
 * 7. 清理角色的日历事件和邀请
 * 8. 从数据库删除角色数据
 * 9. 发送删除结果给客户端
 *
 * @param recvData 接收到的网络数据包，包含要删除角色的GUID
 */
void WorldSession::HandleCharDeleteOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    // 获取发起删除请求的账号ID
    uint32 initAccountId = GetAccountId();

    // 不能删除已在线的角色
    if (ObjectAccessor::FindPlayer(guid))
    {
        sScriptMgr->OnPlayerFailedDelete(guid, initAccountId);
        return;
    }

    uint32 accountId = 0;
    uint8 level = 0;
    std::string name;

    // 检查是否为公会会长，会长无法直接删除角色
    if (sGuildMgr->GetGuildByLeader(guid))
    {
        sScriptMgr->OnPlayerFailedDelete(guid, initAccountId);
        SendCharDelete(CHAR_DELETE_FAILED_GUILD_LEADER);
        return;
    }

    // 检查是否为竞技场战队队长，队长无法直接删除角色
    if (sArenaTeamMgr->GetArenaTeamByCaptain(guid))
    {
        sScriptMgr->OnPlayerFailedDelete(guid, initAccountId);
        SendCharDelete(CHAR_DELETE_FAILED_ARENA_CAPTAIN);
        return;
    }

    // 从缓存获取角色信息
    CharacterCacheEntry const* characterInfo = sCharacterCache->GetCharacterCacheByGuid(guid);
    if (!characterInfo)
    {
        sScriptMgr->OnPlayerFailedDelete(guid, initAccountId);
        return;
    }

    accountId = characterInfo->AccountId;
    name = characterInfo->Name;
    level = characterInfo->Level;

    // 防止使用作弊工具删除其他玩家的角色
    if (accountId != initAccountId)
    {
        sScriptMgr->OnPlayerFailedDelete(guid, initAccountId);
        return;
    }

    // 记录删除日志
    TC_LOG_INFO("entities.player.character", "Account: {}, IP: {} deleted character: {}, {}, Level: {}", accountId, GetRemoteAddress(), name, guid.ToString(), level);

    // 在从数据库移除引用之前触发脚本钩子，防止竞态条件
    sScriptMgr->OnPlayerDelete(guid, initAccountId);

    // 如果启用了角色备份日志，创建角色数据转储
    if (sLog->ShouldLog("entities.player.dump", LOG_LEVEL_INFO))
    {
        std::string dump;
        if (PlayerDumpWriter().GetDump(guid.GetCounter(), dump))
            sLog->OutCharDump(dump.c_str(), accountId, guid.GetRawValue(), name.c_str());
    }

    // 移除角色的所有日历事件和邀请
    sCalendarMgr->RemoveAllPlayerEventsAndInvites(guid);

    // 从数据库删除角色数据
    Player::DeleteFromDB(guid, accountId);

    // 发送删除成功响应
    SendCharDelete(CHAR_DELETE_SUCCESS);
}

/**
 * @brief 处理玩家登录请求操作码
 *
 * 响应客户端选择角色登录的请求(CMSG_PLAYER_LOGIN)。
 * 主要流程：
 * 1. 检查是否已有角色正在加载或已登录（防止重复登录）
 * 2. 解析要登录的角色GUID
 * 3. 验证角色是否属于当前账号（防止越权登录）
 * 4. 初始化登录查询持有器，准备批量数据库查询
 * 5. 异步执行所有登录所需的数据查询
 * 6. 查询完成后回调HandlePlayerLogin处理登录流程
 *
 * @param recvData 接收到的网络数据包，包含要登录角色的GUID
 */
void WorldSession::HandlePlayerLoginOpcode(WorldPacket& recvData)
{
    // 检查是否已有角色正在加载或已登录，防止重复登录
    if (PlayerLoading() || GetPlayer() != nullptr)
    {
        TC_LOG_ERROR("network", "Player tries to login again, AccountId = {}", GetAccountId());
        KickPlayer("WorldSession::HandlePlayerLoginOpcode Another client logging in");
        return;
    }

    // 标记正在加载角色
    m_playerLoading = true;
    ObjectGuid playerGuid;

    // 从数据包读取角色GUID
    recvData >> playerGuid;

    // 验证角色是否属于当前账号，防止使用作弊工具登录他人角色
    if (!IsLegitCharacterForAccount(playerGuid))
    {
        TC_LOG_ERROR("network", "Account ({}) can't login with that character ({}).", GetAccountId(), playerGuid.ToString());
        KickPlayer("WorldSession::HandlePlayerLoginOpcode Trying to login with a character of another account");
        return;
    }

    // 创建登录查询持有器，用于批量执行所有登录所需的数据库查询
    std::shared_ptr<LoginQueryHolder> holder = std::make_shared<LoginQueryHolder>(GetAccountId(), playerGuid);
    if (!holder->Initialize())
    {
        m_playerLoading = false;
        return;
    }

    // 异步执行查询，完成后回调HandlePlayerLogin
    AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(holder)).AfterComplete([this](SQLQueryHolderBase const& holder)
    {
        HandlePlayerLogin(static_cast<LoginQueryHolder const&>(holder));
    });
}

/**
 * @brief 处理玩家登录（内部实现）
 *
 * 处理登录查询完成后的实际登录流程。
 * 主要流程：
 * 1. 从数据库加载角色数据
 * 2. 发送世界验证信息
 * 3. 加载账号数据
 * 4. 发送服务器欢迎消息(MOTD)
 * 5. 处理公会信息
 * 6. 发送初始数据包
 * 7. 处理首次登录（开场动画、初始声望等）
 * 8. 将角色添加到地图
 * 9. 更新在线状态
 * 10. 处理组队通知
 * 11. 加载尸体和宠物
 * 12. 应用登录时标记（重置技能/天赋等）
 * 13. 触发登录脚本事件
 *
 * @param holder 登录查询持有器，包含所有登录所需的数据库查询结果
 */
void WorldSession::HandlePlayerLogin(LoginQueryHolder const& holder)
{
    ObjectGuid playerGuid = holder.GetGuid();

    // 创建玩家对象
    Player* pCurrChar = new Player(this);

    // 创建聊天处理器，用于发送服务器信息和配置字符串
    ChatHandler chH = ChatHandler(pCurrChar->GetSession());

    // 从数据库加载角色数据，LoadFromDB会验证账号ID匹配，防止登录他人角色
    if (!pCurrChar->LoadFromDB(playerGuid, holder))
    {
        SetPlayer(nullptr);
        KickPlayer("WorldSession::HandlePlayerLogin Player::LoadFromDB failed"); // 断开客户端连接，玩家未设置到会话，踢出时不会被删除或保存
        delete pCurrChar;                                   // 手动删除玩家对象
        m_playerLoading = false;
        return;
    }

    // 初始化移动控制器
    pCurrChar->GetMotionMaster()->Initialize();
    // 发送副本难度信息
    pCurrChar->SendDungeonDifficulty(false);

    // 发送登录世界验证包，告诉客户端角色所在的地图和位置
    WorldPackets::Character::LoginVerifyWorld loginVerifyWorld;
    loginVerifyWorld.MapID = pCurrChar->GetMapId();
    loginVerifyWorld.Pos = pCurrChar->GetPosition();
    SendPacket(loginVerifyWorld.Write());

    // 加载角色特定的账号数据
    LoadAccountData(holder.GetPreparedResult(PLAYER_LOGIN_QUERY_LOAD_ACCOUNT_DATA), PER_CHARACTER_CACHE_MASK);
    SendAccountDataTimes(PER_CHARACTER_CACHE_MASK);

    // 发送特性系统状态
    SendFeatureSystemStatus();

    // 发送每日消息(MOTD)
    {
        SendPacket(Motd::GetMotdPacket());

        // 如果配置启用，发送服务器版本信息
        if (sWorld->getIntConfig(CONFIG_ENABLE_SINFO_LOGIN) == 1)
            chH.PSendSysMessage(GitRevision::GetFullVersion());
    }

    // 加载公会成员信息
    if (PreparedQueryResult resultGuild = holder.GetPreparedResult(PLAYER_LOGIN_QUERY_LOAD_GUILD))
    {
        Field* fields = resultGuild->Fetch();
        pCurrChar->SetInGuild(fields[0].GetUInt32());
        pCurrChar->SetRank(fields[1].GetUInt8());
    }
    else if (pCurrChar->GetGuildId())                        // 清除错误的公会数据（公会不存在）
    {
        pCurrChar->SetInGuild(0);
        pCurrChar->SetRank(0);
    }

    // 如果玩家有公会，发送公会登录信息
    if (pCurrChar->GetGuildId() != 0)
    {
        if (Guild* guild = sGuildMgr->GetGuildById(pCurrChar->GetGuildId()))
            guild->SendLoginInfo(this);
        else
        {
            // 移除错误的公会数据
            TC_LOG_ERROR("network", "Player {} {} marked as member of not existing guild (id: {}), removing guild membership for player.", pCurrChar->GetName(), pCurrChar->GetGUID().ToString(), pCurrChar->GetGuildId());
            pCurrChar->SetInGuild(0);
        }
    }

    // 发送已学习的舞蹈动作数据包
    WorldPacket data(SMSG_LEARNED_DANCE_MOVES, 4+4);
    data << uint32(0);
    data << uint32(0);
    SendPacket(&data);

    // 发送加入地图前的初始数据包
    pCurrChar->SendInitialPacketsBeforeAddToMap();

    // 首次登录时显示开场动画
    if (!pCurrChar->getCinematic())
    {
        pCurrChar->setCinematic(1);

        // 根据职业或种族获取开场动画ID
        if (ChrClassesEntry const* cEntry = sChrClassesStore.LookupEntry(pCurrChar->GetClass()))
        {
            if (cEntry->CinematicSequenceID)
                pCurrChar->SendCinematicStart(cEntry->CinematicSequenceID);
            else if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(pCurrChar->GetRace()))
                pCurrChar->SendCinematicStart(rEntry->CinematicSequenceID);

            // 如果有新角色欢迎消息，发送它
            if (!sWorld->GetNewCharString().empty())
                chH.PSendSysMessage("%s", sWorld->GetNewCharString().c_str());
        }
    }

    // 尝试将玩家添加到地图
    if (!pCurrChar->GetMap()->AddPlayerToMap(pCurrChar))
    {
        // 添加失败时，传送到安全位置
        AreaTrigger const* at = sObjectMgr->GetGoBackTrigger(pCurrChar->GetMapId());
        if (at)
            pCurrChar->TeleportTo(at->target_mapId, at->target_X, at->target_Y, at->target_Z, pCurrChar->GetOrientation());
        else
            pCurrChar->TeleportTo(pCurrChar->m_homebindMapId, pCurrChar->m_homebindX, pCurrChar->m_homebindY, pCurrChar->m_homebindZ, pCurrChar->GetOrientation());
    }

    // 将角色添加到对象访问器
    ObjectAccessor::AddObject(pCurrChar);

    // 发送加入地图后的初始数据包
    pCurrChar->SendInitialPacketsAfterAddToMap();

    // 更新角色在线状态
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_ONLINE);
    stmt->setUInt32(0, pCurrChar->GetGUID().GetCounter());
    CharacterDatabase.Execute(stmt);

    // 更新账号在线状态
    LoginDatabasePreparedStatement* loginStmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_ONLINE);
    loginStmt->setUInt32(0, GetAccountId());
    LoginDatabase.Execute(loginStmt);

    // 设置游戏内时间
    pCurrChar->SetInGameTime(GameTime::GetGameTimeMS());

    // 通知队伍成员玩家上线（必须在添加到玩家列表之后，以便自己也能收到通知）
    if (Group* group = pCurrChar->GetGroup())
    {
        group->SendUpdate();
        group->ResetMaxEnchantingLevel();
        // 如果是队长，停止队长离线计时器
        if (group->GetLeaderGUID() == pCurrChar->GetGUID())
            group->StopLeaderOfflineTimer();
    }

    // 发送好友上线状态
    sSocialMgr->SendFriendStatus(pCurrChar, FRIEND_ONLINE, pCurrChar->GetGUID(), true);

    // 在一些对象加载之前将角色放入世界（并加载区域）
    pCurrChar->LoadCorpse(holder.GetPreparedResult(PLAYER_LOGIN_QUERY_LOAD_CORPSE_LOCATION));

    // 如果角色死亡，设置幽灵+水上行走
    if (pCurrChar->m_deathState == DEAD)
        pCurrChar->SetMovement(MOVE_WATER_WALK);

    // 继续飞行坐骑路线（如果角色在飞行中）
    pCurrChar->ContinueTaxiFlight();

    // 在加载宠物之前重置所有宠物（如果需要）
    if (pCurrChar->HasAtLoginFlag(AT_LOGIN_RESET_PET_TALENTS))
        Pet::resetTalentsForAllPetsOf(pCurrChar, nullptr, true);

    // 加载宠物（如果玩家死亡或在飞行中，宠物会暂时解散）
    pCurrChar->LoadPet();

    // 为非GM且非休息状态的玩家设置FFA PvP标志
    if (sWorld->IsFFAPvPRealm() && !pCurrChar->IsGameMaster() && !pCurrChar->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
        pCurrChar->SetPvpFlag(UNIT_BYTE2_FLAG_FFA_PVP);

    // 设置争夺中的PvP标志
    if (pCurrChar->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_CONTESTED_PVP))
        pCurrChar->SetContestedPvP();

    // 应用登录时标记的重置请求
    if (pCurrChar->HasAtLoginFlag(AT_LOGIN_RESET_SPELLS))
    {
        pCurrChar->ResetSpells();
        SendNotification(LANG_RESET_SPELLS);
    }

    if (pCurrChar->HasAtLoginFlag(AT_LOGIN_RESET_TALENTS))
    {
        pCurrChar->ResetTalents(true);
        pCurrChar->SendTalentsInfoData(false);              // 原始天赋已在SendInitialPacketsBeforeAddToMap中发送，此处重发重置状态
    }

    // 检查是否为首次登录
    bool firstLogin = pCurrChar->HasAtLoginFlag(AT_LOGIN_FIRST);
    if (firstLogin)
    {
        pCurrChar->RemoveAtLoginFlag(AT_LOGIN_FIRST);

        // 施放初始法术
        PlayerInfo const* info = sObjectMgr->GetPlayerInfo(pCurrChar->GetRace(), pCurrChar->GetClass());
        for (uint32 spellId : info->castSpells)
            pCurrChar->CastSpell(pCurrChar, spellId, true);

        // 如果启用，开始时探索所有地图区域
        if (sWorld->getBoolConfig(CONFIG_START_ALL_EXPLORED))
            for (uint8 i = 0; i < PLAYER_EXPLORED_ZONES_SIZE; ++i)
                pCurrChar->SetFlag(PLAYER_EXPLORED_ZONES_1 + i, 0xFFFFFFFF);

        // 如果启用StartAllReputation，设置最大相关声望
        if (sWorld->getBoolConfig(CONFIG_START_ALL_REP))
        {
            ReputationMgr& repMgr = pCurrChar->GetReputationMgr();
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 942), 42999, false); // Cenarion Expedition
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 935), 42999, false); // The Sha'tar
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 936), 42999, false); // Shattrath City
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1011), 42999, false); // Lower City
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 970), 42999, false); // Sporeggar
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 967), 42999, false); // The Violet Eye
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 989), 42999, false); // Keepers of Time
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 932), 42999, false); // The Aldor
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 934), 42999, false); // The Scryers
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1038), 42999, false); // Ogri'la
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1077), 42999, false); // Shattered Sun Offensive
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1106), 42999, false); // Argent Crusade
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1104), 42999, false); // Frenzyheart Tribe
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1090), 42999, false); // Kirin Tor
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1098), 42999, false); // Knights of the Ebon Blade
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1156), 42999, false); // The Ashen Verdict
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1073), 42999, false); // The Kalu'ak
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1105), 42999, false); // The Oracles
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1119), 42999, false); // The Sons of Hodir
            repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1091), 42999, false); // The Wyrmrest Accord

            // 根据阵营设置特定声望（如主城等）
            switch (pCurrChar->GetTeam())
            {
                case ALLIANCE:
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(  72), 42999, false); // Stormwind
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(  47), 42999, false); // Ironforge
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(  69), 42999, false); // Darnassus
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 930), 42999, false); // Exodar
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 730), 42999, false); // Stormpike Guard
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 978), 42999, false); // Kurenai
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(  54), 42999, false); // Gnomeregan Exiles
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 946), 42999, false); // Honor Hold
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1037), 42999, false); // Alliance Vanguard
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1068), 42999, false); // Explorers' League
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1126), 42999, false); // The Frostborn
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1094), 42999, false); // The Silver Covenant
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1050), 42999, false); // Valiance Expedition
                    break;
                case HORDE:
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(  76), 42999, false); // Orgrimmar
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(  68), 42999, false); // Undercity
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(  81), 42999, false); // Thunder Bluff
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 911), 42999, false); // Silvermoon City
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 729), 42999, false); // Frostwolf Clan
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 941), 42999, false); // The Mag'har
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 530), 42999, false); // Darkspear Trolls
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry( 947), 42999, false); // Thrallmar
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1052), 42999, false); // Horde Expedition
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1067), 42999, false); // The Hand of Vengeance
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1124), 42999, false); // The Sunreavers
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1064), 42999, false); // The Taunka
                    repMgr.SetOneFactionReputation(sFactionStore.LookupEntry(1085), 42999, false); // Warsong Offensive
                    break;
                default:
                    break;
            }
            repMgr.SendState(nullptr);
        }
    }

    // 如果计划了服务器关闭，显示关闭倒计时
    if (sWorld->IsShuttingDown())
        sWorld->ShutdownMsg(true, pCurrChar);

    if (sWorld->getBoolConfig(CONFIG_ALL_TAXI_PATHS))
        pCurrChar->SetTaxiCheater(true);

    if (pCurrChar->IsGameMaster())
        SendNotification(LANG_GM_ON);

    std::string IP_str = GetRemoteAddress();
    TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Login Character:[{}] {} Level: {}, XP: {}/{} ({} left)",
        GetAccountId(), IP_str, pCurrChar->GetName(), pCurrChar->GetGUID().ToString(), pCurrChar->GetLevel(),
        _player->GetXP(), _player->GetXPForNextLevel(), std::max(0, (int32)_player->GetXPForNextLevel() - (int32)_player->GetXP()));

    if (!pCurrChar->IsStandState() && !pCurrChar->HasUnitState(UNIT_STATE_STUNNED))
        pCurrChar->SetStandState(UNIT_STAND_STATE_STAND);

    m_playerLoading = false;

    // 处理登录成就（应该在加载完成后处理）
    _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ON_LOGIN, 1);

    // 如果加载的是死亡角色，加载完成后传送到墓地
    if (pCurrChar->getDeathState() == CORPSE)
    {
        pCurrChar->BuildPlayerRepop();
        pCurrChar->RepopAtGraveyard();
    }

    sScriptMgr->OnPlayerLogin(pCurrChar, firstLogin);

    TC_METRIC_EVENT("player_events", "Login", pCurrChar->GetName());
}

/**
 * @brief 发送特性系统状态数据包
 *
 * 发送客户端特性系统状态信息，包括投诉功能和语音功能的状态。
 */
void WorldSession::SendFeatureSystemStatus()
{
    WorldPackets::System::FeatureSystemStatus features;
    features.ComplaintStatus = COMPLAINT_ENABLED_WITH_AUTO_IGNORE;
    features.VoiceEnabled = false;
    SendPacket(features.Write());
}

/**
 * @brief 处理设置阵营交战状态操作码
 *
 * 响应客户端设置阵营为交战状态的请求(CMSG_SET_FACTION_ATWAR)。
 * 玩家可以在声望面板中将某些阵营标记为"交战"，从而主动攻击该阵营的NPC。
 *
 * @param recvData 接收到的网络数据包，包含阵营列表ID和交战标志
 */
void WorldSession::HandleSetFactionAtWar(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_SET_FACTION_ATWAR");

    uint32 repListID;
    uint8  flag;

    recvData >> repListID;
    recvData >> flag;

    GetPlayer()->GetReputationMgr().SetAtWar(repListID, flag != 0);
}

/**
 * @brief 处理设置阵营作弊操作码
 *
 * 这个函数可能从未被使用，该操作码可能不存在。
 * 如果被调用，会记录错误日志并发送当前声望状态。
 *
 * @param recvData 接收到的网络数据包（未使用）
 */
void WorldSession::HandleSetFactionCheat(WorldPacket& /*recvData*/)
{
    TC_LOG_ERROR("network", "WORLD SESSION: HandleSetFactionCheat, not expected call, please report.");
    GetPlayer()->GetReputationMgr().SendState(nullptr);
}

/**
 * @brief 处理教程标记操作码
 *
 * 响应客户端标记某个教程步骤已完成的请求(CMSG_TUTORIAL_FLAG)。
 * 教程系统用于引导新玩家了解游戏基本操作。
 *
 * @param recvData 接收到的网络数据包，包含教程步骤ID
 */
void WorldSession::HandleTutorialFlag(WorldPacket& recvData)
{
    uint32 data;
    recvData >> data;

    uint8 index = uint8(data / 32);
    if (index >= MAX_ACCOUNT_TUTORIAL_VALUES)
        return;

    uint32 value = (data % 32);

    uint32 flag = GetTutorialInt(index);
    flag |= (1 << value);
    SetTutorialInt(index, flag);
}

/**
 * @brief 处理清除教程操作码
 *
 * 响应客户端清除所有教程标记的请求(CMSG_TUTORIAL_CLEAR)。
 * 将所有教程步骤标记为已完成（全1表示全部看过）。
 *
 * @param recvData 接收到的网络数据包（未使用）
 */
void WorldSession::HandleTutorialClear(WorldPacket& /*recvData*/)
{
    for (uint8 i = 0; i < MAX_ACCOUNT_TUTORIAL_VALUES; ++i)
        SetTutorialInt(i, 0xFFFFFFFF);
}

/**
 * @brief 处理重置教程操作码
 *
 * 响应客户端重置所有教程标记的请求(CMSG_TUTORIAL_RESET)。
 * 将所有教程步骤标记为未完成（全0表示全部未看）。
 *
 * @param recvData 接收到的网络数据包（未使用）
 */
void WorldSession::HandleTutorialReset(WorldPacket& /*recvData*/)
{
    for (uint8 i = 0; i < MAX_ACCOUNT_TUTORIAL_VALUES; ++i)
        SetTutorialInt(i, 0x00000000);
}

/**
 * @brief 处理设置观察阵营操作码
 *
 * 响应客户端设置要观察的阵营的请求(CMSG_SET_WATCHED_FACTION)。
 * 玩家可以在声望面板中选择一个阵营进行观察，该阵营的声望条会显示在主界面。
 *
 * @param recvData 接收到的网络数据包，包含要观察的阵营索引
 */
void WorldSession::HandleSetWatchedFactionOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_SET_WATCHED_FACTION");
    uint32 fact;
    recvData >> fact;
    GetPlayer()->SetUInt32Value(PLAYER_FIELD_WATCHED_FACTION_INDEX, fact);
}

/**
 * @brief 处理设置阵营非活动状态操作码
 *
 * 响应客户端设置阵营为非活动状态的请求(CMSG_SET_FACTION_INACTIVE)。
 * 玩家可以将不常用的阵营标记为非活动，在声望面板中折叠显示。
 *
 * @param recvData 接收到的网络数据包，包含阵营列表ID和非活动标志
 */
void WorldSession::HandleSetFactionInactiveOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_SET_FACTION_INACTIVE");
    uint32 replistid;
    uint8 inactive;
    recvData >> replistid >> inactive;

    _player->GetReputationMgr().SetInactive(replistid, inactive != 0);
}

/**
 * @brief 处理显示头盔操作码
 *
 * 响应客户端切换是否显示头盔的请求(CMSG_SHOWING_HELM)。
 * 玩家可以隐藏头盔外观，但仍保留头盔的属性效果。
 *
 * @param packet 接收到的网络数据包，包含是否显示头盔的标志
 */
void WorldSession::HandleShowingHelmOpcode(WorldPackets::Character::ShowingHelm& packet)
{
    if (packet.ShowHelm)
        _player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);
    else
        _player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);
}

/**
 * @brief 处理显示披风操作码
 *
 * 响应客户端切换是否显示披风的请求(CMSG_SHOWING_CLOAK)。
 * 玩家可以隐藏披风外观，但仍保留披风的属性效果。
 *
 * @param packet 接收到的网络数据包，包含是否显示披风的标志
 */
void WorldSession::HandleShowingCloakOpcode(WorldPackets::Character::ShowingCloak& packet)
{
    if (packet.ShowCloak)
        _player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);
    else
        _player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);
}

/**
 * @brief 处理角色重命名操作码
 *
 * 响应客户端请求重命名角色的请求(CMSG_CHAR_RENAME)。
 * 主要流程：
 * 1. 解析角色GUID和新名字
 * 2. 验证名字合法性和保留名检查
 * 3. 异步查询数据库验证角色归属和名字唯一性
 * 4. 通过回调函数处理数据库结果
 *
 * @param recvData 接收到的网络数据包，包含角色GUID和新名字
 */
void WorldSession::HandleCharRenameOpcode(WorldPacket& recvData)
{
    std::shared_ptr<CharacterRenameInfo> renameInfo = std::make_shared<CharacterRenameInfo>();

    recvData >> renameInfo->Guid
             >> renameInfo->Name;

    // 防止角色重命名为无效名字
    if (!normalizePlayerName(renameInfo->Name))
    {
        SendCharRename(CHAR_NAME_NO_NAME, renameInfo.get());
        return;
    }

    ResponseCodes res = ObjectMgr::CheckPlayerName(renameInfo->Name, GetSessionDbcLocale(), true);
    if (res != CHAR_NAME_SUCCESS)
    {
        SendCharRename(res, renameInfo.get());
        return;
    }

    // 检查名字限制（保留名）
    if (!HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_RESERVEDNAME) && sObjectMgr->IsReservedName(renameInfo->Name))
    {
        SendCharRename(CHAR_NAME_RESERVED, renameInfo.get());
        return;
    }

    // 验证角色属于当前账号、已启用登录时重命名、且没有同名角色
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_FREE_NAME);

    stmt->setUInt32(0, renameInfo->Guid.GetCounter());
    stmt->setUInt32(1, GetAccountId());
    stmt->setString(2, renameInfo->Name);

    _queryProcessor.AddCallback(CharacterDatabase.AsyncQuery(stmt)
        .WithPreparedCallback(std::bind(&WorldSession::HandleCharRenameCallBack, this, renameInfo, std::placeholders::_1)));
}

/**
 * @brief 处理角色重命名回调
 *
 * 处理重命名数据库查询的结果。
 * 主要流程：
 * 1. 验证查询结果有效性和登录标志
 * 2. 更新数据库中的角色名称和登录标志
 * 3. 删除旧的变格名字记录
 * 4. 更新角色缓存
 * 5. 发送重命名结果给客户端
 *
 * @param renameInfo 重命名信息结构体
 * @param result 数据库查询结果
 */
void WorldSession::HandleCharRenameCallBack(std::shared_ptr<CharacterRenameInfo> renameInfo, PreparedQueryResult result)
{
    if (!result)
    {
        SendCharRename(CHAR_CREATE_ERROR, renameInfo.get());
        return;
    }

    Field* fields = result->Fetch();

    ObjectGuid::LowType guidLow = fields[0].GetUInt32();
    std::string oldName = fields[1].GetString();
    uint16 atLoginFlags = fields[2].GetUInt16();

    if (!(atLoginFlags & AT_LOGIN_RENAME))
    {
        SendCharRename(CHAR_CREATE_ERROR, renameInfo.get());
        return;
    }

    atLoginFlags &= ~AT_LOGIN_RENAME;

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 更新数据库中的名字和登录标志
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_NAME_AT_LOGIN);

    stmt->setString(0, renameInfo->Name);
    stmt->setUInt16(1, atLoginFlags);
    stmt->setUInt32(2, guidLow);

    CharacterDatabase.Execute(stmt);

    // 从数据库中删除变格名字
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_DECLINED_NAME);

    stmt->setUInt32(0, guidLow);

    CharacterDatabase.Execute(stmt);

    TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Character:[{}] ({}) Changed name to: {}", GetAccountId(), GetRemoteAddress(), oldName, renameInfo->Guid.ToString(), renameInfo->Name);

    SendCharRename(RESPONSE_SUCCESS, renameInfo.get());

    sCharacterCache->UpdateCharacterData(renameInfo->Guid, renameInfo->Name);
}

/**
 * @brief 处理设置玩家变格名字操作码
 *
 * 响应客户端设置角色变格名字的请求(CMSG_SET_PLAYER_DECLINED_NAMES)。
 * 变格名字系统主要用于俄语等需要名字变格的语言环境。
 * 主要流程：
 * 1. 验证角色是否存在且名字为西里尔字母
 * 2. 解析并验证变格名字格式
 * 3. 保存变格名字到数据库
 * 4. 发送设置结果给客户端
 *
 * @param recvData 接收到的网络数据包，包含角色GUID和变格名字数据
 */
void WorldSession::HandleSetPlayerDeclinedNames(WorldPacket& recvData)
{
    ObjectGuid guid;

    recvData >> guid;

    // not accept declined names for unsupported languages
    std::string name;
    if (!sCharacterCache->GetCharacterNameByGuid(guid, name))
    {
        SendSetPlayerDeclinedNamesResult(DECLINED_NAMES_RESULT_ERROR, guid);
        return;
    }

    std::wstring wname;
    if (!Utf8toWStr(name, wname))
    {
        SendSetPlayerDeclinedNamesResult(DECLINED_NAMES_RESULT_ERROR, guid);
        return;
    }

    if (!isCyrillicCharacter(wname[0]))                      // name already stored as only single alphabet using
    {
        SendSetPlayerDeclinedNamesResult(DECLINED_NAMES_RESULT_ERROR, guid);
        return;
    }

    std::string name2;
    DeclinedName declinedname;

    recvData >> name2;

    if (name2 != name)                                       // character have different name
    {
        SendSetPlayerDeclinedNamesResult(DECLINED_NAMES_RESULT_ERROR, guid);
        return;
    }

    for (int i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
    {
        recvData >> declinedname.name[i];
        if (!normalizePlayerName(declinedname.name[i]))
        {
            SendSetPlayerDeclinedNamesResult(DECLINED_NAMES_RESULT_ERROR, guid);
            return;
        }
    }

    if (!ObjectMgr::CheckDeclinedNames(wname, declinedname))
    {
        SendSetPlayerDeclinedNamesResult(DECLINED_NAMES_RESULT_ERROR, guid);
        return;
    }

    for (int i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
        CharacterDatabase.EscapeString(declinedname.name[i]);

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_DECLINED_NAME);
    stmt->setUInt32(0, guid.GetCounter());
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_DECLINED_NAME);
    stmt->setUInt32(0, guid.GetCounter());

    for (uint8 i = 0; i < 5; i++)
        stmt->setString(i+1, declinedname.name[i]);

    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);

    SendSetPlayerDeclinedNamesResult(DECLINED_NAMES_RESULT_SUCCESS, guid);
}

/**
 * @brief 处理改变外观操作码
 *
 * 响应客户端在理发店改变角色外观的请求(CMSG_ALTER_APPEARANCE)。
 * 主要流程：
 * 1. 从数据包解析新的发型、发色、面部毛发和肤色
 * 2. 验证外观选项是否有效（种族、性别匹配）
 * 3. 检查玩家是否坐在理发椅上
 * 4. 计算并扣除费用
 * 5. 更新角色外观
 * 6. 更新成就进度
 *
 * @param recvData 接收到的网络数据包，包含新的外观数据
 */
void WorldSession::HandleAlterAppearance(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_ALTER_APPEARANCE");

    uint32 Hair, Color, FacialHair, SkinColor;
    recvData >> Hair >> Color >> FacialHair >> SkinColor;

    BarberShopStyleEntry const* bs_hair = sBarberShopStyleStore.LookupEntry(Hair);

    if (!bs_hair || bs_hair->Type != 0 || bs_hair->Race != _player->GetRace() || Gender(bs_hair->Sex) != _player->GetNativeGender())
        return;

    BarberShopStyleEntry const* bs_facialHair = sBarberShopStyleStore.LookupEntry(FacialHair);

    if (!bs_facialHair || bs_facialHair->Type != 2 || bs_facialHair->Race != _player->GetRace() || Gender(bs_facialHair->Sex) != _player->GetNativeGender())
        return;

    BarberShopStyleEntry const* bs_skinColor = sBarberShopStyleStore.LookupEntry(SkinColor);

    if (bs_skinColor && (bs_skinColor->Type != 3 || bs_skinColor->Race != _player->GetRace() || Gender(bs_skinColor->Sex) != _player->GetNativeGender()))
        return;

    if (!Player::ValidateAppearance(_player->GetRace(), _player->GetClass(), _player->GetNativeGender(),
        bs_hair->Data, Color, _player->GetFaceId(), bs_facialHair->Data,
        bs_skinColor ? bs_skinColor->Data : _player->GetSkinId()))
        return;

    GameObject* go = _player->FindNearestGameObjectOfType(GAMEOBJECT_TYPE_BARBER_CHAIR, 5.0f);
    if (!go)
    {
        SendBarberShopResult(BARBER_SHOP_RESULT_NOT_ON_CHAIR);
        return;
    }

    if (_player->GetStandState() != UNIT_STAND_STATE_SIT_LOW_CHAIR + go->GetGOInfo()->barberChair.chairheight)
    {
        SendBarberShopResult(BARBER_SHOP_RESULT_NOT_ON_CHAIR);
        return;
    }

    uint32 cost = _player->GetBarberShopCost(bs_hair->Data, Color, bs_facialHair->Data, bs_skinColor);

    // 0 - 成功
    // 1, 3 - 金钱不足
    // 2 - 必须坐在理发椅上
    if (!_player->HasEnoughMoney(cost))
    {
        SendBarberShopResult(BARBER_SHOP_RESULT_NO_MONEY);
        return;
    }

    SendBarberShopResult(BARBER_SHOP_RESULT_SUCCESS);

    _player->ModifyMoney(-int32(cost));                     // 理发不是免费的
    _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_AT_BARBER, cost);

    _player->SetHairStyleId(uint8(bs_hair->Data));
    _player->SetHairColorId(uint8(Color));
    _player->SetFacialStyle(uint8(bs_facialHair->Data));
    if (bs_skinColor)
        _player->SetSkinId(uint8(bs_skinColor->Data));

    _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_VISIT_BARBER_SHOP, 1);

    _player->SetStandState(UNIT_STAND_STATE_STAND);
}

/**
 * @brief 处理移除雕文操作码
 *
 * 响应客户端移除某个雕文槽中雕文的请求(CMSG_REMOVE_GLYPH)。
 * 主要流程：
 * 1. 从数据包解析雕文槽位索引
 * 2. 验证槽位索引是否有效
 * 3. 移除该雕文关联的法术光环
 * 4. 清空雕文槽
 * 5. 发送更新后的天赋信息
 *
 * @param recvData 接收到的网络数据包，包含要移除的雕文槽位
 */
void WorldSession::HandleRemoveGlyph(WorldPacket& recvData)
{
    uint32 slot;
    recvData >> slot;

    if (slot >= MAX_GLYPH_SLOT_INDEX)
    {
        TC_LOG_DEBUG("network", "Client sent wrong glyph slot number in opcode CMSG_REMOVE_GLYPH {}", slot);
        return;
    }

    if (uint32 glyph = _player->GetGlyph(slot))
    {
        if (GlyphPropertiesEntry const* gp = sGlyphPropertiesStore.LookupEntry(glyph))
        {
            _player->RemoveAurasDueToSpell(gp->SpellID);
            _player->SetGlyph(slot, 0);
            _player->SendTalentsInfoData(false);
        }
    }
}

/**
 * @brief 处理角色自定义外观操作码
 *
 * 响应客户端请求自定义角色外观的请求(CMSG_CHAR_CUSTOMIZE)。
 * 这是在角色选择界面进行的付费服务，可以改变角色的性别和外观。
 * 主要流程：
 * 1. 解析角色GUID和新的外观数据
 * 2. 验证角色归属
 * 3. 异步查询数据库验证自定义标志
 * 4. 通过回调函数处理数据库结果
 *
 * @param recvData 接收到的网络数据包，包含角色GUID和外观数据
 */
void WorldSession::HandleCharCustomize(WorldPacket& recvData)
{
    std::shared_ptr<CharacterCustomizeInfo> customizeInfo = std::make_shared<CharacterCustomizeInfo>();

    recvData >> customizeInfo->Guid;
    if (!IsLegitCharacterForAccount(customizeInfo->Guid))
    {
        TC_LOG_ERROR("entities.player.cheat", "Account {}, IP: {} tried to customise {}, but it does not belong to their account!",
            GetAccountId(), GetRemoteAddress(), customizeInfo->Guid.ToString());
        recvData.rfinish();
        KickPlayer("WorldSession::HandleCharCustomize Trying to customise character of another account");
        return;
    }

    recvData >> customizeInfo->Name
             >> customizeInfo->Gender
             >> customizeInfo->Skin
             >> customizeInfo->HairColor
             >> customizeInfo->HairStyle
             >> customizeInfo->FacialHair
             >> customizeInfo->Face;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_CUSTOMIZE_INFO);
    stmt->setUInt32(0, customizeInfo->Guid.GetCounter());

    _queryProcessor.AddCallback(CharacterDatabase.AsyncQuery(stmt)
        .WithPreparedCallback(std::bind(&WorldSession::HandleCharCustomizeCallback, this, customizeInfo, std::placeholders::_1)));
}

/**
 * @brief 处理角色自定义外观回调
 *
 * 处理自定义外观数据库查询的结果。
 * 主要流程：
 * 1. 验证查询结果和外观有效性
 * 2. 检查自定义登录标志
 * 3. 验证名字合法性（如果改名）
 * 4. 更新数据库中的外观和名字
 * 5. 更新角色缓存
 * 6. 发送自定义结果给客户端
 *
 * @param customizeInfo 自定义外观信息结构体
 * @param result 数据库查询结果
 */
void WorldSession::HandleCharCustomizeCallback(std::shared_ptr<CharacterCustomizeInfo> customizeInfo, PreparedQueryResult result)
{
    if (!result)
    {
        SendCharCustomize(CHAR_CREATE_ERROR, customizeInfo.get());
        return;
    }

    Field* fields = result->Fetch();
    std::string oldName = fields[0].GetString();
    uint8 plrRace = fields[1].GetUInt8();
    uint8 plrClass = fields[2].GetUInt8();
    uint8 plrGender = fields[3].GetUInt8();
    uint16 atLoginFlags = fields[4].GetUInt16();

    if (!Player::ValidateAppearance(plrRace, plrClass, plrGender, customizeInfo->HairStyle, customizeInfo->HairColor, customizeInfo->Face, customizeInfo->FacialHair, customizeInfo->Skin, true))
    {
        SendCharCustomize(CHAR_CREATE_ERROR, customizeInfo.get());
        return;
    }

    if (!(atLoginFlags & AT_LOGIN_CUSTOMIZE))
    {
        SendCharCustomize(CHAR_CREATE_ERROR, customizeInfo.get());
        return;
    }

    atLoginFlags &= ~AT_LOGIN_CUSTOMIZE;

    // 防止角色重命名（如果配置禁止）
    if (sWorld->getBoolConfig(CONFIG_PREVENT_RENAME_CUSTOMIZATION) && (customizeInfo->Name != oldName))
    {
        SendCharCustomize(CHAR_NAME_FAILURE, customizeInfo.get());
        return;
    }

    // 防止角色重命名为无效名字
    if (!normalizePlayerName(customizeInfo->Name))
    {
        SendCharCustomize(CHAR_NAME_NO_NAME, customizeInfo.get());
        return;
    }

    ResponseCodes res = ObjectMgr::CheckPlayerName(customizeInfo->Name, GetSessionDbcLocale(), true);
    if (res != CHAR_NAME_SUCCESS)
    {
        SendCharCustomize(res, customizeInfo.get());
        return;
    }

    // 检查名字限制（保留名）
    if (!HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_RESERVEDNAME) && sObjectMgr->IsReservedName(customizeInfo->Name))
    {
        SendCharCustomize(CHAR_NAME_RESERVED, customizeInfo.get());
        return;
    }

    // 检查是否已存在同名角色
    if (ObjectGuid newGuid = sCharacterCache->GetCharacterGuidByName(customizeInfo->Name))
    {
        if (newGuid != customizeInfo->Guid)
        {
            SendCharCustomize(CHAR_CREATE_NAME_IN_USE, customizeInfo.get());
            return;
        }
    }

    CharacterDatabasePreparedStatement* stmt = nullptr;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    ObjectGuid::LowType lowGuid = customizeInfo->Guid.GetCounter();

    /// 自定义外观
    Player::Customize(customizeInfo.get(), trans);

    /// 名字变更和更新登录标志
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_NAME_AT_LOGIN);
        stmt->setString(0, customizeInfo->Name);
        stmt->setUInt16(1, atLoginFlags);
        stmt->setUInt32(2, lowGuid);

        trans->Append(stmt);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_DECLINED_NAME);
        stmt->setUInt32(0, lowGuid);

        trans->Append(stmt);
    }

    CharacterDatabase.CommitTransaction(trans);

    sCharacterCache->UpdateCharacterData(customizeInfo->Guid, customizeInfo->Name, customizeInfo->Gender);

    SendCharCustomize(RESPONSE_SUCCESS, customizeInfo.get());

    TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}), Character[{}] ({}) Customized to: {}",
        GetAccountId(), GetRemoteAddress(), oldName, customizeInfo->Guid.ToString(), customizeInfo->Name);
}

/**
 * @brief 处理保存装备方案操作码
 *
 * 响应客户端保存装备管理器方案的请求(CMSG_EQUIPMENT_SET_SAVE)。
 * 装备管理器允许玩家保存多套装备配置，快速切换装备。
 * 主要流程：
 * 1. 解析方案GUID、索引、名称和图标
 * 2. 解析各装备槽位的物品GUID或忽略标志
 * 3. 验证物品是否真实存在于玩家背包
 * 4. 保存或更新装备方案
 *
 * @param recvData 接收到的网络数据包，包含装备方案数据
 */
void WorldSession::HandleEquipmentSetSave(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_EQUIPMENT_SET_SAVE");

    uint64 setGuid;
    recvData.readPackGUID(setGuid);

    uint32 index;
    recvData >> index;
    if (index >= MAX_EQUIPMENT_SET_INDEX)                    // client set slots amount
        return;

    std::string name;
    recvData >> name;

    std::string iconName;
    recvData >> iconName;

    EquipmentSetInfo::EquipmentSetData eqData;
    eqData.Guid    = setGuid;
    eqData.SetID   = index;
    eqData.SetName = name;
    eqData.SetIcon = iconName;

    for (uint32 i = 0; i < EQUIPMENT_SLOT_END; ++i)
    {
        ObjectGuid itemGuid;
        recvData >> itemGuid.ReadAsPacked();

        // 如果客户端发送0，表示空槽位
        if (itemGuid.IsEmpty())
            continue;

        // 装备管理器发送"1"（作为原始GUID）表示设置为"忽略"的槽位（装备方案时不触碰该槽位）
        if (itemGuid.GetRawValue() == 1)
        {
            // 忽略的槽位保存为位掩码，因为Items[i]没有空闲的特殊值
            eqData.IgnoreMask |= 1 << i;
            continue;
        }

        // 作弊检查
        Item* item = _player->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (!item || item->GetGUID() != itemGuid)
            continue;

        eqData.Pieces[i] = itemGuid;
    }

    _player->SetEquipmentSet(eqData);
}

/**
 * @brief 处理删除装备方案操作码
 *
 * 响应客户端删除装备管理器方案的请求(CMSG_EQUIPMENT_SET_DELETE)。
 *
 * @param recvData 接收到的网络数据包，包含要删除的装备方案GUID
 */
void WorldSession::HandleEquipmentSetDelete(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_EQUIPMENT_SET_DELETE");

    uint64 setGuid;
    recvData.readPackGUID(setGuid);

    _player->DeleteEquipmentSet(setGuid);
}

/**
 * @brief 处理使用装备方案操作码
 *
 * 响应客户端使用装备管理器方案快速切换装备的请求(CMSG_EQUIPMENT_SET_USE)。
 * 主要流程：
 * 1. 遍历每个装备槽位
 * 2. 对于标记为"忽略"的槽位跳过处理
 * 3. 在战斗中只能切换武器
 * 4. 卸下当前槽位的装备到背包
 * 5. 装备目标槽位的物品
 * 6. 发送装备使用结果
 *
 * @param recvData 接收到的网络数据包，包含各槽位的装备信息
 */
void WorldSession::HandleEquipmentSetUse(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_EQUIPMENT_SET_USE");

    for (uint32 i = 0; i < EQUIPMENT_SLOT_END; ++i)
    {
        ObjectGuid itemGuid;
        recvData >> itemGuid.ReadAsPacked();

        uint8 srcbag, srcslot;
        recvData >> srcbag >> srcslot;

        TC_LOG_DEBUG("entities.player.items", "{}: srcbag {}, srcslot {}", itemGuid.ToString(), srcbag, srcslot);

        // 检查槽位是否设置为"忽略"（原始值==1），如果是则不应卸下装备
        if (itemGuid.GetRawValue() == 1)
            continue;

        // 战斗中只能装备武器
        if (_player->IsInCombat() && i != EQUIPMENT_SLOT_MAINHAND && i != EQUIPMENT_SLOT_OFFHAND && i != EQUIPMENT_SLOT_RANGED)
            continue;

        Item* item = _player->GetItemByGuid(itemGuid);

        uint16 dstpos = i | (INVENTORY_SLOT_BAG_0 << 8);

        if (!item)
        {
            Item* uItem = _player->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
            if (!uItem)
                continue;

            ItemPosCountVec sDest;
            InventoryResult msg = _player->CanStoreItem(NULL_BAG, NULL_SLOT, sDest, uItem, false);
            if (msg == EQUIP_ERR_OK)
            {
                if (_player->CanUnequipItem(dstpos, true) != EQUIP_ERR_OK)
                    continue;

                _player->RemoveItem(INVENTORY_SLOT_BAG_0, i, true);
                _player->StoreItem(sDest, uItem, true);
            }
            else
                _player->SendEquipError(msg, uItem, nullptr);

            continue;
        }

        if (item->GetPos() == dstpos)
            continue;

        if (_player->CanEquipItem(i, dstpos, item, true) != EQUIP_ERR_OK)
            continue;

        _player->SwapItem(item->GetPos(), dstpos);
    }

    WorldPacket data(SMSG_EQUIPMENT_SET_USE_RESULT, 1);
    data << uint8(0);                                       // 0=成功，4=装备交换失败-背包已满
    SendPacket(&data);
}

/**
 * @brief 处理阵营或种族变更操作码
 *
 * 响应客户端请求阵营或种族变更的请求(CMSG_CHAR_FACTION_CHANGE或CMSG_CHAR_RACE_CHANGE)。
 * 这是付费服务，允许玩家转换阵营（联盟/部落）或仅变更种族。
 * 主要流程：
 * 1. 解析角色GUID和新的种族、外观数据
 * 2. 验证角色归属
 * 3. 异步查询数据库获取当前角色信息
 * 4. 通过回调函数处理阵营/种族转换逻辑
 *
 * @param recvData 接收到的网络数据包，包含角色GUID和新外观数据
 */
void WorldSession::HandleCharFactionOrRaceChange(WorldPacket& recvData)
{
    std::shared_ptr<CharacterFactionChangeInfo> factionChangeInfo = std::make_shared<CharacterFactionChangeInfo>();
    recvData >> factionChangeInfo->Guid;

    if (!IsLegitCharacterForAccount(factionChangeInfo->Guid))
    {
        TC_LOG_ERROR("entities.player.cheat", "Account {}, IP: {} tried to factionchange character {}, but it does not belong to their account!",
            GetAccountId(), GetRemoteAddress(), factionChangeInfo->Guid.ToString());
        recvData.rfinish();
        KickPlayer("WorldSession::HandleCharFactionOrRaceChange Trying to change faction of character of another account");
        return;
    }

    recvData >> factionChangeInfo->Name
             >> factionChangeInfo->Gender
             >> factionChangeInfo->Skin
             >> factionChangeInfo->HairColor
             >> factionChangeInfo->HairStyle
             >> factionChangeInfo->FacialHair
             >> factionChangeInfo->Face
             >> factionChangeInfo->Race;

    factionChangeInfo->FactionChange = (recvData.GetOpcode() == CMSG_CHAR_FACTION_CHANGE);

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_RACE_OR_FACTION_CHANGE_INFOS);
    stmt->setUInt32(0, factionChangeInfo->Guid.GetCounter());

    _queryProcessor.AddCallback(CharacterDatabase.AsyncQuery(stmt)
        .WithPreparedCallback(std::bind(&WorldSession::HandleCharFactionOrRaceChangeCallback, this, factionChangeInfo, std::placeholders::_1)));
}

/**
 * @brief 处理阵营或种族变更回调
 *
 * 处理阵营/种族变更数据库查询的结果。
 * 主要流程：
 * 1. 验证查询结果和新种族/职业组合有效性
 * 2. 检查登录标志和阵营变更是否有效
 * 3. 验证名字合法性
 * 4. 检查竞技场队长限制
 * 5. 执行阵营转换操作：
 *    - 复活角色
 *    - 更新名字、外观和种族
 *    - 转换语言技能
 *    - 重置飞行路径
 *    - 离开公会/竞技场队伍/好友列表
 *    - 重置炉石位置
 *    - 转换成就、物品、任务、法术、声望和头衔
 * 6. 更新角色缓存
 * 7. 发送变更结果给客户端
 *
 * @param factionChangeInfo 阵营变更信息结构体
 * @param result 数据库查询结果
 */
void WorldSession::HandleCharFactionOrRaceChangeCallback(std::shared_ptr<CharacterFactionChangeInfo> factionChangeInfo, PreparedQueryResult result)
{
    if (!result)
    {
        SendCharFactionChange(CHAR_CREATE_ERROR, factionChangeInfo.get());
        return;
    }

    // get the players old (at this moment current) race
    CharacterCacheEntry const* characterInfo = sCharacterCache->GetCharacterCacheByGuid(factionChangeInfo->Guid);
    if (!characterInfo)
    {
        SendCharFactionChange(CHAR_CREATE_ERROR, factionChangeInfo.get());
        return;
    }

    uint8 oldRace     = characterInfo->Race;
    uint8 playerClass = characterInfo->Class;
    uint8 level       = characterInfo->Level;
    //std::string oldName = characterInfo->Name;

    if (!sObjectMgr->GetPlayerInfo(factionChangeInfo->Race, playerClass))
    {
        SendCharFactionChange(CHAR_CREATE_ERROR, factionChangeInfo.get());
        return;
    }

    Field* fields              = result->Fetch();
    uint32 atLoginFlags        = fields[0].GetUInt16();
    std::string knownTitlesStr = fields[1].GetString();
    uint32 groupId             = !fields[2].IsNull() ? fields[2].GetUInt32() : 0;

    uint32 usedLoginFlag = (factionChangeInfo->FactionChange ? AT_LOGIN_CHANGE_FACTION : AT_LOGIN_CHANGE_RACE);
    if (!(atLoginFlags & usedLoginFlag))
    {
        SendCharFactionChange(CHAR_CREATE_ERROR, factionChangeInfo.get());
        return;
    }

    uint32 newTeam = Player::TeamForRace(factionChangeInfo->Race);
    if (factionChangeInfo->FactionChange == (Player::TeamForRace(oldRace) == newTeam))
    {
        SendCharFactionChange(factionChangeInfo->FactionChange ? CHAR_CREATE_CHARACTER_SWAP_FACTION : CHAR_CREATE_CHARACTER_RACE_ONLY, factionChangeInfo.get());
        return;
    }

    if (!HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_RACEMASK))
    {
        uint32 raceMaskDisabled = sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_DISABLED_RACEMASK);
        if ((1 << (factionChangeInfo->Race - 1)) & raceMaskDisabled)
        {
            SendCharFactionChange(CHAR_CREATE_ERROR, factionChangeInfo.get());
            return;
        }
    }

    // 防止角色重命名（如果配置禁止）
    if (sWorld->getBoolConfig(CONFIG_PREVENT_RENAME_CUSTOMIZATION) && (factionChangeInfo->Name != characterInfo->Name))
    {
        SendCharFactionChange(CHAR_NAME_FAILURE, factionChangeInfo.get());
        return;
    }

    // 防止角色重命名为无效名字
    if (!normalizePlayerName(factionChangeInfo->Name))
    {
        SendCharFactionChange(CHAR_NAME_NO_NAME, factionChangeInfo.get());
        return;
    }

    ResponseCodes res = ObjectMgr::CheckPlayerName(factionChangeInfo->Name, GetSessionDbcLocale(), true);
    if (res != CHAR_NAME_SUCCESS)
    {
        SendCharFactionChange(res, factionChangeInfo.get());
        return;
    }

    // 检查名字限制（保留名）
    if (!HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_RESERVEDNAME) && sObjectMgr->IsReservedName(factionChangeInfo->Name))
    {
        SendCharFactionChange(CHAR_NAME_RESERVED, factionChangeInfo.get());
        return;
    }

    // 检查是否已存在同名角色
    ObjectGuid newGuid = sCharacterCache->GetCharacterGuidByName(factionChangeInfo->Name);
    if (!newGuid.IsEmpty())
    {
        if (newGuid != factionChangeInfo->Guid)
        {
            SendCharFactionChange(CHAR_CREATE_NAME_IN_USE, factionChangeInfo.get());
            return;
        }
    }

    if (sArenaTeamMgr->GetArenaTeamByCaptain(factionChangeInfo->Guid))
    {
        SendCharFactionChange(CHAR_CREATE_CHARACTER_ARENA_LEADER, factionChangeInfo.get());
        return;
    }

    // 所有检查通过，现在处理种族变更
    ObjectGuid::LowType lowGuid = factionChangeInfo->Guid.GetCounter();

    CharacterDatabasePreparedStatement* stmt = nullptr;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 如果角色已死亡，复活角色
    Player::OfflineResurrect(factionChangeInfo->Guid, trans);

    // 更新名字和登录标志
    {
        CharacterDatabase.EscapeString(factionChangeInfo->Name);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_NAME_AT_LOGIN);
        stmt->setString(0, factionChangeInfo->Name);
        stmt->setUInt16(1, uint16((atLoginFlags | AT_LOGIN_RESURRECT) & ~usedLoginFlag));
        stmt->setUInt32(2, lowGuid);
        trans->Append(stmt);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_DECLINED_NAME);
        stmt->setUInt32(0, lowGuid);
        trans->Append(stmt);
    }

    // 自定义外观
    Player::Customize(factionChangeInfo.get(), trans);

    // 种族变更
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_RACE);
        stmt->setUInt8(0, factionChangeInfo->Race);
        stmt->setUInt16(1, PLAYER_EXTRA_HAS_RACE_CHANGED);
        stmt->setUInt32(2, lowGuid);
        trans->Append(stmt);
    }

    sCharacterCache->UpdateCharacterData(factionChangeInfo->Guid, factionChangeInfo->Name, factionChangeInfo->Gender, factionChangeInfo->Race);

    if (oldRace != factionChangeInfo->Race)
    {
        // 切换语言
        // 首先删除所有语言技能
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_SKILL_LANGUAGES);
        stmt->setUInt32(0, lowGuid);
        trans->Append(stmt);

        // 重新添加语言技能
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_SKILL_LANGUAGE);
        stmt->setUInt32(0, lowGuid);

        // 阵营特定语言（部落：兽人语，联盟：通用语）
        if (newTeam == HORDE)
            stmt->setUInt16(1, 109);
        else
            stmt->setUInt16(1, 98);

        trans->Append(stmt);

        // 种族特定语言
        if (factionChangeInfo->Race != RACE_ORC && factionChangeInfo->Race != RACE_HUMAN)
        {
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_SKILL_LANGUAGE);
            stmt->setUInt32(0, lowGuid);

            switch (factionChangeInfo->Race)
            {
                case RACE_DWARF:
                    stmt->setUInt16(1, 111);
                    break;
                case RACE_DRAENEI:
                    stmt->setUInt16(1, 759);
                    break;
                case RACE_GNOME:
                    stmt->setUInt16(1, 313);
                    break;
                case RACE_NIGHTELF:
                    stmt->setUInt16(1, 113);
                    break;
                case RACE_UNDEAD_PLAYER:
                    stmt->setUInt16(1, 673);
                    break;
                case RACE_TAUREN:
                    stmt->setUInt16(1, 115);
                    break;
                case RACE_TROLL:
                    stmt->setUInt16(1, 315);
                    break;
                case RACE_BLOODELF:
                    stmt->setUInt16(1, 137);
                    break;
            }

            trans->Append(stmt);
        }

        if (factionChangeInfo->FactionChange)
        {
            // 删除所有飞行路径
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_TAXI_PATH);
            stmt->setUInt32(0, lowGuid);
            trans->Append(stmt);

            if (level > 7)
            {
                // 更新飞行路径
                // 这可能不是100%符合暴雪原版，但无法完全避免
                std::ostringstream taximaskstream;
                uint32 numFullTaximasks = level / 7;
                if (numFullTaximasks > 11)
                    numFullTaximasks = 11;

                TaxiMask const& factionMask = newTeam == HORDE ? sHordeTaxiNodesMask : sAllianceTaxiNodesMask;
                for (uint8 i = 0; i < numFullTaximasks; ++i)
                {
                    uint8 deathKnightExtraNode = (playerClass == CLASS_DEATH_KNIGHT) ? sDeathKnightTaxiNodesMask[i] : 0;
                    taximaskstream << uint32(factionMask[i] | deathKnightExtraNode) << ' ';
                }

                uint32 numEmptyTaximasks = 11 - numFullTaximasks;
                for (uint8 i = 0; i < numEmptyTaximasks; ++i)
                    taximaskstream << "0 ";
                taximaskstream << '0';

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_TAXIMASK);
                stmt->setString(0, taximaskstream.str());
                stmt->setUInt32(1, lowGuid);
                trans->Append(stmt);
            }

            if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD))
            {
                // 重置公会
                if (Guild* guild = sGuildMgr->GetGuildById(characterInfo->GuildId))
                    guild->DeleteMember(trans, factionChangeInfo->Guid, false, false);

                Player::LeaveAllArenaTeams(factionChangeInfo->Guid);
            }

            if (groupId && !sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP))
            {
                if (Group* group = sGroupMgr->GetGroupByDbStoreId(groupId))
                    group->RemoveMember(factionChangeInfo->Guid);
            }

            if (!HasPermission(rbac::RBAC_PERM_TWO_SIDE_ADD_FRIEND))
            {
                // 删除好友列表
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_SOCIAL_BY_GUID);
                stmt->setUInt32(0, lowGuid);
                trans->Append(stmt);

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_SOCIAL_BY_FRIEND);
                stmt->setUInt32(0, lowGuid);
                trans->Append(stmt);
            }

            // 重置炉石绑定位置
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_PLAYER_HOMEBIND);
            stmt->setUInt32(0, lowGuid);
            trans->Append(stmt);

            stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_PLAYER_HOMEBIND);
            stmt->setUInt32(0, lowGuid);

            WorldLocation loc;
            uint16 zoneId = 0;
            if (newTeam == ALLIANCE)
            {
                loc.WorldRelocate(0, -8867.68f, 673.373f, 97.9034f, 0.0f);
                zoneId = 1519;
            }
            else
            {
                loc.WorldRelocate(1, 1633.33f, -4439.11f, 15.7588f, 0.0f);
                zoneId = 1637;
            }

            stmt->setUInt16(1, loc.GetMapId());
            stmt->setUInt16(2, zoneId);
            stmt->setFloat(3, loc.GetPositionX());
            stmt->setFloat(4, loc.GetPositionY());
            stmt->setFloat(5, loc.GetPositionZ());
            trans->Append(stmt);

            Player::SavePositionInDB(loc, zoneId, factionChangeInfo->Guid, trans);

            // 成就转换
            for (std::map<uint32, uint32>::const_iterator it = sObjectMgr->FactionChangeAchievements.begin(); it != sObjectMgr->FactionChangeAchievements.end(); ++it)
            {
                uint32 achiev_alliance = it->first;
                uint32 achiev_horde = it->second;

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_ACHIEVEMENT_BY_ACHIEVEMENT);
                stmt->setUInt16(0, uint16(newTeam == ALLIANCE ? achiev_alliance : achiev_horde));
                stmt->setUInt32(1, lowGuid);
                trans->Append(stmt);

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_ACHIEVEMENT);
                stmt->setUInt16(0, uint16(newTeam == ALLIANCE ? achiev_alliance : achiev_horde));
                stmt->setUInt16(1, uint16(newTeam == ALLIANCE ? achiev_horde : achiev_alliance));
                stmt->setUInt32(2, lowGuid);
                trans->Append(stmt);
            }

            // 物品转换
            for (std::map<uint32, uint32>::const_iterator it = sObjectMgr->FactionChangeItems.begin(); it != sObjectMgr->FactionChangeItems.end(); ++it)
            {
                uint32 item_alliance = it->first;
                uint32 item_horde = it->second;

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_INVENTORY_FACTION_CHANGE);
                stmt->setUInt32(0, (newTeam == ALLIANCE ? item_alliance : item_horde));
                stmt->setUInt32(1, (newTeam == ALLIANCE ? item_horde : item_alliance));
                stmt->setUInt32(2, lowGuid);
                trans->Append(stmt);
            }

            // 删除所有当前任务
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_QUESTSTATUS);
            stmt->setUInt32(0, lowGuid);
            trans->Append(stmt);

            // 任务转换
            for (std::map<uint32, uint32>::const_iterator it = sObjectMgr->FactionChangeQuests.begin(); it != sObjectMgr->FactionChangeQuests.end(); ++it)
            {
                uint32 quest_alliance = it->first;
                uint32 quest_horde = it->second;

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_QUESTSTATUS_REWARDED_BY_QUEST);
                stmt->setUInt32(0, lowGuid);
                stmt->setUInt32(1, (newTeam == ALLIANCE ? quest_alliance : quest_horde));
                trans->Append(stmt);

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_QUESTSTATUS_REWARDED_FACTION_CHANGE);
                stmt->setUInt32(0, (newTeam == ALLIANCE ? quest_alliance : quest_horde));
                stmt->setUInt32(1, (newTeam == ALLIANCE ? quest_horde : quest_alliance));
                stmt->setUInt32(2, lowGuid);
                trans->Append(stmt);
            }

            // 将所有已奖励任务标记为"活动"（将计入已完成任务成就）
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_QUESTSTATUS_REWARDED_ACTIVE);
            stmt->setUInt32(0, lowGuid);
            trans->Append(stmt);

            // 禁用所有旧阵营特定任务
            {
                ObjectMgr::QuestContainer const& questTemplates = sObjectMgr->GetQuestTemplates();
                for (auto const& [questId, quest] : questTemplates)
                {
                    uint32 newRaceMask = (newTeam == ALLIANCE) ? RACEMASK_ALLIANCE : RACEMASK_HORDE;
                    if (quest->GetAllowableRaces() && !(quest->GetAllowableRaces() & newRaceMask))
                    {
                        stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_QUESTSTATUS_REWARDED_ACTIVE_BY_QUEST);
                        stmt->setUInt32(0, lowGuid);
                        stmt->setUInt32(1, questId);
                        trans->Append(stmt);
                    }
                }
            }

            // 法术转换
            for (std::map<uint32, uint32>::const_iterator it = sObjectMgr->FactionChangeSpells.begin(); it != sObjectMgr->FactionChangeSpells.end(); ++it)
            {
                uint32 spell_alliance = it->first;
                uint32 spell_horde = it->second;

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_SPELL_BY_SPELL);
                stmt->setUInt32(0, (newTeam == ALLIANCE ? spell_alliance : spell_horde));
                stmt->setUInt32(1, lowGuid);
                trans->Append(stmt);

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_SPELL_FACTION_CHANGE);
                stmt->setUInt32(0, (newTeam == ALLIANCE ? spell_alliance : spell_horde));
                stmt->setUInt32(1, (newTeam == ALLIANCE ? spell_horde : spell_alliance));
                stmt->setUInt32(2, lowGuid);
                trans->Append(stmt);
            }

            // 声望转换
            for (std::map<uint32, uint32>::const_iterator it = sObjectMgr->FactionChangeReputation.begin(); it != sObjectMgr->FactionChangeReputation.end(); ++it)
            {
                uint32 reputation_alliance = it->first;
                uint32 reputation_horde = it->second;
                uint32 newReputation = (newTeam == ALLIANCE) ? reputation_alliance : reputation_horde;
                uint32 oldReputation = (newTeam == ALLIANCE) ? reputation_horde : reputation_alliance;

                // 查询数据库中旧的声望值
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_REP_BY_FACTION);
                stmt->setUInt32(0, oldReputation);
                stmt->setUInt32(1, lowGuid);

                if (PreparedQueryResult reputationResult = CharacterDatabase.Query(stmt))
                {
                    fields = reputationResult->Fetch();
                    int32 oldDBRep = fields[0].GetInt32();
                    FactionEntry const* factionEntry = sFactionStore.LookupEntry(oldReputation);

                    // 旧的基础声望
                    int32 oldBaseRep = sObjectMgr->GetBaseReputationOf(factionEntry, oldRace, playerClass);

                    // 新的基础声望
                    int32 newBaseRep = sObjectMgr->GetBaseReputationOf(sFactionStore.LookupEntry(newReputation), factionChangeInfo->Race, playerClass);

                    // 最终声望不应改变
                    int32 FinalRep = oldDBRep + oldBaseRep;
                    int32 newDBRep = FinalRep - newBaseRep;

                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_REP_BY_FACTION);
                    stmt->setUInt32(0, newReputation);
                    stmt->setUInt32(1, lowGuid);
                    trans->Append(stmt);

                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_REP_FACTION_CHANGE);
                    stmt->setUInt16(0, uint16(newReputation));
                    stmt->setInt32(1, newDBRep);
                    stmt->setUInt16(2, uint16(oldReputation));
                    stmt->setUInt32(3, lowGuid);
                    trans->Append(stmt);
                }
            }

            // 头衔转换
            if (!knownTitlesStr.empty())
            {
                std::vector<std::string_view> tokens = Trinity::Tokenize(knownTitlesStr, ' ', false);
                std::array<uint32, KNOWN_TITLES_SIZE * 2> knownTitles;

                for (uint32 index = 0; index < knownTitles.size(); ++index)
                {
                    Optional<uint32> thisMask;
                    if (index < tokens.size())
                        thisMask = Trinity::StringTo<uint32>(tokens[index]);

                    if (thisMask)
                        knownTitles[index] = *thisMask;
                    else
                    {
                        TC_LOG_WARN("entities.player", "{} has invalid title data '{}' at index {} - skipped, this may result in titles being lost",
                            GetPlayerInfo(), (index < tokens.size()) ? std::string(tokens[index]) : "<none>", index);
                        knownTitles[index] = 0;
                    }
                }

                for (std::map<uint32, uint32>::const_iterator it = sObjectMgr->FactionChangeTitles.begin(); it != sObjectMgr->FactionChangeTitles.end(); ++it)
                {
                    uint32 title_alliance = it->first;
                    uint32 title_horde = it->second;

                    CharTitlesEntry const* atitleInfo = sCharTitlesStore.AssertEntry(title_alliance);
                    CharTitlesEntry const* htitleInfo = sCharTitlesStore.AssertEntry(title_horde);
                    // new team
                    if (newTeam == ALLIANCE)
                    {
                        uint32 bitIndex = htitleInfo->MaskID;
                        uint32 index = bitIndex / 32;
                        uint32 old_flag = 1 << (bitIndex % 32);
                        uint32 new_flag = 1 << (atitleInfo->MaskID % 32);
                        if (knownTitles[index] & old_flag)
                        {
                            knownTitles[index] &= ~old_flag;
                            // 使用新头衔的索引
                            knownTitles[atitleInfo->MaskID / 32] |= new_flag;
                        }
                    }
                    else
                    {
                        uint32 bitIndex = atitleInfo->MaskID;
                        uint32 index = bitIndex / 32;
                        uint32 old_flag = 1 << (bitIndex % 32);
                        uint32 new_flag = 1 << (htitleInfo->MaskID % 32);
                        if (knownTitles[index] & old_flag)
                        {
                            knownTitles[index] &= ~old_flag;
                            // 使用新头衔的索引
                            knownTitles[htitleInfo->MaskID / 32] |= new_flag;
                        }
                    }

                    std::ostringstream ss;
                    for (uint32 mask : knownTitles)
                        ss << mask << ' ';

                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_TITLES_FACTION_CHANGE);
                    stmt->setString(0, ss.str());
                    stmt->setUInt32(1, lowGuid);
                    trans->Append(stmt);

                    // 取消当前选择的头衔
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_RES_CHAR_TITLES_FACTION_CHANGE);
                    stmt->setUInt32(0, lowGuid);
                    trans->Append(stmt);
                }
            }
        }
    }

    CharacterDatabase.CommitTransaction(trans);

    TC_LOG_DEBUG("entities.player", "{} (IP: {}) changed race from {} to {}", GetPlayerInfo(), GetRemoteAddress(), oldRace, factionChangeInfo->Race);

    SendCharFactionChange(RESPONSE_SUCCESS, factionChangeInfo.get());
}

/**
 * @brief 发送角色创建结果数据包
 *
 * 向客户端发送角色创建的结果状态。
 *
 * @param result 创建结果代码（成功或各种错误原因）
 */
void WorldSession::SendCharCreate(ResponseCodes result)
{
    WorldPacket data(SMSG_CHAR_CREATE, 1);
    data << uint8(result);
    SendPacket(&data);
}

/**
 * @brief 发送角色删除结果数据包
 *
 * 向客户端发送角色删除的结果状态。
 *
 * @param result 删除结果代码（成功或各种错误原因）
 */
void WorldSession::SendCharDelete(ResponseCodes result)
{
    WorldPacket data(SMSG_CHAR_DELETE, 1);
    data << uint8(result);
    SendPacket(&data);
}

/**
 * @brief 发送角色重命名结果数据包
 *
 * 向客户端发送角色重命名的结果状态。
 * 如果成功，还会发送新的角色名字。
 *
 * @param result 重命名结果代码
 * @param renameInfo 重命名信息结构体，包含角色GUID和新名字
 */
void WorldSession::SendCharRename(ResponseCodes result, CharacterRenameInfo const* renameInfo)
{
    WorldPacket data(SMSG_CHAR_RENAME, 1 + 8 + renameInfo->Name.size() + 1);
    data << uint8(result);
    if (result == RESPONSE_SUCCESS)
    {
        data << renameInfo->Guid;
        data << renameInfo->Name;
    }
    SendPacket(&data);
}

/**
 * @brief 发送角色自定义外观结果数据包
 *
 * 向客户端发送角色自定义外观的结果状态。
 * 如果成功，还会发送新的外观数据。
 *
 * @param result 自定义结果代码
 * @param customizeInfo 自定义外观信息结构体
 */
void WorldSession::SendCharCustomize(ResponseCodes result, CharacterCustomizeInfo const* customizeInfo)
{
    WorldPacket data(SMSG_CHAR_CUSTOMIZE, 1 + 8 + customizeInfo->Name.size() + 1 + 6);
    data << uint8(result);
    if (result == RESPONSE_SUCCESS)
    {
        data << customizeInfo->Guid;
        data << customizeInfo->Name;
        data << uint8(customizeInfo->Gender);
        data << uint8(customizeInfo->Skin);
        data << uint8(customizeInfo->Face);
        data << uint8(customizeInfo->HairStyle);
        data << uint8(customizeInfo->HairColor);
        data << uint8(customizeInfo->FacialHair);
    }
    SendPacket(&data);
}

/**
 * @brief 发送角色阵营变更结果数据包
 *
 * 向客户端发送角色阵营变更的结果状态。
 * 如果成功，还会发送新的种族和外观数据。
 *
 * @param result 变更结果代码
 * @param factionChangeInfo 阵营变更信息结构体
 */
void WorldSession::SendCharFactionChange(ResponseCodes result, CharacterFactionChangeInfo const* factionChangeInfo)
{
    WorldPacket data(SMSG_CHAR_FACTION_CHANGE, 1 + 8 + factionChangeInfo->Name.size() + 1 + 7);
    data << uint8(result);
    if (result == RESPONSE_SUCCESS)
    {
        data << factionChangeInfo->Guid;
        data << factionChangeInfo->Name;
        data << uint8(factionChangeInfo->Gender);
        data << uint8(factionChangeInfo->Skin);
        data << uint8(factionChangeInfo->Face);
        data << uint8(factionChangeInfo->HairStyle);
        data << uint8(factionChangeInfo->HairColor);
        data << uint8(factionChangeInfo->FacialHair);
        data << uint8(factionChangeInfo->Race);
    }
    SendPacket(&data);
}

/**
 * @brief 发送设置玩家变格名字结果数据包
 *
 * 向客户端发送设置变格名字的结果状态。
 *
 * @param result 设置结果代码
 * @param guid 角色GUID
 */
void WorldSession::SendSetPlayerDeclinedNamesResult(DeclinedNameResult result, ObjectGuid guid)
{
    WorldPacket data(SMSG_SET_PLAYER_DECLINED_NAMES_RESULT, 4 + 8);
    data << uint32(result);
    data << guid;
    SendPacket(&data);
}

/**
 * @brief 发送理发店结果数据包
 *
 * 向客户端发送理发店操作的结果状态。
 *
 * @param result 理发店操作结果（成功、金钱不足、不在理发椅上等）
 */
void WorldSession::SendBarberShopResult(BarberShopResult result)
{
    WorldPacket data(SMSG_BARBER_SHOP_RESULT, 4);
    data << uint32(result);
    SendPacket(&data);
}

/**
 * @brief 处理播放开场动画操作码
 *
 * 响应客户端请求播放开场动画的请求(CMSG_OPENING_CINEMATIC)。
 * 仅允许未获得任何经验值的角色播放开场动画。
 * 根据职业或种族决定播放哪个开场动画序列。
 *
 * @param packet 接收到的网络数据包（未使用）
 */
void WorldSession::HandleOpeningCinematic(WorldPackets::Misc::OpeningCinematic& /*packet*/)
{
    // 仅允许未获得任何经验值的角色使用此功能
    if (_player->GetUInt32Value(PLAYER_XP))
        return;

    if (ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(_player->GetClass()))
    {
        if (classEntry->CinematicSequenceID)
            _player->SendCinematicStart(classEntry->CinematicSequenceID);
        else if (ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(_player->GetRace()))
            _player->SendCinematicStart(raceEntry->CinematicSequenceID);
    }
}

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
 * @file Group.h
 * @brief 队伍系统核心头文件
 *
 * 本文件定义了游戏中的队伍系统核心类和数据结构，包括：
 * - Group 类：队伍和团队的主要实现
 * - Roll 类：战利品掷骰分配系统
 * - 各种枚举类型：队伍类型、成员状态、更新标志等
 *
 * 队伍系统功能概述：
 * - 小队管理：最多 5 名玩家
 * - 团队管理：最多 40 名玩家，分为 8 个子组
 * - 战利品分配：支持多种分配模式（自由拾取、队伍分配、需求优先、主分配者）
 * - 副本绑定：队伍与副本实例的关联
 * - 战场队伍：战场和战场的特殊队伍
 * - 随机副本：LFG 系统的特殊队伍
 *
 * 关键数据结构：
 * - MemberSlot：存储成员基本信息
 * - InstanceGroupBind：存储副本绑定信息
 * - Roll：管理战利品掷骰过程
 *
 * 设计模式：
 * - 引用计数：使用 GroupReference 管理玩家引用
 * - 观察者模式：队伍状态变更通知所有成员
 *
 * @see GroupMgr 管理所有队伍的全局管理器
 * @see GroupReference 玩家与队伍的引用关系
 */

#ifndef TRINITYCORE_GROUP_H
#define TRINITYCORE_GROUP_H

#include "DBCEnums.h"
#include "DatabaseEnvFwd.h"
#include "GroupRefManager.h"
#include "Loot.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "UniqueTrackablePtr.h"
#include <map>

class Battlefield;
class Battleground;
class Creature;
class InstanceSave;
class Map;
class Player;
class Unit;
class WorldObject;
class WorldPacket;
class WorldSession;

struct MapEntry;

// 小队最大成员数量
#define MAX_GROUP_SIZE      5
// 团队最大成员数量
#define MAX_RAID_SIZE       40
// 团队子组数量（8个子组，每个子组5人）
#define MAX_RAID_SUBGROUPS  MAX_RAID_SIZE / MAX_GROUP_SIZE

// 目标标记图标数量
#define TARGET_ICONS_COUNT  8

// 掷骰投票类型枚举
enum RollVote
{
    PASS              = 0,  // 放弃
    NEED              = 1,  // 需要
    GREED             = 2,  // 贪婪
    DISENCHANT        = 3,  // 分解
    NOT_EMITED_YET    = 4,  // 尚未发出
    NOT_VALID         = 5   // 无效
};

// 队员在线状态枚举
enum GroupMemberOnlineStatus
{
    MEMBER_STATUS_OFFLINE   = 0x0000,  // 离线状态
    MEMBER_STATUS_ONLINE    = 0x0001,  // 在线状态（Lua_UnitIsConnected）
    MEMBER_STATUS_PVP       = 0x0002,  // PVP状态（Lua_UnitIsPVP）
    MEMBER_STATUS_DEAD      = 0x0004,  // 死亡状态（Lua_UnitIsDead）
    MEMBER_STATUS_GHOST     = 0x0008,  // 灵魂状态（Lua_UnitIsGhost）
    MEMBER_STATUS_PVP_FFA   = 0x0010,  // 自由PVP状态（Lua_UnitIsPVPFreeForAll）
    MEMBER_STATUS_UNK3      = 0x0020,  // 未知状态3（用于Lua_GetPlayerMapPosition/Lua_GetBattlefieldFlagPosition调用）
    MEMBER_STATUS_AFK       = 0x0040,  // 离开状态（Lua_UnitIsAFK）
    MEMBER_STATUS_DND       = 0x0080   // 请勿打扰状态（Lua_UnitIsDND）
};

// 队员标志位枚举
enum GroupMemberFlags
{
    MEMBER_FLAG_ASSISTANT   = 0x01,  // 助手标志
    MEMBER_FLAG_MAINTANK    = 0x02,  // 主坦克标志
    MEMBER_FLAG_MAINASSIST  = 0x04   // 主助理标志
};

// 队员角色分配枚举
enum GroupMemberAssignment
{
    GROUP_ASSIGN_MAINTANK   = 0,  // 分配为主坦克
    GROUP_ASSIGN_MAINASSIST = 1   // 分配为主助理
};

// 队伍类型枚举
enum GroupType
{
    GROUPTYPE_NORMAL         = 0x00,                          // 普通小队
    GROUPTYPE_BG             = 0x01,                          // 战场队伍
    GROUPTYPE_RAID           = 0x02,                          // 团队
    GROUPTYPE_BGRAID         = GROUPTYPE_BG | GROUPTYPE_RAID, // 战场团队（组合标志）
    GROUPTYPE_LFG_RESTRICTED = 0x04,                          // 随机副本限制（Script_HasLFGRestrictions()）
    GROUPTYPE_LFG            = 0x08,                          // 随机副本队伍
    // 0x10, 离开/更换队伍? 在离开队伍和离开战场时看到此标志
    // GROUPTYPE_ONE_PERSON_PARTY   = 0x20, 4.x 单人队伍（Script_IsOnePersonParty()）
    // GROUPTYPE_EVERYONE_ASSISTANT = 0x40  4.x 所有人都是助理（Script_IsEveryoneAssistant()）
};

// 队伍更新标志枚举
enum GroupUpdateFlags
{
    GROUP_UPDATE_FLAG_NONE              = 0x00000000,  // 无更新
    GROUP_UPDATE_FLAG_STATUS            = 0x00000001,  // uint16, 状态标志
    GROUP_UPDATE_FLAG_CUR_HP            = 0x00000002,  // uint32, 当前生命值
    GROUP_UPDATE_FLAG_MAX_HP            = 0x00000004,  // uint32, 最大生命值
    GROUP_UPDATE_FLAG_POWER_TYPE        = 0x00000008,  // uint8, 能量类型
    GROUP_UPDATE_FLAG_CUR_POWER         = 0x00000010,  // uint16, 当前能量值
    GROUP_UPDATE_FLAG_MAX_POWER         = 0x00000020,  // uint16, 最大能量值
    GROUP_UPDATE_FLAG_LEVEL             = 0x00000040,  // uint16, 等级
    GROUP_UPDATE_FLAG_ZONE              = 0x00000080,  // uint16, 区域ID
    GROUP_UPDATE_FLAG_POSITION          = 0x00000100,  // uint16, uint16, 位置坐标
    GROUP_UPDATE_FLAG_AURAS             = 0x00000200,  // uint64掩码, 对每个设置的位包含uint32法术ID + uint8未知值
    GROUP_UPDATE_FLAG_PET_GUID          = 0x00000400,  // uint64, 宠物GUID
    GROUP_UPDATE_FLAG_PET_NAME          = 0x00000800,  // 宠物名称, NULL结尾字符串
    GROUP_UPDATE_FLAG_PET_MODEL_ID      = 0x00001000,  // uint16, 模型ID
    GROUP_UPDATE_FLAG_PET_CUR_HP        = 0x00002000,  // uint32, 宠物当前生命值
    GROUP_UPDATE_FLAG_PET_MAX_HP        = 0x00004000,  // uint32, 宠物最大生命值
    GROUP_UPDATE_FLAG_PET_POWER_TYPE    = 0x00008000,  // uint8, 宠物能量类型
    GROUP_UPDATE_FLAG_PET_CUR_POWER     = 0x00010000,  // uint16, 宠物当前能量值
    GROUP_UPDATE_FLAG_PET_MAX_POWER     = 0x00020000,  // uint16, 宠物最大能量值
    GROUP_UPDATE_FLAG_PET_AURAS         = 0x00040000,  // uint64掩码, 对每个设置的位包含uint32法术ID + uint8未知值, 宠物光环
    GROUP_UPDATE_FLAG_VEHICLE_SEAT      = 0x00080000,  // uint32, 载具座位ID（VehicleSeat.dbc中的索引）
    GROUP_UPDATE_PET                    = 0x0007FC00,  // 所有宠物相关标志
    GROUP_UPDATE_FULL                   = 0x0007FFFF   // 所有已知标志
};

// 队伍更新标志数量
#define GROUP_UPDATE_FLAGS_COUNT          20
// 每个更新标志的数据长度（字节）
// 索引: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19
static const uint8 GroupUpdateLength[GROUP_UPDATE_FLAGS_COUNT] = { 0, 2, 2, 2, 1, 2, 2, 2, 2, 4, 8, 8, 1, 2, 2, 2, 1, 2, 2, 8};

/**
 * @brief 掷骰类 - 管理战利品分配掷骰过程
 *
 * 继承自 LootValidatorRef，用于处理小队/团队中的战利品分配投票
 */
class Roll : public LootValidatorRef
{
    public:
        Roll(ObjectGuid _guid, LootItem const& li);
        ~Roll();

        // 设置关联的战利品对象
        void setLoot(Loot* pLoot);
        // 获取关联的战利品对象
        Loot* getLoot();
        // 构建目标对象链接
        void targetObjectBuildLink() override;

        ObjectGuid itemGUID;           // 物品GUID
        uint32 itemid;                 // 物品模板ID
        int32 itemRandomPropId;        // 物品随机属性ID
        uint32 itemRandomSuffix;       // 物品随机后缀
        uint8 itemCount;               // 物品数量

        typedef std::map<ObjectGuid, RollVote> PlayerVote;
        PlayerVote playerVote;         // 玩家投票映射（投票位置对应队伍中的玩家位置）

        uint8 totalPlayersRolling;     // 参与掷骰的玩家总数
        uint8 totalNeed;               // 选择"需要"的玩家数量
        uint8 totalGreed;              // 选择"贪婪"的玩家数量
        uint8 totalPass;               // 选择"放弃"的玩家数量
        uint8 itemSlot;                // 物品槽位
        uint8 rollVoteMask;            // 掷骰投票掩码
};

/**
 * @brief 副本队伍绑定结构体
 *
 * 用于记录队伍与副本的绑定关系
 */
struct InstanceGroupBind
{
    InstanceSave* save;  // 副本存档对象
    bool perm;           // 是否永久绑定（如果队长对同一副本有永久玩家副本绑定，则存在永久副本队伍绑定）
    InstanceGroupBind() : save(nullptr), perm(false) { }
};

/** 请求成员状态检查 **/
/// @todo 取消未接受邀请的人的邀请
/**
 * @brief 队伍类 - 管理小队和团队
 *
 * 这个类负责管理游戏中的队伍系统，包括小队和团队的各种功能：
 * - 队伍创建、成员管理
 * - 战利品分配
 * - 副本绑定
 * - 战场/战场队伍
 * - 队伍同步更新
 */
class TC_GAME_API Group
{
    public:
        /**
         * @brief 队员槽位结构体
         *
         * 存储单个队伍成员的基本信息
         */
        struct MemberSlot
        {
            ObjectGuid  guid;   // 成员GUID
            std::string name;   // 成员名称
            uint8       group;  // 所属子组编号（0-7）
            uint8       flags;  // 成员标志（助手、主坦、主助理等）
            uint8       roles;  // 成员角色
        };

        typedef std::list<MemberSlot> MemberSlotList;                     // 成员槽位列表类型
        typedef MemberSlotList::const_iterator member_citerator;          // 成员常量迭代器

        typedef std::unordered_map< uint32 /*mapId*/, InstanceGroupBind> BoundInstancesMap;  // 绑定副本映射
    protected:
        typedef MemberSlotList::iterator member_witerator;      // 成员可写迭代器
        typedef std::set<Player*> InvitesList;                  // 邀请列表类型

        typedef std::vector<Roll*> Rolls;                       // 掷骰列表类型

    public:
        /**
         * @brief 默认构造函数
         *
         * 初始化队伍对象的所有成员变量为默认值。
         * 创建一个空的队伍对象，需要后续调用 Create() 或 LoadGroupFromDB() 来初始化。
         *
         * 默认设置：
         * - 队伍类型：普通小队
         * - 地下城难度：普通
         * - 团队难度：10人普通
         * - 战利品分配：自由拾取
         * - 战利品品质阈值：优秀
         */
        Group();

        /**
         * @brief 析构函数
         *
         * 清理队伍对象的所有资源：
         * - 如果是战场队伍，解除与战场对象的关联
         * - 删除所有未完成的掷骰记录
         * - 解除副本存档绑定
         * - 释放子组计数器数组
         *
         * @note 析构时会通知副本存档管理器
         */
        ~Group();

        /**
         * @brief 更新队伍状态
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 定期调用以处理队伍的定时任务：
         * - 队长离线计时器：当队长离线超过一定时间后，自动选择新队长
         *
         * 调用时机：
         * - 每个 World 更新周期（通常每 50ms）
         *
         * @note 仅对普通小队和团队有效，战场队伍不触发队长自动移交
         */
        void Update(uint32 diff);

        /*********************************************************/
        /***              队伍操作方法                         ***/
        /*********************************************************/

        /**
         * @brief 创建队伍
         * @param leader 队长玩家对象指针
         * @return 创建成功返回 true，失败返回 false
         *
         * 创建一个新队伍，设置队长并初始化队伍的基本信息。
         *
         * 主要流程：
         * 1. 生成队伍 GUID 和数据库存储 ID
         * 2. 设置队长信息和队伍类型
         * 3. 将队长添加到成员列表
         * 4. 将队伍注册到 GroupMgr
         * 5. 将队伍信息保存到数据库
         *
         * 调用时机：
         * - 玩家创建新队伍时
         * - 玩家接受队伍邀请时
         *
         * @note 队长必须不在其他队伍中
         */
        bool Create(Player* leader);

        /**
         * @brief 从数据库加载队伍信息
         * @param field 数据库查询结果字段数组
         *
         * 从数据库字段中恢复队伍的基本信息。
         * 通常在服务器启动时由 GroupMgr::LoadGroups() 调用。
         *
         * 加载的信息包括：
         * - 队长 GUID 和名称
         * - 队伍类型和难度
         * - 战利品分配设置
         * - 目标标记图标
         * - 数据库存储 ID
         *
         * @note 必须在 LoadMemberFromDB() 之前调用
         * @see GroupMgr::LoadGroups()
         */
        void LoadGroupFromDB(Field* field);

        /**
         * @brief 从数据库加载成员信息
         * @param guidLow 成员的低 GUID
         * @param memberFlags 成员标志（助手、主坦等）
         * @param subgroup 子组编号（0-7）
         * @param roles 成员角色
         *
         * 向队伍添加一个成员，通常在服务器启动时从数据库恢复。
         * 不会触发成员加入通知，因为是加载阶段。
         *
         * 调用时机：
         * - 服务器启动时，在 LoadGroupFromDB() 之后调用
         *
         * @note 此方法不会验证成员是否存在
         */
        void LoadMemberFromDB(ObjectGuid::LowType guidLow, uint8 memberFlags, uint8 subgroup, uint8 roles);

        /**
         * @brief 添加邀请
         * @param player 被邀请的玩家对象指针
         * @return 添加成功返回 true，失败返回 false
         *
         * 向玩家发送队伍邀请，将其添加到被邀请者列表。
         *
         * 失败情况：
         * - 玩家已经在队伍中
         * - 玩家已经被邀请
         * - 玩家已经在被邀请者列表中
         *
         * 调用时机：
         * - 队长邀请玩家加入队伍
         * - 处理邀请请求时
         *
         * @note 邀请有超时机制，一段时间后自动过期
         */
        bool AddInvite(Player* player);

        /**
         * @brief 移除邀请
         * @param player 要移除邀请的玩家对象指针
         *
         * 从被邀请者列表中移除玩家。
         * 当玩家拒绝邀请或邀请超时时调用。
         *
         * 调用时机：
         * - 玩家拒绝队伍邀请
         * - 邀请超时
         * - 玩家加入其他队伍
         */
        void RemoveInvite(Player* player);

        /**
         * @brief 移除所有邀请
         *
         * 清空被邀请者列表。
         * 通常在队伍解散或成员已满时调用。
         *
         * 调用时机：
         * - 队伍解散
         * - 队伍成员已满
         */
        void RemoveAllInvites();

        /**
         * @brief 添加队长邀请
         * @param player 被邀请的玩家对象指针
         * @return 添加成功返回 true，失败返回 false
         *
         * 特殊的邀请方式，邀请玩家成为队长。
         * 当队伍没有成员时使用，允许被邀请者成为队长。
         *
         * 调用时机：
         * - 创建新队伍后立即邀请
         *
         * @see AddInvite()
         */
        bool AddLeaderInvite(Player* player);

        /**
         * @brief 添加成员
         * @param player 要添加的玩家对象指针
         * @return 添加成功返回 true，失败返回 false
         *
         * 将玩家添加到队伍中。
         *
         * 主要流程：
         * 1. 检查队伍是否已满
         * 2. 检查玩家是否已在队伍中
         * 3. 分配子组编号
         * 4. 创建成员槽位
         * 5. 建立玩家到队伍的引用
         * 6. 发送队伍更新通知
         * 7. 保存到数据库
         *
         * 调用时机：
         * - 玩家接受队伍邀请
         *
         * @note 添加成员后会广播更新通知
         */
        bool AddMember(Player* player);

        /**
         * @brief 移除成员
         * @param guid 要移除的成员 GUID
         * @param method 移除方式（默认、离开、被踢、断线等）
         * @param kicker 踢人的玩家 GUID（如果是被踢出）
         * @param reason 移除原因字符串（可选）
         * @return 移除成功返回 true，失败返回 false
         *
         * 从队伍中移除指定成员。
         *
         * 主要流程：
         * 1. 查找成员槽位
         * 2. 解除玩家与队伍的引用
         * 3. 从成员列表中移除
         * 4. 更新子组计数
         * 5. 如果移除的是队长，选择新队长
         * 6. 如果成员不足 2 人，解散队伍
         * 7. 广播更新通知
         * 8. 更新数据库
         *
         * 调用时机：
         * - 玩家主动离队
         * - 玩家被踢出队伍
         * - 玩家下线
         * - 玩家转服
         *
         * @note 如果队伍成员少于 2 人，会自动解散
         */
        bool RemoveMember(ObjectGuid guid, RemoveMethod const& method = GROUP_REMOVEMETHOD_DEFAULT, ObjectGuid kicker = ObjectGuid::Empty, char const* reason = nullptr);

        /**
         * @brief 更改队长
         * @param guid 新队长的 GUID
         *
         * 将队伍领导权转移给指定成员。
         *
         * 主要流程：
         * 1. 验证新队长是否是队伍成员
         * 2. 更新队长信息
         * 3. 如果是团队，设置助手权限
         * 4. 广播队长变更通知
         * 5. 更新数据库
         *
         * 调用时机：
         * - 队长主动转让队长
         * - 队长离线超时自动移交
         * - 队长离开队伍
         *
         * @note 新队长必须是当前队伍成员
         */
        void ChangeLeader(ObjectGuid guid);

        /**
         * @brief 将队长的副本实例转换为队伍绑定
         * @param player 队长玩家对象指针
         * @param group 目标队伍对象指针
         * @param switchLeader 是否切换队长标志
         *
         * 静态方法，用于在队伍创建或加入时，将队长的个人副本绑定转换为队伍绑定。
         *
         * 调用时机：
         * - 创建队伍时
         * - 玩家加入队伍时
         *
         * @note 这是副本绑定系统的重要方法
         */
        static void ConvertLeaderInstancesToGroup(Player* player, Group* group, bool switchLeader);

        /**
         * @brief 设置战利品分配方式
         * @param method 新的战利品分配方式
         *
         * 更改队伍的战利品分配规则。
         *
         * 可选方式：
         * - FREE_FOR_ALL：自由拾取
         * - ROUND_ROBIN：轮流拾取
         * - MASTER_LOOT：主分配者分配
         * - GROUP_LOOT：队伍分配
         * - NEED_BEFORE_GREED：需求优先贪婪
         *
         * 调用时机：
         * - 队长更改分配方式
         *
         * @note 只有队长可以更改
         */
        void SetLootMethod(LootMethod method);

        /**
         * @brief 设置拥有战利品分配权的玩家 GUID
         * @param guid 玩家 GUID
         *
         * 设置当前有权拾取战利品的玩家。
         * 主要用于轮流拾取模式。
         *
         * 调用时机：
         * - 战利品拾取后轮换
         */
        void SetLooterGuid(ObjectGuid guid);

        /**
         * @brief 设置主分配者 GUID
         * @param guid 主分配者的玩家 GUID
         *
         * 在主分配者分配模式下，指定谁有权分配战利品。
         *
         * 调用时机：
         * - 队长设置主分配者
         * - 主分配者离开队伍
         *
         * @note 主分配者必须是队伍成员
         */
        void SetMasterLooterGuid(ObjectGuid guid);

        /**
         * @brief 更新拥有战利品分配权的玩家 GUID
         * @param pLootedObject 被拾取的对象（尸体或宝箱）
         * @param ifneed 是否只在需要时更新
         *
         * 自动更新下一个有权拾取的玩家。
         * 主要用于轮流拾取模式。
         *
         * 调用时机：
         * - 战利品拾取后
         * - 开怪前
         */
        void UpdateLooterGuid(WorldObject* pLootedObject, bool ifneed = false);

        /**
         * @brief 设置战利品品质阈值
         * @param threshold 品质阈值
         *
         * 设置触发掷骰的最低物品品质。
         * 只有品质达到或超过此阈值的物品才会触发队伍分配或需求优先贪婪掷骰。
         *
         * 调用时机：
         * - 队长更改品质阈值
         *
         * @note 只有队长可以更改
         */
        void SetLootThreshold(ItemQualities threshold);

        /**
         * @brief 解散队伍
         * @param hideDestroy 是否隐藏解散消息（可选）
         *
         * 解散整个队伍，移除所有成员。
         *
         * 主要流程：
         * 1. 广播队伍解散消息
         * 2. 移除所有成员引用
         * 3. 清理战利品掷骰
         * 4. 解除副本绑定
         * 5. 从 GroupMgr 移除
         * 6. 从数据库删除
         *
         * 调用时机：
         * - 队长解散队伍
         * - 队伍成员不足 2 人
         * - 队长离开队伍
         */
        void Disband(bool hideDestroy = false);

        /**
         * @brief 设置随机副本角色
         * @param guid 成员 GUID
         * @param roles 角色标志（坦克、治疗、输出等）
         *
         * 设置成员在随机副本系统中的角色偏好。
         *
         * 调用时机：
         * - 成员选择角色偏好
         *
         * @see LfgRoles
         */
        void SetLfgRoles(ObjectGuid guid, uint8 roles);

        /*********************************************************/
        /***              属性访问方法                         ***/
        /*********************************************************/

        /**
         * @brief 队伍是否已满
         * @return 已满返回 true，否则返回 false
         *
         * 检查队伍是否已达到最大成员数量。
         * 小队最多 5 人，团队最多 40 人。
         */
        bool IsFull() const;

        /**
         * @brief 是否是随机副本队伍
         * @return 是返回 true，否返回 false
         *
         * 随机副本队伍有特殊规则，如不能踢人、自动退队等。
         */
        bool isLFGGroup()  const;

        /**
         * @brief 是否是团队
         * @return 是返回 true，否返回 false
         *
         * 团队支持最多 40 人，分为 8 个子组。
         */
        bool isRaidGroup() const;

        /**
         * @brief 是否是战场队伍
         * @return 是返回 true，否返回 false
         *
         * 战场队伍由战场系统管理，有特殊的创建和解散逻辑。
         */
        bool isBGGroup()   const;

        /**
         * @brief 是否是战场队伍
         * @return 是返回 true，否返回 false
         *
         * 战场队伍由战场系统管理，如冬拥湖。
         */
        bool isBFGroup()   const;

        /**
         * @brief 队伍是否已创建
         * @return 已创建返回 true，未创建返回 false
         *
         * 检查队伍对象是否已初始化。
         */
        bool IsCreated()   const;

        /**
         * @brief 获取队长 GUID
         * @return 队长的全局唯一标识符
         *
         * 返回队伍领导者的 GUID。
         */
        ObjectGuid GetLeaderGUID() const;

        /**
         * @brief 获取队伍 GUID
         * @return 队伍的全局唯一标识符
         *
         * 返回队伍对象的 GUID。
         */
        ObjectGuid GetGUID() const;

        /**
         * @brief 获取队伍低 GUID
         * @return 队伍 GUID 的低 32 位
         *
         * 返回队伍 GUID 的数值部分，用于数据库存储和查找。
         */
        ObjectGuid::LowType GetLowGUID() const;

        /**
         * @brief 获取队长名称
         * @return 队长角色名称的 C 字符串指针
         *
         * 返回缓存队长名称。
         */
        const char * GetLeaderName() const;

        /**
         * @brief 获取战利品分配方式
         * @return 当前战利品分配方式
         *
         * @see LootMethod
         */
        LootMethod GetLootMethod() const;

        /**
         * @brief 获取拥有战利品分配权的玩家 GUID
         * @return 当前有权拾取的玩家 GUID
         *
         * 主要用于轮流拾取模式。
         */
        ObjectGuid GetLooterGuid() const;

        /**
         * @brief 获取主分配者 GUID
         * @return 主分配者的玩家 GUID
         *
         * 在主分配者分配模式下使用。
         */
        ObjectGuid GetMasterLooterGuid() const;

        /**
         * @brief 获取战利品品质阈值
         * @return 当前品质阈值
         *
         * 只有品质达到或超过此阈值的物品才会触发掷骰。
         */
        ItemQualities GetLootThreshold() const;

        /**
         * @brief 获取数据库存储 ID
         * @return 数据库存储 ID
         *
         * 用于快速查找和数据库操作。
         */
        uint32 GetDbStoreId() const { return m_dbStoreId; }

        /*********************************************************/
        /***              成员操作方法                         ***/
        /*********************************************************/

        /**
         * @brief 是否是队伍成员
         * @param guid 要检查的玩家 GUID
         * @return 是成员返回 true，否则返回 false
         *
         * 检查指定玩家是否在当前队伍中。
         */
        bool IsMember(ObjectGuid guid) const;

        /**
         * @brief 是否是队长
         * @param guid 要检查的玩家 GUID
         * @return 是队长返回 true，否则返回 false
         *
         * 检查指定玩家是否是队伍领导。
         */
        bool IsLeader(ObjectGuid guid) const;

        /**
         * @brief 根据名称获取成员 GUID
         * @param name 玩家角色名称
         * @return 成员的 GUID，如果不存在返回空 GUID
         *
         * 通过角色名称查找队伍成员。
         */
        ObjectGuid GetMemberGUID(const std::string& name);

        /**
         * @brief 获取成员标志
         * @param guid 成员 GUID
         * @return 成员标志（助手、主坦、主助理等）
         *
         * @see GroupMemberFlags
         */
        uint8 GetMemberFlags(ObjectGuid guid) const;

        /**
         * @brief 是否是助手
         * @param guid 成员 GUID
         * @return 是助手返回 true，否则返回 false
         *
         * 检查成员是否有助手权限。
         * 助手可以邀请成员、设置图标等。
         */
        bool IsAssistant(ObjectGuid guid) const
        {
            return (GetMemberFlags(guid) & MEMBER_FLAG_ASSISTANT) == MEMBER_FLAG_ASSISTANT;
        }

        /**
         * @brief 根据 GUID 获取被邀请的玩家
         * @param guid 玩家 GUID
         * @return 被邀请的玩家对象指针，如果不存在返回 nullptr
         *
         * 从被邀请者列表中查找玩家。
         */
        Player* GetInvited(ObjectGuid guid) const;

        /**
         * @brief 根据名称获取被邀请的玩家
         * @param name 玩家角色名称
         * @return 被邀请的玩家对象指针，如果不存在返回 nullptr
         *
         * 从被邀请者列表中通过名称查找玩家。
         */
        Player* GetInvited(const std::string& name) const;

        /**
         * @brief 两个玩家是否在同一子组
         * @param guid1 第一个玩家的 GUID
         * @param guid2 第二个玩家的 GUID
         * @return 在同一子组返回 true，否则返回 false
         *
         * 用于判断两个成员是否在同一小队中（仅对团队有意义）。
         * 某些法术和效果只影响同一子组的成员。
         */
        bool SameSubGroup(ObjectGuid guid1, ObjectGuid guid2) const;

        /**
         * @brief 两个玩家是否在同一子组（重载版本）
         * @param guid1 第一个玩家的 GUID
         * @param slot2 第二个玩家的成员槽位指针
         * @return 在同一子组返回 true，否则返回 false
         */
        bool SameSubGroup(ObjectGuid guid1, MemberSlot const* slot2) const;

        /**
         * @brief 两个玩家是否在同一子组（重载版本）
         * @param member1 第一个玩家对象指针
         * @param member2 第二个玩家对象指针
         * @return 在同一子组返回 true，否则返回 false
         */
        bool SameSubGroup(Player const* member1, Player const* member2) const;

        /**
         * @brief 子组是否有空位
         * @param subgroup 子组编号（0-7）
         * @return 有空位返回 true，已满返回 false
         *
         * 检查指定子组是否还有空位（每个子组最多 5 人）。
         */
        bool HasFreeSlotSubGroup(uint8 subgroup) const;

        /**
         * @brief 获取成员槽位列表
         * @return 成员槽位列表的常量引用
         *
         * 返回所有成员的信息列表。
         */
        MemberSlotList const& GetMemberSlots() const { return m_memberSlots; }

        /**
         * @brief 获取第一个成员的引用（可修改版本）
         * @return 指向第一个成员的 GroupReference 指针
         *
         * 用于遍历在线成员链表。
         */
        GroupReference* GetFirstMember() { return m_memberMgr.getFirst(); }

        /**
         * @brief 获取第一个成员的引用（只读版本）
         * @return 指向第一个成员的 GroupReference 常量指针
         */
        GroupReference const* GetFirstMember() const { return m_memberMgr.getFirst(); }

        /**
         * @brief 获取成员数量
         * @return 当前队伍成员数量
         */
        uint32 GetMembersCount() const { return m_memberSlots.size(); }

        /**
         * @brief 获取被邀请者数量
         * @return 当前被邀请者数量
         */
        uint32 GetInviteeCount() const { return m_invitees.size(); }

        /**
         * @brief 获取成员所在的子组编号
         * @param guid 成员 GUID
         * @return 子组编号（0-7）
         *
         * 查找成员所属的子组。
         */
        uint8 GetMemberGroup(ObjectGuid guid) const;

        /**
         * @brief 转换为随机副本队伍
         *
         * 将普通队伍转换为随机副本队伍。
         * 随机副本队伍有特殊规则和限制。
         *
         * 调用时机：
         * - 通过随机副本系统匹配成功后
         */
        void ConvertToLFG();

        /**
         * @brief 转换为团队
         *
         * 将普通小队转换为团队。
         * 团队最多可容纳 40 名玩家。
         *
         * 调用时机：
         * - 队长将小队转换为团队
         *
         * @note 转换后无法再转回小队
         */
        void ConvertToRaid();

        /**
         * @brief 设置战场队伍
         * @param bg 战场对象指针
         *
         * 将队伍标记为战场队伍，并关联到战场对象。
         */
        void SetBattlegroundGroup(Battleground* bg);

        /**
         * @brief 设置战场队伍
         * @param bf 战场对象指针
         *
         * 将队伍标记为战场队伍，如冬拥湖。
         */
        void SetBattlefieldGroup(Battlefield* bf);

        /**
         * @brief 检查是否可以加入战场队列
         * @param bgOrTemplate 战场或战场模板对象
         * @param bgQueueTypeId 战场队列类型 ID
         * @param MinPlayerCount 最小玩家数量
         * @param MaxPlayerCount 最大玩家数量
         * @param isRated 是否是积分赛（竞技场）
         * @param arenaSlot 竞技场槽位
         * @return 加入结果（成功或错误码）
         *
         * 检查队伍是否满足加入战场队列的条件。
         *
         * 检查项目：
         * - 队伍成员数量
         * - 成员等级
         * - 成员是否已在队列中
         * - 队伍类型是否匹配
         */
        GroupJoinBattlegroundResult CanJoinBattlegroundQueue(Battleground const* bgOrTemplate, BattlegroundQueueTypeId bgQueueTypeId, uint32 MinPlayerCount, uint32 MaxPlayerCount, bool isRated, uint32 arenaSlot);

        /**
         * @brief 更改成员所在子组
         * @param guid 成员 GUID
         * @param group 新的子组编号（0-7）
         *
         * 将成员移动到指定的子组。
         *
         * 调用时机：
         * - 队长或助手调整团队组织
         */
        void ChangeMembersGroup(ObjectGuid guid, uint8 group);

        /**
         * @brief 设置目标标记图标
         * @param id 图标 ID（0-7）
         * @param whoGuid 设置图标的玩家 GUID
         * @param targetGuid 目标的 GUID（可以是怪物或玩家）
         *
         * 设置或清除目标标记图标。
         * 图标用于在团队中标记重要目标。
         *
         * 调用时机：
         * - 队长或助手标记目标
         */
        void SetTargetIcon(uint8 id, ObjectGuid whoGuid, ObjectGuid targetGuid);

        /**
         * @brief 设置队伍成员标志
         * @param guid 成员 GUID
         * @param apply 是否应用标志（true 为设置，false 为清除）
         * @param flag 要设置的标志（助手、主坦、主助理）
         *
         * 设置或清除成员的特殊标志。
         *
         * 调用时机：
         * - 队长设置主坦、主助理或助手
         *
         * @see GroupMemberFlags
         */
        void SetGroupMemberFlag(ObjectGuid guid, bool apply, GroupMemberFlags flag);

        /**
         * @brief 移除唯一的队伍成员标志
         * @param flag 要移除的标志
         *
         * 从所有成员身上移除指定的唯一标志（如主坦、主助理）。
         * 因为这些标志在队伍中只能有一个人。
         *
         * 调用时机：
         * - 设置新的主坦或主助理之前
         */
        void RemoveUniqueGroupMemberFlag(GroupMemberFlags flag);

        /*********************************************************/
        /***              副本难度相关                         ***/
        /*********************************************************/

        /**
         * @brief 获取难度
         * @param isRaid 是否是团队副本
         * @return 当前难度设置
         *
         * 根据副本类型返回相应的难度设置。
         */
        Difficulty GetDifficulty(bool isRaid) const;

        /**
         * @brief 获取地下城难度
         * @return 当前地下城难度
         *
         * 返回小队地下城的难度设置。
         */
        Difficulty GetDungeonDifficulty() const;

        /**
         * @brief 获取团队难度
         * @return 当前团队难度
         *
         * 返回团队副本的难度设置。
         */
        Difficulty GetRaidDifficulty() const;

        /**
         * @brief 设置地下城难度
         * @param difficulty 新的难度设置
         *
         * 更改地下城的难度设置。
         * 只有队长可以更改。
         *
         * 调用时机：
         * - 队长更改副本难度
         *
         * @note 必须在副本外才能更改
         */
        void SetDungeonDifficulty(Difficulty difficulty);

        /**
         * @brief 设置团队难度
         * @param difficulty 新的难度设置
         *
         * 更改团队副本的难度设置。
         * 只有队长可以更改。
         *
         * 调用时机：
         * - 队长更改团队副本难度
         *
         * @note 必须在副本外才能更改
         */
        void SetRaidDifficulty(Difficulty difficulty);

        /**
         * @brief 是否在副本中
         * @return 在副本中返回非零值，否则返回 0
         *
         * 检查队伍成员是否在副本地图中。
         */
        uint16 InInstance();

        /**
         * @brief 是否在副本战斗中
         * @param instanceId 副本实例 ID
         * @return 在战斗中返回 true，否则返回 false
         *
         * 检查队伍是否有成员在指定副本实例中战斗。
         * 用于判断是否可以重置副本。
         */
        bool InCombatToInstance(uint32 instanceId);

        /**
         * @brief 重置副本
         * @param method 重置方式
         * @param isRaid 是否是团队副本
         * @param SendMsgTo 接收消息的玩家（用于发送错误消息）
         *
         * 重置队伍绑定的副本实例。
         *
         * 调用时机：
         * - 队长请求重置副本
         *
         * @note 如果副本中有成员在战斗，无法重置
         */
        void ResetInstances(uint8 method, bool isRaid, Player* SendMsgTo);

        /*********************************************************/
        /***              消息发送方法                         ***/
        /*********************************************************/

        /**
         * @brief 发送目标标记列表
         * @param session 目标玩家的会话对象
         *
         * 向指定玩家发送当前的目标标记图标列表。
         */
        void SendTargetIconList(WorldSession* session);

        /**
         * @brief 发送更新消息
         *
         * 向所有队伍成员广播队伍更新数据包。
         * 包括成员状态、权限、子组等信息。
         *
         * 调用时机：
         * - 队伍成员变化时
         * - 队伍设置变更时
         */
        void SendUpdate();

        /**
         * @brief 发送更新消息给指定玩家
         * @param player 目标玩家对象
         * @param slot 成员槽位指针（可选，默认为 nullptr）
         *
         * 向指定玩家发送队伍更新消息。
         * 用于新成员加入时发送完整的队伍信息。
         */
        void SendUpdateToPlayer(Player const* player, MemberSlot const* slot = nullptr);

        /**
         * @brief 发送原始队伍更新消息给指定玩家
         * @param player 目标玩家对象
         *
         * 发送原始格式的队伍更新消息。
         * 用于某些特殊场景。
         */
        void SendOriginalGroupUpdateToPlayer(Player const* player) const;

        /**
         * @brief 更新超出范围的玩家
         * @param player 目标玩家对象
         *
         * 将离开视野范围的玩家的状态更新为不可见。
         */
        void UpdatePlayerOutOfRange(Player* player);

        /**
         * @brief 广播工作器（可修改）
         * @tparam Worker 工作器类型
         * @param worker 工作器对象
         */
        template<class Worker>
        void BroadcastWorker(Worker& worker)
        {
            for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
                worker(itr->GetSource());
        }

        /**
         * @brief 广播工作器（只读）
         * @tparam Worker 工作器类型
         * @param worker 工作器对象
         */
        template<class Worker>
        void BroadcastWorker(Worker const& worker) const
        {
            for (GroupReference const* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
                worker(itr->GetSource());
        }

        /**
         * @brief 广播数据包
         * @param packet 要广播的数据包指针
         * @param ignorePlayersInBGRaid 是否忽略战场队伍中的玩家
         * @param group 指定子组编号（-1 表示所有子组）
         * @param ignoredPlayer 要忽略的玩家 GUID（可选）
         *
         * 向队伍成员广播数据包。
         *
         * 参数说明：
         * - ignorePlayersInBGRaid：如果为 true，战场队伍中的玩家不会收到数据包
         * - group：指定子组编号时，只向该子组成员广播
         * - ignoredPlayer：指定的玩家不会收到数据包
         */
        void BroadcastPacket(WorldPacket const* packet, bool ignorePlayersInBGRaid, int group = -1, ObjectGuid ignoredPlayer = ObjectGuid::Empty);

        /**
         * @brief 广播就绪检查
         * @param packet 就绪检查数据包指针
         *
         * 向所有在线成员广播就绪检查请求。
         * 只有队长可以发起。
         */
        void BroadcastReadyCheck(WorldPacket const* packet);

        /**
         * @brief 离线就绪检查
         *
         * 检查所有成员的在线状态，标记离线成员。
         */
        void OfflineReadyCheck();

        /*********************************************************/
        /***                   战利品系统                      ***/
        /*********************************************************/

        /**
         * @brief 掷骰分配是否激活
         * @return 有正在进行的掷骰返回 true，否则返回 false
         *
         * 检查是否有战利品正在等待掷骰分配。
         */
        bool isRollLootActive() const;

        /**
         * @brief 发送战利品掷骰开始消息
         * @param CountDown 倒计时时间（秒）
         * @param mapid 地图 ID
         * @param r 掷骰对象引用
         *
         * 向所有有资格的成员发送掷骰开始通知。
         */
        void SendLootStartRoll(uint32 CountDown, uint32 mapid, Roll const& r);

        /**
         * @brief 发送战利品掷骰开始消息给指定玩家
         * @param countDown 倒计时时间（秒）
         * @param mapId 地图 ID
         * @param p 目标玩家对象
         * @param canNeed 是否可以选择"需求"
         * @param r 掷骰对象引用
         *
         * 向单个玩家发送掷骰通知。
         */
        void SendLootStartRollToPlayer(uint32 countDown, uint32 mapId, Player* p, bool canNeed, Roll const& r);

        /**
         * @brief 发送战利品掷骰结果
         * @param SourceGuid 战利品来源的 GUID（尸体）
         * @param TargetGuid 掷骰玩家的 GUID
         * @param RollNumber 掷骰点数（1-100）
         * @param RollType 掷骰类型（需求、贪婪、分解、放弃）
         * @param r 掷骰对象引用
         * @param autoPass 是否自动放弃（可选）
         *
         * 向所有成员广播某玩家的掷骰结果。
         */
        void SendLootRoll(ObjectGuid SourceGuid, ObjectGuid TargetGuid, uint8 RollNumber, uint8 RollType, Roll const& r, bool autoPass = false);

        /**
         * @brief 发送战利品掷骰获胜消息
         * @param SourceGuid 战利品来源的 GUID
         * @param TargetGuid 获胜玩家的 GUID
         * @param RollNumber 获胜点数
         * @param RollType 获胜类型
         * @param r 掷骰对象引用
         *
         * 向所有成员广播掷骰获胜者。
         */
        void SendLootRollWon(ObjectGuid SourceGuid, ObjectGuid TargetGuid, uint8 RollNumber, uint8 RollType, Roll const& r);

        /**
         * @brief 发送战利品全部放弃消息
         * @param roll 掷骰对象引用
         *
         * 当所有有资格的玩家都选择放弃时，通知所有人。
         */
        void SendLootAllPassed(Roll const& roll);

        /**
         * @brief 发送战利品分配者信息
         * @param creature 生物对象指针（尸体）
         * @param pLooter 当前分配者玩家对象
         *
         * 通知队伍谁有权分配战利品（主分配者模式）。
         */
        void SendLooter(Creature* creature, Player* pLooter);

        /**
         * @brief 队伍分配模式
         * @param loot 战利品对象指针
         * @param pLootedObject 被拾取的对象
         *
         * 处理队伍分配模式的战利品分配。
         * 品质达到阈值的物品会触发掷骰。
         */
        void GroupLoot(Loot* loot, WorldObject* pLootedObject);

        /**
         * @brief 需求优先贪婪分配模式
         * @param loot 战利品对象指针
         * @param pLootedObject 被拾取的对象
         *
         * 处理需求优先贪婪模式的战利品分配。
         * 只有能使用该装备的玩家才能选择"需求"。
         */
        void NeedBeforeGreed(Loot* loot, WorldObject* pLootedObject);

        /**
         * @brief 主分配者分配模式
         * @param loot 战利品对象指针
         * @param pLootedObject 被拾取的对象
         *
         * 在主分配者模式下，向主分配者发送分配界面。
         */
        void MasterLoot(Loot* loot, WorldObject* pLootedObject);

        /**
         * @brief 获取掷骰对象迭代器
         * @param Guid 物品 GUID
         * @return 掷骰对象的迭代器
         *
         * 查找指定物品的掷骰记录。
         */
        Rolls::iterator GetRoll(ObjectGuid Guid);

        /**
         * @brief 统计掷骰结果
         * @param roll 掷骰对象的迭代器
         * @param allowedMap 允许的地图（用于过滤离线玩家）
         *
         * 当所有有资格的玩家都投票后，计算获胜者并分配物品。
         */
        void CountTheRoll(Rolls::iterator roll, Map* allowedMap);

        /**
         * @brief 统计掷骰投票
         * @param playerGUID 玩家 GUID
         * @param Guid 物品 GUID
         * @param Choise 选择（需求、贪婪、分解、放弃）
         * @return 投票成功返回 true，否则返回 false
         *
         * 记录玩家的掷骰选择。
         */
        bool CountRollVote(ObjectGuid playerGUID, ObjectGuid Guid, uint8 Choise);

        /**
         * @brief 结束掷骰
         * @param loot 战利品对象指针
         * @param allowedMap 允许的地图
         *
         * 强制结束所有待处理的掷骰，通常在时间到期时调用。
         */
        void EndRoll(Loot* loot, Map* allowedMap);

        /**
         * @brief 重置最大附魔等级
         *
         * 重新计算队伍中最高的附魔技能等级。
         * 用于判断是否允许分解选项。
         */
        void ResetMaxEnchantingLevel();

        /*********************************************************/
        /***              成员链接管理                         ***/
        /*********************************************************/

        /**
         * @brief 链接成员
         * @param pRef GroupReference 对象指针
         *
         * 将成员引用添加到队伍的引用管理器中。
         * 在 GroupReference::targetObjectBuildLink() 中调用。
         */
        void LinkMember(GroupReference* pRef);

        /**
         * @brief 取消链接成员
         * @param guid 成员 GUID
         *
         * 从队伍的引用管理器中移除成员引用。
         */
        void DelinkMember(ObjectGuid guid);

        /*********************************************************/
        /***              副本绑定方法                         ***/
        /*********************************************************/

        /**
         * @brief 绑定到副本
         * @param save 副本存档对象指针
         * @param permanent 是否永久绑定
         * @param load 是否从数据库加载（可选，默认为 false）
         * @return 副本绑定结构的指针
         *
         * 将队伍绑定到指定的副本实例。
         */
        InstanceGroupBind* BindToInstance(InstanceSave* save, bool permanent, bool load = false);

        /**
         * @brief 解除副本绑定
         * @param mapid 地图 ID
         * @param difficulty 难度
         * @param unload 是否因卸载而解除绑定（可选，默认为 false）
         *
         * 移除队伍与副本实例的绑定关系。
         */
        void UnbindInstance(uint32 mapid, uint8 difficulty, bool unload = false);

        /**
         * @brief 获取副本绑定（根据玩家）
         * @param player 玩家对象指针
         * @return 副本绑定结构指针，如果不存在返回 nullptr
         *
         * 根据玩家所在的地图和难度查找副本绑定。
         */
        InstanceGroupBind* GetBoundInstance(Player* player);

        /**
         * @brief 获取副本绑定（根据地图）
         * @param aMap 地图对象指针
         * @return 副本绑定结构指针，如果不存在返回 nullptr
         *
         * 根据地图对象查找副本绑定。
         */
        InstanceGroupBind* GetBoundInstance(Map* aMap);

        /**
         * @brief 获取副本绑定（根据地图条目）
         * @param mapEntry 地图条目指针
         * @return 副本绑定结构指针，如果不存在返回 nullptr
         *
         * 根据地图条目查找副本绑定。
         */
        InstanceGroupBind* GetBoundInstance(MapEntry const* mapEntry);

        /**
         * @brief 获取副本绑定（根据难度和地图ID）
         * @param difficulty 难度
         * @param mapId 地图 ID
         * @return 副本绑定结构指针，如果不存在返回 nullptr
         *
         * 精确查找指定难度和地图的副本绑定。
         */
        InstanceGroupBind* GetBoundInstance(Difficulty difficulty, uint32 mapId);

        /**
         * @brief 获取所有绑定副本
         * @param difficulty 难度
         * @return 副本绑定映射的引用
         *
         * 返回指定难度的所有副本绑定。
         */
        BoundInstancesMap& GetBoundInstances(Difficulty difficulty);

        /*********************************************************/
        /***              队长离线管理                         ***/
        /*********************************************************/

        /**
         * @brief 启动队长离线计时器
         *
         * 当队长离线时开始计时。
         * 超过设定时间后自动移交队长权限。
         *
         * 调用时机：
         * - 队长下线时
         */
        void StartLeaderOfflineTimer();

        /**
         * @brief 停止队长离线计时器
         *
         * 取消队长离线计时。
         *
         * 调用时机：
         * - 队长重新上线
         * - 队长离开队伍
         */
        void StopLeaderOfflineTimer();

        /**
         * @brief 选择新的队长
         *
         * 自动选择新的队伍或团队队长。
         *
         * 选择规则：
         * 1. 团队中优先选择在线的助手
         * 2. 如果没有助手或不是团队，选择第一个在线成员
         *
         * 调用时机：
         * - 队长离线超时
         */
        void SelectNewPartyOrRaidLeader();

        /**
         * @brief 广播队伍更新
         *
         * 向所有成员广播队伍状态更新。
         * 特殊用途方法。
         */
        void BroadcastGroupUpdate(void);

        /**
         * @brief 获取弱引用指针
         * @return 队伍对象的弱引用指针
         *
         * 用于脚本系统安全地引用队伍对象。
         */
        Trinity::unique_weak_ptr<Group> GetWeakPtr() const { return m_scriptRef; }

    protected:
        /*********************************************************/
        /***              内部辅助方法                         ***/
        /*********************************************************/

        // 设置成员所在子组（内部实现）
        bool _setMembersGroup(ObjectGuid guid, uint8 group);
        // 如果在副本中则回城
        void _homebindIfInstance(Player* player);

        // 初始化团队子组计数器
        void _initRaidSubGroupsCounter();
        // 获取成员常量槽位迭代器
        member_citerator _getMemberCSlot(ObjectGuid Guid) const;
        // 获取成员可写槽位迭代器
        member_witerator _getMemberWSlot(ObjectGuid Guid);
        // 子组计数器增加
        void SubGroupCounterIncrease(uint8 subgroup);
        // 子组计数器减少
        void SubGroupCounterDecrease(uint8 subgroup);
        // 切换队伍成员标志
        void ToggleGroupMemberFlag(member_witerator slot, uint8 flag, bool apply);

        /*********************************************************/
        /***              成员变量                             ***/
        /*********************************************************/

        /**
         * @brief 成员槽位列表
         *
         * 存储所有队伍成员的信息，按加入顺序排列。
         * 每个元素是一个 MemberSlot 结构体，包含 GUID、名称、子组编号、标志和角色。
         *
         * @note 在团队中，最多有 40 个元素；在小队中，最多有 5 个元素
         */
        MemberSlotList      m_memberSlots;

        /**
         * @brief 成员引用管理器
         *
         * 管理所有成员的 GroupReference 对象。
         * 用于快速遍历和访问在线成员。
         *
         * @see GroupReference
         */
        GroupRefManager     m_memberMgr;

        /**
         * @brief 被邀请者列表
         *
         * 存储所有已收到邀请但尚未接受或拒绝的玩家。
         * 使用 set 存储以保证唯一性和快速查找。
         */
        InvitesList         m_invitees;

        /**
         * @brief 队长 GUID
         *
         * 队伍领导者的全局唯一标识符。
         * 队长拥有特殊权限：邀请/移除成员、更改分配方式、设置难度等。
         */
        ObjectGuid          m_leaderGuid;

        /**
         * @brief 队长名称
         *
         * 缓存队长的角色名称，避免频繁查询。
         */
        std::string         m_leaderName;

        /**
         * @brief 队伍类型
         *
         * 指定队伍的类型：普通小队、团队、战场队伍、随机副本队伍等。
         * @see GroupType
         */
        GroupType           m_groupType;

        /**
         * @brief 地下城难度
         *
         * 小队/团队在地下城中的难度设置。
         * @see Difficulty
         */
        Difficulty          m_dungeonDifficulty;

        /**
         * @brief 团队难度
         *
         * 团队在团队副本中的难度设置（10人/25人，普通/英雄）。
         * @see Difficulty
         */
        Difficulty          m_raidDifficulty;

        /**
         * @brief 战场队伍指针
         *
         * 如果此队伍是战场队伍，指向关联的战场对象。
         * 非战场队伍时为 nullptr。
         */
        Battleground*       m_bgGroup;

        /**
         * @brief 战场队伍指针
         *
         * 如果此队伍是战场队伍，指向关联的战场对象。
         * 非战场队伍时为 nullptr。
         */
        Battlefield*        m_bfGroup;

        /**
         * @brief 目标标记图标数组
         *
         * 存储 8 个目标标记图标的 GUID。
         * 图标可以是：骷髅、十字、方块、月亮、三角形、星星、菱形、圆圈。
         * 用于团队中标记重要目标。
         */
        ObjectGuid          m_targetIcons[TARGET_ICONS_COUNT];

        /**
         * @brief 战利品分配方式
         *
         * 指定队伍如何分配战利品：
         * - FREE_FOR_ALL：自由拾取
         * - ROUND_ROBIN：轮流拾取
         * - MASTER_LOOT：主分配者分配
         * - GROUP_LOOT：队伍分配（掷骰）
         * - NEED_BEFORE_GREED：需求优先贪婪
         *
         * @see LootMethod
         */
        LootMethod          m_lootMethod;

        /**
         * @brief 战利品品质阈值
         *
         * 在队伍分配和需求优先贪婪模式下，只有品质达到或超过此阈值的物品才会触发掷骰。
         * 例如：ITEM_QUALITY_UNCOMMON 表示优秀及以上品质。
         *
         * @see ItemQualities
         */
        ItemQualities       m_lootThreshold;

        /**
         * @brief 拥有战利品分配权的玩家 GUID
         *
         * 在轮流拾取模式下，记录当前有权拾取的玩家。
         * 每次拾取后会更新到下一个成员。
         */
        ObjectGuid          m_looterGuid;

        /**
         * @brief 主分配者 GUID
         *
         * 在主分配者模式下，只有此玩家可以分配战利品。
         * 队长可以设置或更改主分配者。
         */
        ObjectGuid          m_masterLooterGuid;

        /**
         * @brief 掷骰列表
         *
         * 存储所有正在进行中的战利品掷骰。
         * 每个元素对应一个待分配的物品。
         *
         * @see Roll
         */
        Rolls               RollId;

        /**
         * @brief 绑定副本映射数组
         *
         * 按难度索引，存储队伍绑定的所有副本实例。
         * 每个难度一个映射，键为地图 ID，值为 InstanceGroupBind 结构。
         *
         * @see InstanceGroupBind
         */
        BoundInstancesMap   m_boundInstances[MAX_DIFFICULTY];

        /**
         * @brief 子组计数器数组
         *
         * 记录每个子组的当前成员数量。
         * 用于在添加成员时自动分配到人数较少的子组。
         * 数组大小为 MAX_RAID_SUBGROUPS（8），每个元素对应一个子组。
         *
         * @note 仅在团队中使用，普通小队为 nullptr
         */
        uint8*              m_subGroupsCounts;

        /**
         * @brief 队伍 GUID
         *
         * 队伍的全局唯一标识符。
         * 由 GroupMgr 分配，保证全局唯一。
         */
        ObjectGuid          m_guid;

        /**
         * @brief 计数器
         *
         * 用于 SMSG_GROUP_LIST 数据包的序列号。
         * 每次发送队伍列表时递增。
         */
        uint32              m_counter;

        /**
         * @brief 最大附魔等级
         *
         * 队伍中成员的最高附魔技能等级。
         * 用于判断是否允许分解掷骰选项。
         */
        uint32              m_maxEnchantingLevel;

        /**
         * @brief 数据库存储 ID
         *
         * 队伍在数据库中的存储标识符。
         * 用于快速查找和数据库操作。
         * 队伍解散后，此 ID 可以被其他新队伍重用。
         */
        uint32              m_dbStoreId;

        /**
         * @brief 队长是否离线
         *
         * 标记队长当前是否处于离线状态。
         * 用于触发队长自动移交机制。
         */
        bool                m_isLeaderOffline;

        /**
         * @brief 队长离线计时器
         *
         * 追踪队长离线的持续时间。
         * 当队长离线超过一定时间后，自动选择新队长。
         *
         * @see Update()
         */
        TimeTracker         m_leaderOfflineTimer;

        /**
         * @brief 空操作删除器结构体
         *
         * 用于 unique_trackable_ptr 的删除器。
         * 不执行实际删除操作，因为 Group 对象由其他机制管理。
         */
        struct NoopGroupDeleter { void operator()(Group*) const { /*空操作 - 非托管*/ } };

        /**
         * @brief 脚本引用指针
         *
         * 提供对 Group 对象的弱引用能力。
         * 用于脚本系统安全地引用队伍对象。
         */
        Trinity::unique_trackable_ptr<Group> m_scriptRef;
};
#endif

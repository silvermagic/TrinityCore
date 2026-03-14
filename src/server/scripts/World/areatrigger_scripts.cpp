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
 * @file    areatrigger_scripts.cpp
 * @brief   区域触发器脚本模块
 *
 * 本文件实现了各种区域触发器（AreaTrigger）的脚本逻辑。
 * 区域触发器是游戏世界中玩家进入特定区域时自动触发的机制，
 * 用于实现传送门、任务触发、NPC召唤等功能。
 *
 * 主要功能包括:
 * - 副本门控制（盘牙水库瀑布）
 * - 任务相关传送（军团传送器、索拉查传送门）
 * - 任务NPC召唤（拉科维尔的配偶、潜伏的鲨鱼）
 * - 节日事件触发（美酒节欢迎语）
 * - 城市入口效果（52区神经删除器）
 *
 * 性能注意事项:
 * - 区域触发器会在玩家进入时频繁调用，需要保持OnTrigger函数简洁
 * - 使用冷却时间机制避免重复触发
 * - 避免在触发时进行复杂的数据库查询
 */

#include "ScriptMgr.h"
#include "DBCStructure.h"
#include "GameObject.h"
#include "GameTime.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

/*######
## at_coilfang_waterfall - 盘牙水库瀑布区域触发器
######*/

/**
 * @brief 盘牙水库相关游戏对象ID
 */
enum CoilfangGOs
{
    GO_COILFANG_WATERFALL   = 184212    ///< 盘牙水库瀑布门游戏对象ID
};

/**
 * @class AreaTrigger_at_coilfang_waterfall
 * @brief 盘牙水库瀑布门区域触发器脚本
 *
 * 当玩家进入盘牙水库副本入口附近的区域触发器时，
 * 自动打开瀑布门游戏对象。该触发器用于副本入口的便捷访问。
 */
class AreaTrigger_at_coilfang_waterfall : public AreaTriggerScript
{
    public:
        /**
         * @brief 构造函数
         */
        AreaTrigger_at_coilfang_waterfall() : AreaTriggerScript("at_coilfang_waterfall") { }

        /**
         * @brief 区域触发器触发回调
         * @param player 触发区域的玩家指针
         * @param trigger 区域触发器数据条目（未使用）
         * @return 返回false表示不阻止后续处理
         *
         * 当玩家进入触发区域时调用：
         * 1. 搜索35码范围内最近的瀑布门游戏对象
         * 2. 如果门处于可使用状态，则激活开门动画
         *
         * 性能注意：使用GetClosestGameObjectWithEntry进行范围搜索，
         * 应避免过大的搜索半径
         */
        bool OnTrigger(Player* player, AreaTriggerEntry const* /*trigger*/) override
        {
            // 在玩家35码范围内查找瀑布门
            if (GameObject* go = GetClosestGameObjectWithEntry(player, GO_COILFANG_WATERFALL, 35.0f))
                // 检查门是否处于可使用状态（未被激活）
                if (go->getLootState() == GO_READY)
                    go->UseDoorOrButton();  // 激活开门动画

            return false;
        }
};

/*#####
## at_legion_teleporter - 军团传送器区域触发器
#####*/

/**
 * @brief 军团传送器相关法术和任务ID
 */
enum LegionTeleporter
{
    SPELL_TELE_A_TO         = 37387,    ///< 联盟传送法术ID
    QUEST_GAINING_ACCESS_A  = 10589,    ///< 联盟任务：获得访问权限

    SPELL_TELE_H_TO         = 37389,    ///< 部落传送法术ID
    QUEST_GAINING_ACCESS_H  = 10604     ///< 部落任务：获得访问权限
};

/**
 * @class AreaTrigger_at_legion_teleporter
 * @brief 军团传送器区域触发器脚本
 *
 * 为完成特定任务的玩家提供传送服务。
 * 联盟和部落玩家需要完成各自的任务才能使用传送器。
 *
 * 使用条件：
 * - 玩家必须存活
 * - 玩家不能处于战斗状态
 * - 必须完成对应阵营的任务
 */
class AreaTrigger_at_legion_teleporter : public AreaTriggerScript
{
    public:
        /**
         * @brief 构造函数
         */
        AreaTrigger_at_legion_teleporter() : AreaTriggerScript("at_legion_teleporter") { }

        /**
         * @brief 区域触发器触发回调
         * @param player 触发区域的玩家指针
         * @param trigger 区域触发器数据条目（未使用）
         * @return 返回true表示成功触发传送，false表示条件不满足
         *
         * 触发逻辑：
         * 1. 检查玩家是否存活且不在战斗中
         * 2. 根据玩家阵营检查对应任务完成状态
         * 3. 施放传送法术将玩家传送到目标位置
         *
         * 注意：只有完成任务的玩家才能使用传送器
         */
        bool OnTrigger(Player* player, AreaTriggerEntry const* /*trigger*/) override
        {
            // 验证玩家状态：必须存活且不在战斗中
            if (player->IsAlive() && !player->IsInCombat())
            {
                // 联盟玩家处理
                if (player->GetTeam() == ALLIANCE && player->GetQuestRewardStatus(QUEST_GAINING_ACCESS_A))
                {
                    player->CastSpell(player, SPELL_TELE_A_TO, false);
                    return true;
                }

                // 部落玩家处理
                if (player->GetTeam() == HORDE && player->GetQuestRewardStatus(QUEST_GAINING_ACCESS_H))
                {
                    player->CastSpell(player, SPELL_TELE_H_TO, false);
                    return true;
                }

                return false;
            }
            return false;
        }
};

/*######
## at_scent_larkorwi - 拉科维尔的气味任务区域触发器
######*/

/**
 * @brief 拉科维尔的气味任务相关ID
 */
enum ScentLarkorwi
{
    QUEST_SCENT_OF_LARKORWI                     = 4291,    ///< 任务：拉科维尔的气味
    NPC_LARKORWI_MATE                           = 9683     ///< NPC：拉科维尔的配偶
};

/**
 * @class AreaTrigger_at_scent_larkorwi
 * @brief 拉科维尔的气味任务区域触发器脚本
 *
 * 当正在进行"拉科维尔的气味"任务的玩家进入特定区域时，
 * 召唤拉科维尔的配偶NPC进行战斗。
 *
 * 任务机制：
 * - 玩家需要在任务区域触发NPC出现
 * - NPC在脱离战斗后会自动消失
 */
class AreaTrigger_at_scent_larkorwi : public AreaTriggerScript
{
    public:
        /**
         * @brief 构造函数
         */
        AreaTrigger_at_scent_larkorwi() : AreaTriggerScript("at_scent_larkorwi") { }

        /**
         * @brief 区域触发器触发回调
         * @param player 触发区域的玩家指针
         * @param trigger 区域触发器数据条目（未使用）
         * @return 返回false表示不阻止后续处理
         *
         * 触发条件：
         * - 玩家必须存活
         * - 玩家正在进行"拉科维尔的气味"任务
         * - 15码内不存在拉科维尔的配偶（防止重复召唤）
         *
         * 召唤逻辑：
         * - 在玩家位置偏移5单位处召唤NPC
         * - NPC在脱离战斗100秒后自动消失
         */
        bool OnTrigger(Player* player, AreaTriggerEntry const* /*trigger*/) override
        {
            // 检查玩家存活状态和任务进度
            if (!player->isDead() && player->GetQuestStatus(QUEST_SCENT_OF_LARKORWI) == QUEST_STATUS_INCOMPLETE)
            {
                // 检查附近是否已存在该NPC，避免重复召唤
                if (!player->FindNearestCreature(NPC_LARKORWI_MATE, 15))
                    // 召唤NPC：位置在玩家前方5单位，面向方向3.3弧度，脱战100秒后消失
                    player->SummonCreature(NPC_LARKORWI_MATE, player->GetPositionX()+5, player->GetPositionY(), player->GetPositionZ(), 3.3f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 100s);
            }

            return false;
        }
};

/*######
## at_sholazar_waygate - 索拉查传送门区域触发器
######*/

/**
 * @brief 索拉查传送门相关法术、区域触发器和任务ID
 */
enum Waygate
{
    SPELL_SHOLAZAR_TO_UNGORO_TELEPORT           = 52056,    ///< 从索拉查传送到安戈洛的法术
    SPELL_UNGORO_TO_SHOLAZAR_TELEPORT           = 52057,    ///< 从安戈洛传送到索拉查的法术

    AT_SHOLAZAR                                 = 5046,     ///< 索拉查盆地传送门区域触发器ID
    AT_UNGORO                                   = 5047,     ///< 安戈洛环形山传送门区域触发器ID

    QUEST_THE_MAKERS_OVERLOOK                   = 12613,    ///< 任务：造物者的俯瞰台
    QUEST_THE_MAKERS_PERCH                      = 12559,    ///< 任务：造物者的栖地
    QUEST_MEETING_A_GREAT_ONE                   = 13956,    ///< 任务：会见伟大者
};

/**
 * @class AreaTrigger_at_sholazar_waygate
 * @brief 索拉查传送门区域触发器脚本
 *
 * 实现索拉查盆地与安戈洛环形山之间的双向传送门功能。
 * 玩家需要完成特定任务链才能使用传送门。
 *
 * 使用条件（满足其一即可）：
 * 1. 正在进行"会见伟大者"任务
 * 2. 已完成"造物者的俯瞰台"和"造物者的栖地"两个任务
 */
class AreaTrigger_at_sholazar_waygate : public AreaTriggerScript
{
    public:
        /**
         * @brief 构造函数
         */
        AreaTrigger_at_sholazar_waygate() : AreaTriggerScript("at_sholazar_waygate") { }

        /**
         * @brief 区域触发器触发回调
         * @param player 触发区域的玩家指针
         * @param trigger 区域触发器数据条目，用于确定传送方向
         * @return 返回false表示不阻止后续处理
         *
         * 传送逻辑：
         * 1. 检查玩家是否存活
         * 2. 验证玩家是否满足任务要求
         * 3. 根据触发的区域ID选择对应的传送法术
         */
        bool OnTrigger(Player* player, AreaTriggerEntry const* trigger) override
        {
            // 验证玩家存活状态和任务完成情况
            if (!player->isDead() && (player->GetQuestStatus(QUEST_MEETING_A_GREAT_ONE) != QUEST_STATUS_NONE ||
                (player->GetQuestStatus(QUEST_THE_MAKERS_OVERLOOK) == QUEST_STATUS_REWARDED && player->GetQuestStatus(QUEST_THE_MAKERS_PERCH) == QUEST_STATUS_REWARDED)))
            {
                // 根据区域触发器ID决定传送方向
                switch (trigger->ID)
                {
                    case AT_SHOLAZAR:
                        // 从索拉查传送到安戈洛
                        player->CastSpell(player, SPELL_SHOLAZAR_TO_UNGORO_TELEPORT, true);
                        break;

                    case AT_UNGORO:
                        // 从安戈洛传送到索拉查
                        player->CastSpell(player, SPELL_UNGORO_TO_SHOLAZAR_TELEPORT, true);
                        break;
                }
            }

            return false;
        }
};

/*######
## at_nats_landing - 纳特的着陆点区域触发器
######*/

/**
 * @brief 纳特的着陆点任务相关ID
 */
enum NatsLanding
{
    QUEST_NATS_BARGAIN = 11209,     ///< 任务：纳特的交易
    SPELL_FISH_PASTE   = 42644,     ///< 法术：鱼膏（吸引鲨鱼的物品效果）
    NPC_LURKING_SHARK  = 23928      ///< NPC：潜伏的鲨鱼
};

/**
 * @class AreaTrigger_at_nats_landing
 * @brief 纳特的着陆点区域触发器脚本
 *
 * 当正在进行"纳特的交易"任务且拥有鱼膏效果的玩家进入特定水域时，
 * 召唤潜伏的鲨鱼NPC进行攻击。
 *
 * 任务机制：
 * - 玩家需要使用鱼膏物品获得法术效果
 * - 进入水域区域触发鲨鱼出现
 * - 击败鲨鱼完成任务目标
 */
class AreaTrigger_at_nats_landing : public AreaTriggerScript
{
    public:
        /**
         * @brief 构造函数
         */
        AreaTrigger_at_nats_landing() : AreaTriggerScript("at_nats_landing") { }

        /**
         * @brief 区域触发器触发回调
         * @param player 触发区域的玩家指针
         * @param trigger 区域触发器数据条目（未使用）
         * @return 返回true表示已处理，false表示条件不满足
         *
         * 触发条件：
         * - 玩家必须存活
         * - 玩家必须拥有鱼膏法术效果
         * - 正在进行"纳特的交易"任务
         * - 20码内不存在潜伏的鲨鱼
         *
         * 召唤逻辑：
         * - 在指定坐标召唤鲨鱼（固定位置）
         * - 鲨鱼立即攻击玩家
         * - 脱战100秒后自动消失
         */
        bool OnTrigger(Player* player, AreaTriggerEntry const* /*trigger*/) override
        {
            // 验证玩家存活状态和鱼膏效果
            if (!player->IsAlive() || !player->HasAura(SPELL_FISH_PASTE))
                return false;

            // 检查任务进度
            if (player->GetQuestStatus(QUEST_NATS_BARGAIN) == QUEST_STATUS_INCOMPLETE)
            {
                // 检查附近是否已存在鲨鱼，避免重复召唤
                if (!player->FindNearestCreature(NPC_LURKING_SHARK, 20.0f))
                {
                    // 召唤鲨鱼：固定坐标位置，脱战100秒后消失
                    if (Creature* shark = player->SummonCreature(NPC_LURKING_SHARK, -4246.243f, -3922.356f, -7.488f, 5.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 100s))
                        shark->AI()->AttackStart(player);  // 立即攻击玩家

                    return false;
                }
            }
            return true;
        }
};

/*######
## at_sentry_point - 哨兵点区域触发器
######*/

/**
 * @brief 哨兵点相关法术、任务和NPC ID
 */
enum SentryPoint
{
    SPELL_TELEPORT_VISUAL = 799,        ///< 传送视觉效果法术（待确认正确ID）
    QUEST_MISSING_DIPLO_PT14 = 1265,    ///< 任务：缺失的使节第14部分
    NPC_TERVOSH = 4967                  ///< NPC：特沃什
};

/**
 * @class AreaTrigger_at_sentry_point
 * @brief 哨兵点区域触发器脚本
 *
 * 当正在进行"缺失的使节"任务链的玩家进入哨兵点时，
 * 召唤特沃什NPC并播放传送视觉效果。
 *
 * 任务机制：
 * - 任务处于进行中状态时触发NPC出现
 * - NPC显示传送效果后在1分钟后消失
 */
class AreaTrigger_at_sentry_point : public AreaTriggerScript
{
public:
    /**
     * @brief 构造函数
     */
    AreaTrigger_at_sentry_point() : AreaTriggerScript("at_sentry_point") { }

    /**
     * @brief 区域触发器触发回调
     * @param player 触发区域的玩家指针
     * @param trigger 区域触发器数据条目（未使用）
     * @return 返回true表示已处理，false表示条件不满足
     *
     * 触发条件：
     * - 玩家必须存活
     * - 任务必须处于进行中状态（非未开始或已完成）
     * - 100码内不存在特沃什NPC
     *
     * 召唤逻辑：
     * - 在固定坐标召唤特沃什
     * - 播放传送视觉效果
     * - 1分钟后自动消失
     */
    bool OnTrigger(Player* player, AreaTriggerEntry const* /*trigger*/)
    {
        QuestStatus quest_status = player->GetQuestStatus(QUEST_MISSING_DIPLO_PT14);
        // 验证玩家存活状态和任务进度
        if (!player->IsAlive() || quest_status == QUEST_STATUS_NONE || quest_status == QUEST_STATUS_REWARDED)
            return false;

        // 检查附近是否已存在特沃什，避免重复召唤
        if (!player->FindNearestCreature(NPC_TERVOSH, 100.0f))
        {
            // 召唤特沃什：固定坐标，1分钟后消失
            if (Creature* tervosh = player->SummonCreature(NPC_TERVOSH, -3476.51f, -4105.94f, 17.1f, 5.3816f, TEMPSUMMON_TIMED_DESPAWN, 1min))
                tervosh->CastSpell(tervosh, SPELL_TELEPORT_VISUAL, true);  // 播放传送视觉效果
        }

        return true;
    }
};

/*######
## at_brewfest - 美酒节区域触发器
######*/

/**
 * @brief 美酒节相关NPC、区域触发器和配置
 */
enum Brewfest
{
    NPC_TAPPER_SWINDLEKEG       = 24711,    ///< NPC：塔珀·斯温德尔凯格（部落，杜隆塔尔）
    NPC_IPFELKOFER_IRONKEG      = 24710,    ///< NPC：伊普费尔科弗·铁桶（联盟，丹莫罗）

    AT_BREWFEST_DUROTAR         = 4829,     ///< 区域触发器：杜隆塔尔美酒节入口
    AT_BREWFEST_DUN_MOROGH      = 4820,     ///< 区域触发器：丹莫罗美酒节入口

    SAY_WELCOME                 = 4,        ///< 对话ID：欢迎语

    AREATRIGGER_TALK_COOLDOWN   = 5,        ///< 触发冷却时间（秒）
};

/**
 * @class AreaTrigger_at_brewfest
 * @brief 美酒节区域触发器脚本
 *
 * 当玩家进入美酒节活动入口区域时，触发NPC欢迎对话。
 * 部落和联盟各自拥有不同的入口和NPC。
 *
 * 使用冷却机制避免频繁触发：
 * - 每个区域触发器独立计算冷却时间
 * - 5秒内重复进入不会重复触发对话
 *
 * 性能注意：
 * - 使用map存储每个触发器的最后触发时间
 * - 避免短时间内重复处理
 */
class AreaTrigger_at_brewfest : public AreaTriggerScript
{
    public:
        /**
         * @brief 构造函数，初始化冷却时间映射表
         */
        AreaTrigger_at_brewfest() : AreaTriggerScript("at_brewfest")
        {
            // 初始化各区域触发器的冷却时间为0
            _triggerTimes[AT_BREWFEST_DUROTAR] = _triggerTimes[AT_BREWFEST_DUN_MOROGH] = 0;
        }

        /**
         * @brief 区域触发器触发回调
         * @param player 触发区域的玩家指针
         * @param trigger 区域触发器数据条目
         * @return 返回false表示不阻止后续处理
         *
         * 触发逻辑：
         * 1. 检查冷却时间，避免频繁触发
         * 2. 根据区域ID找到对应的NPC
         * 3. 让NPC对玩家说欢迎语
         * 4. 更新冷却时间
         */
        bool OnTrigger(Player* player, AreaTriggerEntry const* trigger) override
        {
            uint32 triggerId = trigger->ID;
            // 检查冷却时间：如果距离上次触发不足5秒，跳过本次触发
            if (GameTime::GetGameTime() - _triggerTimes[triggerId] < AREATRIGGER_TALK_COOLDOWN)
                return false;

            // 根据区域触发器ID执行对应逻辑
            switch (triggerId)
            {
                case AT_BREWFEST_DUROTAR:
                    // 杜隆塔尔（部落）：塔珀·斯温德尔凯格说欢迎语
                    if (Creature* tapper = player->FindNearestCreature(NPC_TAPPER_SWINDLEKEG, 20.0f))
                        tapper->AI()->Talk(SAY_WELCOME, player);
                    break;
                case AT_BREWFEST_DUN_MOROGH:
                    // 丹莫罗（联盟）：伊普费尔科弗·铁桶说欢迎语
                    if (Creature* ipfelkofer = player->FindNearestCreature(NPC_IPFELKOFER_IRONKEG, 20.0f))
                        ipfelkofer->AI()->Talk(SAY_WELCOME, player);
                    break;
                default:
                    break;
            }

            // 更新该区域触发器的最后触发时间
            _triggerTimes[triggerId] = GameTime::GetGameTime();
            return false;
        }

    private:
        std::map<uint32, time_t> _triggerTimes;  ///< 各区域触发器的最后触发时间映射表
};

/*######
## at_area_52_entrance - 52区入口区域触发器
######*/

/**
 * @brief 52区入口相关法术、NPC和区域触发器ID
 */
enum Area52Entrance
{
    SPELL_A52_NEURALYZER  = 34400,  ///< 法术：52区神经删除器（让玩家忘记所见）
    NPC_SPOTLIGHT         = 19913,  ///< NPC：聚光灯
    SUMMON_COOLDOWN       = 5,      ///< 召唤冷却时间（秒）

    AT_AREA_52_SOUTH      = 4472,   ///< 区域触发器：52区南门
    AT_AREA_52_NORTH      = 4466,   ///< 区域触发器：52区北门
    AT_AREA_52_WEST       = 4471,   ///< 区域触发器：52区西门
    AT_AREA_52_EAST       = 4422,   ///< 区域触发器：52区东门
};

/**
 * @class AreaTrigger_at_area_52_entrance
 * @brief 52区入口区域触发器脚本
 *
 * 当玩家进入52区时，召唤聚光灯NPC并对玩家施放神经删除器法术，
 * 创造一个幽默的"记忆清除"效果（致敬电影《黑衣人》）。
 *
 * 52区有东南西北四个入口，每个入口都会触发相同的效果，
 * 但聚光灯会在不同的固定位置召唤。
 *
 * 使用冷却机制避免频繁触发。
 */
class AreaTrigger_at_area_52_entrance : public AreaTriggerScript
{
    public:
        /**
         * @brief 构造函数，初始化各入口的冷却时间
         */
        AreaTrigger_at_area_52_entrance() : AreaTriggerScript("at_area_52_entrance")
        {
            // 初始化所有入口的触发时间为0
            _triggerTimes[AT_AREA_52_SOUTH] = _triggerTimes[AT_AREA_52_NORTH] = _triggerTimes[AT_AREA_52_WEST] = _triggerTimes[AT_AREA_52_EAST] = 0;
        }

        /**
         * @brief 区域触发器触发回调
         * @param player 触发区域的玩家指针
         * @param trigger 区域触发器数据条目
         * @return 返回false表示不阻止后续处理
         *
         * 触发逻辑：
         * 1. 检查玩家存活状态
         * 2. 检查冷却时间
         * 3. 根据入口ID确定聚光灯召唤位置
         * 4. 召唤聚光灯NPC并施放神经删除器法术
         *
         * 性能注意：
         * - 每个入口独立计算冷却时间
         * - 避免在冷却期内重复召唤
         */
        bool OnTrigger(Player* player, AreaTriggerEntry const* trigger) override
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;  // 聚光灯召唤坐标

            // 验证玩家存活状态
            if (!player->IsAlive())
                return false;

            uint32 triggerId = trigger->ID;
            // 检查冷却时间
            if (GameTime::GetGameTime() - _triggerTimes[trigger->ID] < SUMMON_COOLDOWN)
                return false;

            // 根据入口ID确定聚光灯召唤位置
            switch (triggerId)
            {
                case AT_AREA_52_EAST:   // 东门
                    x = 3044.176f;
                    y = 3610.692f;
                    z = 143.61f;
                    break;
                case AT_AREA_52_NORTH:  // 北门
                    x = 3114.87f;
                    y = 3687.619f;
                    z = 143.62f;
                    break;
                case AT_AREA_52_WEST:   // 西门
                    x = 3017.79f;
                    y = 3746.806f;
                    z = 144.27f;
                    break;
                case AT_AREA_52_SOUTH:  // 南门
                    x = 2950.63f;
                    y = 3719.905f;
                    z = 143.33f;
                    break;
            }

            // 召唤聚光灯NPC，5秒后消失
            player->SummonCreature(NPC_SPOTLIGHT, x, y, z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, 5s);
            // 对玩家施放神经删除器法术
            player->AddAura(SPELL_A52_NEURALYZER, player);
            // 更新冷却时间
            _triggerTimes[trigger->ID] = GameTime::GetGameTime();
            return false;
        }

    private:
        std::map<uint32, time_t> _triggerTimes;  ///< 各入口的最后触发时间映射表
};

/*######
 ## at_frostgrips_hollow - 弗罗斯特格里斯空谷区域触发器
 ######*/

/**
 * @brief 弗罗斯特格里斯空谷任务相关ID
 */
enum FrostgripsHollow
{
    QUEST_THE_LONESOME_WATCHER      = 12877,   ///< 任务：孤独的守望者

    NPC_STORMFORGED_MONITOR         = 29862,   ///< NPC：风暴铸造监视者
    NPC_STORMFORGED_ERADICTOR       = 29861,   ///< NPC：风暴铸造根除者

    TYPE_WAYPOINT                   = 0,       ///< 路径点类型
    DATA_START                      = 0        ///< 数据起点
};

/// 风暴铸造监视者初始位置
Position const stormforgedMonitorPosition = {6963.95f, 45.65f, 818.71f, 4.948f};
/// 风暴铸造根除者初始位置
Position const stormforgedEradictorPosition = {6983.18f, 7.15f, 806.33f, 2.228f};

/**
 * @class AreaTrigger_at_frostgrips_hollow
 * @brief 弗罗斯特格里斯空谷区域触发器脚本
 *
 * 当正在进行"孤独的守望者"任务的玩家进入特定区域时，
 * 召唤两个风暴铸造NPC并让它们按照预设路径点移动。
 *
 * 任务机制：
 * - 玩家需要击败这两个风暴铸造NPC
 * - NPC会沿路径点移动到目标位置
 * - 避免重复召唤已存在的NPC
 *
 * 技术细节：
 * - 使用ObjectGuid跟踪已召唤的NPC
 * - 监视者使用UNIT_STATE_IGNORE_PATHFINDING状态
 *   防止NPC寻找替代路径
 */
class AreaTrigger_at_frostgrips_hollow : public AreaTriggerScript
{
public:
    /**
     * @brief 构造函数，初始化NPC GUID
     */
    AreaTrigger_at_frostgrips_hollow() : AreaTriggerScript("at_frostgrips_hollow")
    {
        stormforgedMonitorGUID.Clear();
        stormforgedEradictorGUID.Clear();
    }

    /**
     * @brief 区域触发器触发回调
     * @param player 触发区域的玩家指针
     * @param trigger 区域触发器数据条目（未使用）
     * @return 返回true表示已处理，false表示条件不满足
     *
     * 触发条件：
     * - 玩家正在进行"孤独的守望者"任务
     * - 两个NPC都未被召唤
     *
     * 召唤逻辑：
     * 1. 检查任务状态和NPC存在性
     * 2. 在预设位置召唤两个NPC
     * 3. 设置NPC沿路径点移动
     * 4. 存储NPC的GUID用于后续检查
     *
     * 性能注意：
     * - 使用ObjectAccessor快速查找NPC
     * - 避免重复召唤造成资源浪费
     */
    bool OnTrigger(Player* player, AreaTriggerEntry const* /* trigger */) override
    {
        // 验证任务进度
        if (player->GetQuestStatus(QUEST_THE_LONESOME_WATCHER) != QUEST_STATUS_INCOMPLETE)
            return false;

        // 检查风暴铸造监视者是否已存在
        Creature* stormforgedMonitor = ObjectAccessor::GetCreature(*player, stormforgedMonitorGUID);
        if (stormforgedMonitor)
            return false;

        // 检查风暴铸造根除者是否已存在
        Creature* stormforgedEradictor = ObjectAccessor::GetCreature(*player, stormforgedEradictorGUID);
        if (stormforgedEradictor)
            return false;

        // 召唤风暴铸造监视者
        stormforgedMonitor = player->SummonCreature(NPC_STORMFORGED_MONITOR, stormforgedMonitorPosition, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 1min);
        if (stormforgedMonitor)
        {
            stormforgedMonitorGUID = stormforgedMonitor->GetGUID();
            stormforgedMonitor->SetWalk(false);  // 使用跑步速度移动
            // 设置忽略寻路状态，防止NPC寻找替代路径
            /// 该NPC需要这个状态，否则会寻找替代路径到达最后一个路径点
            stormforgedMonitor->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);
            // 开始沿预设路径点移动
            stormforgedMonitor->GetMotionMaster()->MovePath(NPC_STORMFORGED_MONITOR * 100, false);
        }

        // 召唤风暴铸造根除者
        stormforgedEradictor = player->SummonCreature(NPC_STORMFORGED_ERADICTOR, stormforgedEradictorPosition, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 1min);
        if (stormforgedEradictor)
        {
            stormforgedEradictorGUID = stormforgedEradictor->GetGUID();
            // 开始沿预设路径点移动
            stormforgedEradictor->GetMotionMaster()->MovePath(NPC_STORMFORGED_ERADICTOR * 100, false);
        }

        return true;
    }

private:
    ObjectGuid stormforgedMonitorGUID;   ///< 风暴铸造监视者GUID，用于追踪已召唤的NPC
    ObjectGuid stormforgedEradictorGUID; ///< 风暴铸造根除者GUID，用于追踪已召唤的NPC
};

/**
 * @brief 注册所有区域触发器脚本
 *
 * 该函数由脚本系统在启动时调用，用于注册本文件中定义的所有区域触发器脚本。
 * 每个脚本实例化后会自动注册到脚本管理器中。
 */
void AddSC_areatrigger_scripts()
{
    new AreaTrigger_at_coilfang_waterfall();   // 盘牙水库瀑布门
    new AreaTrigger_at_legion_teleporter();    // 军团传送器
    new AreaTrigger_at_scent_larkorwi();       // 拉科维尔的气味
    new AreaTrigger_at_sholazar_waygate();     // 索拉查传送门
    new AreaTrigger_at_nats_landing();         // 纳特的着陆点
    new AreaTrigger_at_sentry_point();         // 哨兵点
    new AreaTrigger_at_brewfest();             // 美酒节
    new AreaTrigger_at_area_52_entrance();     // 52区入口
    new AreaTrigger_at_frostgrips_hollow();    // 弗罗斯特格里斯空谷
}

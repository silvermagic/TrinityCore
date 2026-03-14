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
 * @file    go_scripts.cpp
 * @brief   游戏对象脚本模块
 *
 * 本文件实现了各种游戏对象（GameObject）的交互脚本逻辑。
 * 游戏对象包括门、宝箱、传送门、任务物品、节日道具等，
 * 玩家可以通过右键点击或特定条件触发与这些对象的交互。
 *
 * 主要功能包括:
 * - 任务相关对象（镀金火盆、石板等）
 * - 随机召唤NPC的对象（以太监狱、储藏器）
 * - 传送水晶和传送门
 * - 术士灵魂之井（分发治疗石）
 * - 节日相关对象（篝火、彩带柱、音乐播放器）
 * - 城市钟声系统
 *
 * 性能注意事项:
 * - OnGossipHello会在每次玩家点击对象时调用，需保持简洁
 * - 避免在频繁调用的函数中进行数据库查询
 * - 使用事件调度器管理定时任务
 */

/* ContentData
go_ethereum_prison
go_ethereum_stasis
go_southfury_moonstone
go_resonite_cask
go_tablet_of_the_seven
go_tele_to_dalaran_crystal
go_tele_to_violet_stand
go_soulwell
go_amberpine_outhouse
go_veil_skith_cage
go_bells
EndContentData */

#include "ScriptMgr.h"
#include "DBCStructure.h"
#include "GameEventMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameTime.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "WorldSession.h"

/*######
## go_gilded_brazier - 镀金火盆（圣骑士第一次试炼任务）
######*/

/**
 * @brief 镀金火盆相关NPC和任务ID
 */
enum GildedBrazier
{
    NPC_STILLBLADE        = 17716,    ///< NPC：静刃（试炼目标）
    QUEST_THE_FIRST_TRIAL = 9678     ///< 任务：第一次试炼
};

/**
 * @class go_gilded_brazier
 * @brief 镀金火盆游戏对象脚本
 *
 * 当正在进行圣骑士职业任务"第一次试炼"的玩家点击镀金火盆时，
 * 召唤静刃NPC进行战斗。
 *
 * 任务机制：
 * - 玩家需要点击火盆触发试炼
 * - 击败静刃完成任务目标
 * - NPC死亡后消失
 */
class go_gilded_brazier : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_gilded_brazier() : GameObjectScript("go_gilded_brazier") { }

    /**
     * @class go_gilded_brazierAI
     * @brief 镀金火盆AI实现
     */
    struct go_gilded_brazierAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_gilded_brazierAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 玩家点击游戏对象时调用
         * @param player 点击对象的玩家指针
         * @return 返回true表示阻止默认处理
         *
         * 触发条件：
         * - 对象类型必须为GOOBER（可交互对象）
         * - 玩家正在进行"第一次试炼"任务
         *
         * 召唤逻辑：
         * - 在指定位置召唤静刃NPC
         * - NPC立即攻击玩家
         * - NPC死亡后消失
         */
        bool OnGossipHello(Player* player) override
        {
            // 检查游戏对象类型
            if (me->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
            {
                // 验证任务进度
                if (player->GetQuestStatus(QUEST_THE_FIRST_TRIAL) == QUEST_STATUS_INCOMPLETE)
                {
                    // 召唤静刃NPC，死亡后1分钟消失
                    if (Creature* Stillblade = player->SummonCreature(NPC_STILLBLADE, 8106.11f, -7542.06f, 151.775f, 3.02598f, TEMPSUMMON_DEAD_DESPAWN, 1min))
                        Stillblade->AI()->AttackStart(player);  // 立即攻击玩家
                }
            }
            return true;
        }
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_gilded_brazierAI(go);
    }
};

/*######
## go_tablet_of_the_seven - 七贤石板
######*/

/**
 * @class go_tablet_of_the_seven
 * @brief 七贤石板游戏对象脚本
 *
 * 当玩家点击石板时，为正在进行任务4296的玩家施放法术15065。
 * 这是一个任务相关的可交互对象。
 *
 * 任务机制：
 * - 玩家需要拓印石板内容
 * - 点击石板自动施放拓印法术
 */
class go_tablet_of_the_seven : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_tablet_of_the_seven() : GameObjectScript("go_tablet_of_the_seven") { }

    /**
     * @class go_tablet_of_the_sevenAI
     * @brief 七贤石板AI实现
     */
    struct go_tablet_of_the_sevenAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_tablet_of_the_sevenAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 玩家点击游戏对象时调用
         * @param player 点击对象的玩家指针
         * @return 返回true表示阻止默认处理
         *
         * @todo 应该使用对话选项("拓印石板")替代，如果Trinity支持的话
         *
         * 触发条件：
         * - 对象类型必须为QUESTGIVER（任务给予者）
         * - 玩家正在进行任务4296
         */
        bool OnGossipHello(Player* player) override
        {
            // 检查游戏对象类型
            if (me->GetGoType() != GAMEOBJECT_TYPE_QUESTGIVER)
                return true;

            // 验证任务进度并施放拓印法术
            if (player->GetQuestStatus(4296) == QUEST_STATUS_INCOMPLETE)
                player->CastSpell(player, 15065, false);

            return true;
        }
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_tablet_of_the_sevenAI(go);
    }
};

/*######
## go_ethereum_prison - 以太监狱
######*/

/**
 * @brief 以太监狱相关声望法术ID
 */
enum EthereumPrison
{
    SPELL_REP_LC        = 39456,    ///< 塞纳里奥远征队声望奖励
    SPELL_REP_SHAT      = 39457,    ///< 沙塔尔声望奖励
    SPELL_REP_CE        = 39460,    ///< 塞纳里奥议会声望奖励
    SPELL_REP_CON       = 39474,    ///< 联合团声望奖励
    SPELL_REP_KT        = 39475,    ///< 库雷尼声望奖励
    SPELL_REP_SPOR      = 39476     ///< 孢子村声望奖励
};

/**
 * @brief 以太监狱可能召唤的NPC列表
 *
 * 前6个是友善NPC，后7个是敌对NPC
 */
const uint32 NpcPrisonEntry[] =
{
    22810, 22811, 22812, 22813, 22814, 22815,               // 友善NPC
    20783, 20784, 20785, 20786, 20788, 20789, 20790         // 敌对NPC
};

/**
 * @class go_ethereum_prison
 * @brief 以太监狱游戏对象脚本
 *
 * 当玩家点击以太监狱时，随机召唤一个NPC。
 * 召唤的NPC可能是友善的（给予声望奖励）或敌对的（需要战斗）。
 *
 * 机制：
 * - 随机从13个NPC中选择一个召唤
 * - 友善NPC会根据其阵营给予对应的声望奖励
 * - 敌对NPC需要玩家击败
 */
class go_ethereum_prison : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_ethereum_prison() : GameObjectScript("go_ethereum_prison") { }

    /**
     * @class go_ethereum_prisonAI
     * @brief 以太监狱AI实现
     */
    struct go_ethereum_prisonAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_ethereum_prisonAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 玩家点击游戏对象时调用
         * @param player 点击对象的玩家指针
         * @return 返回false表示允许默认处理
         *
         * 触发逻辑：
         * 1. 激活监狱门动画
         * 2. 随机选择一个NPC召唤
         * 3. 如果NPC友善，根据其阵营给予声望奖励
         * 4. 如果NPC敌对，需要玩家战斗
         *
         * 性能注意：
         * - 使用rand32()生成随机数
         * - 友善NPC立即给予奖励并消失
         */
        bool OnGossipHello(Player* player) override
        {
            me->UseDoorOrButton();  // 激活开门动画
            // 随机选择NPC
            int Random = rand32() % (sizeof(NpcPrisonEntry) / sizeof(uint32));

            // 召唤NPC
            if (Creature* creature = player->SummonCreature(NpcPrisonEntry[Random], me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetAbsoluteAngle(player),
                TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s))
            {
                // 如果NPC对玩家友善
                if (!creature->IsHostileTo(player))
                {
                    if (FactionTemplateEntry const* pFaction = creature->GetFactionTemplateEntry())
                    {
                        uint32 Spell = 0;

                        // 根据NPC阵营选择对应的声望奖励法术
                        switch (pFaction->Faction)
                        {
                            case 1011: Spell = SPELL_REP_LC; break;    // 塞纳里奥远征队
                            case 935: Spell = SPELL_REP_SHAT; break;   // 沙塔尔
                            case 942: Spell = SPELL_REP_CE; break;     // 塞纳里奥议会
                            case 933: Spell = SPELL_REP_CON; break;    // 联合团
                            case 989: Spell = SPELL_REP_KT; break;     // 库雷尼
                            case 970: Spell = SPELL_REP_SPOR; break;   // 孢子村
                        }

                        if (Spell)
                            creature->CastSpell(player, Spell, false);  // 给予声望奖励
                        else
                            TC_LOG_ERROR("scripts", "go_ethereum_prison summoned Creature (entry {}) but faction ({}) are not expected by script.", creature->GetEntry(), creature->GetFaction());
                    }
                }
            }

            return false;
        }
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_ethereum_prisonAI(go);
    }
};

/*######
## go_ethereum_stasis - 以太储藏器
######*/

/**
 * @brief 以太储藏器可能召唤的NPC列表
 */
const uint32 NpcStasisEntry[] =
{
    22825, 20888, 22827, 22826, 22828
};

/**
 * @class go_ethereum_stasis
 * @brief 以太储藏器游戏对象脚本
 *
 * 当玩家点击以太储藏器时，随机召唤一个NPC。
 * 与以太监狱类似，但召唤池更小（只有5个NPC）。
 */
class go_ethereum_stasis : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_ethereum_stasis() : GameObjectScript("go_ethereum_stasis") { }

    /**
     * @class go_ethereum_stasisAI
     * @brief 以太储藏器AI实现
     */
    struct go_ethereum_stasisAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_ethereum_stasisAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 玩家点击游戏对象时调用
         * @param player 点击对象的玩家指针
         * @return 返回false表示允许默认处理
         *
         * 触发逻辑：
         * 1. 激活储藏器门动画
         * 2. 随机选择一个NPC召唤
         */
        bool OnGossipHello(Player* player) override
        {
            me->UseDoorOrButton();  // 激活开门动画
            // 随机选择NPC
            int Random = rand32() % (sizeof(NpcStasisEntry) / sizeof(uint32));

            // 召唤NPC
            player->SummonCreature(NpcStasisEntry[Random], me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetAbsoluteAngle(player),
                TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);

            return false;
        }
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_ethereum_stasisAI(go);
    }
};

/*######
## go_resonite_cask - 共振酒桶
######*/

/**
 * @brief 共振酒桶相关NPC ID
 */
enum ResoniteCask
{
    NPC_GOGGEROC    = 11920    ///< NPC：戈格罗克
};

/**
 * @class go_resonite_cask
 * @brief 共振酒桶游戏对象脚本
 *
 * 当玩家点击共振酒桶时，召唤戈格罗克NPC。
 * 这是一个任务相关的召唤对象。
 */
class go_resonite_cask : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_resonite_cask() : GameObjectScript("go_resonite_cask") { }

    /**
     * @class go_resonite_caskAI
     * @brief 共振酒桶AI实现
     */
    struct go_resonite_caskAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_resonite_caskAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 玩家点击游戏对象时调用
         * @param player 点击对象的玩家指针（未使用）
         * @return 返回false表示允许默认处理
         *
         * 触发条件：
         * - 对象类型必须为GOOBER（可交互对象）
         *
         * 召唤逻辑：
         * - 在对象位置召唤戈格罗克
         * - NPC脱战5分钟后消失
         */
        bool OnGossipHello(Player* /*player*/) override
        {
            // 检查游戏对象类型
            if (me->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
                // 召唤NPC：使用对象位置，脱战5分钟后消失
                me->SummonCreature(NPC_GOGGEROC, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5min);

            return false;
        }
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_resonite_caskAI(go);
    }
};

/*######
## go_southfury_moonstone - 南月石
######*/

/**
 * @brief 南月石相关NPC和法术ID
 */
enum Southfury
{
    NPC_RIZZLE                  = 23002,    ///< NPC：里泽尔
    SPELL_BLACKJACK             = 39865,    ///< 法术：短棍（眩晕玩家）
    SPELL_SUMMON_RIZZLE         = 39866     ///< 法术：召唤里泽尔
};

/**
 * @class go_southfury_moonstone
 * @brief 南月石游戏对象脚本
 *
 * 当玩家点击南月石时，召唤里泽尔NPC并对玩家施放眩晕法术。
 * 这是一个恶作剧式的交互对象。
 *
 * 机制：
 * - 召唤里泽尔NPC
 * - NPC对玩家施放短棍法术使其眩晕
 */
class go_southfury_moonstone : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_southfury_moonstone() : GameObjectScript("go_southfury_moonstone") { }

    /**
     * @class go_southfury_moonstoneAI
     * @brief 南月石AI实现
     */
    struct go_southfury_moonstoneAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_southfury_moonstoneAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 玩家点击游戏对象时调用
         * @param player 点击对象的玩家指针
         * @return 返回false表示允许默认处理
         *
         * 注意：implicitTarget=48在编写此代码时未实现，
         * 手动召唤可能已满足需求。
         */
        bool OnGossipHello(Player* player) override
        {
            // 召唤里泽尔NPC，死亡后消失
            if (Creature* creature = player->SummonCreature(NPC_RIZZLE, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_DEAD_DESPAWN))
                creature->CastSpell(player, SPELL_BLACKJACK, false);  // 眩晕玩家

            return false;
        }
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_southfury_moonstoneAI(go);
    }
};

/*######
## go_tele_to_dalaran_crystal - 达拉然传送水晶
######*/

/**
 * @brief 达拉然传送水晶相关任务ID
 */
enum DalaranCrystal
{
    QUEST_LEARN_LEAVE_RETURN    = 12790,    ///< 任务：学习离开和返回
    QUEST_TELE_CRYSTAL_FLAG     = 12845     ///< 任务：传送水晶标记（解锁水晶使用权限）
};

/// 传送失败提示消息
#define GO_TELE_TO_DALARAN_CRYSTAL_FAILED   "This teleport crystal cannot be used until the teleport crystal in Dalaran has been used at least once."

/**
 * @class go_tele_to_dalaran_crystal
 * @brief 达拉然传送水晶游戏对象脚本
 *
 * 玩家需要先使用达拉然城内的传送水晶后，才能使用城外的传送水晶。
 * 这个脚本检查玩家是否已解锁传送权限。
 *
 * 使用条件：
 * - 必须完成任务QUEST_TELE_CRYSTAL_FLAG（使用过达拉然水晶）
 */
class go_tele_to_dalaran_crystal : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_tele_to_dalaran_crystal() : GameObjectScript("go_tele_to_dalaran_crystal") { }

    /**
     * @class go_tele_to_dalaran_crystalAI
     * @brief 达拉然传送水晶AI实现
     */
    struct go_tele_to_dalaran_crystalAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_tele_to_dalaran_crystalAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 玩家点击游戏对象时调用
         * @param player 点击对象的玩家指针
         * @return 返回true阻止传送（未解锁），返回false允许传送
         *
         * 检查逻辑：
         * - 如果玩家完成了解锁任务，允许使用（返回false）
         * - 否则显示错误消息并阻止传送（返回true）
         */
        bool OnGossipHello(Player* player) override
        {
            // 检查是否已解锁传送权限
            if (player->GetQuestRewardStatus(QUEST_TELE_CRYSTAL_FLAG))
                return false;  // 允许传送

            // 未解锁，显示错误消息
            player->GetSession()->SendNotification(GO_TELE_TO_DALARAN_CRYSTAL_FAILED);
            return true;  // 阻止传送
        }
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_tele_to_dalaran_crystalAI(go);
    }
};

/*######
## go_tele_to_violet_stand
######*/

class go_tele_to_violet_stand : public GameObjectScript
{
public:
    go_tele_to_violet_stand() : GameObjectScript("go_tele_to_violet_stand") { }

    struct go_tele_to_violet_standAI : public GameObjectAI
    {
        go_tele_to_violet_standAI(GameObject* go) : GameObjectAI(go) { }

        bool OnGossipHello(Player* player) override
        {
            if (player->GetQuestRewardStatus(QUEST_LEARN_LEAVE_RETURN) || player->GetQuestStatus(QUEST_LEARN_LEAVE_RETURN) == QUEST_STATUS_INCOMPLETE)
                return false;

            return true;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_tele_to_violet_standAI(go);
    }
};

/*######
## go_blood_filled_orb
######*/

enum BloodFilledOrb
{
    NPC_ZELEMAR     = 17830

};

class go_blood_filled_orb : public GameObjectScript
{
public:
    go_blood_filled_orb() : GameObjectScript("go_blood_filled_orb") { }

    struct go_blood_filled_orbAI : public GameObjectAI
    {
        go_blood_filled_orbAI(GameObject* go) : GameObjectAI(go) { }

        bool OnGossipHello(Player* player) override
        {
            if (me->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
                player->SummonCreature(NPC_ZELEMAR, -369.746f, 166.759f, -21.50f, 5.235f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);

            return true;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_blood_filled_orbAI(go);
    }
};

/*######
## go_soulwell - 灵魂之井（术士召唤的治疗石分发器）
######*/

/**
 * @brief 灵魂之井相关游戏对象和法术ID
 */
enum SoulWellData
{
    GO_SOUL_WELL_R1                     = 181621,   ///< 游戏对象：灵魂之井（等级1，大师级治疗石）
    GO_SOUL_WELL_R2                     = 193169,   ///< 游戏对象：灵魂之井（等级2，恶魔级治疗石）

    SPELL_IMPROVED_HEALTH_STONE_R1      = 18692,    ///< 天赋：强化治疗石等级1
    SPELL_IMPROVED_HEALTH_STONE_R2      = 18693,    ///< 天赋：强化治疗石等级2

    SPELL_CREATE_MASTER_HEALTH_STONE_R0 = 34130,    ///< 法术：制造大师治疗石（无天赋）
    SPELL_CREATE_MASTER_HEALTH_STONE_R1 = 34149,    ///< 法术：制造大师治疗石（天赋等级1）
    SPELL_CREATE_MASTER_HEALTH_STONE_R2 = 34150,    ///< 法术：制造大师治疗石（天赋等级2）

    SPELL_CREATE_FEL_HEALTH_STONE_R0    = 58890,    ///< 法术：制造恶魔治疗石（无天赋）
    SPELL_CREATE_FEL_HEALTH_STONE_R1    = 58896,    ///< 法术：制造恶魔治疗石（天赋等级1）
    SPELL_CREATE_FEL_HEALTH_STONE_R2    = 58898,    ///< 法术：制造恶魔治疗石（天赋等级2）
};

/**
 * @class go_soulwell
 * @brief 灵魂之井游戏对象脚本
 *
 * 灵魂之井是术士召唤的游戏对象，允许团队成员从中获取治疗石。
 * 治疗石的等级取决于术士的强化治疗石天赋。
 *
 * 机制：
 * - 只有与召唤者在同一团队的玩家才能使用
 * - 每个玩家一次只能持有一个治疗石
 * - 使用后消耗灵魂之井的一次使用次数
 *
 * 治疗石等级：
 * - R1灵魂之井：大师级治疗石（0/1/2点天赋）
 * - R2灵魂之井：恶魔级治疗石（0/1/2点天赋）
 */
class go_soulwell : public GameObjectScript
{
    public:
        /**
         * @brief 构造函数
         */
        go_soulwell() : GameObjectScript("go_soulwell") { }

        /**
         * @class go_soulwellAI
         * @brief 灵魂之井AI实现
         */
        struct go_soulwellAI : public GameObjectAI
        {
            uint32 _stoneSpell;   ///< 治疗石制造法术ID
            uint32 _stoneId;      ///< 治疗石物品ID

            /**
             * @brief 构造函数
             * @param go 游戏对象指针
             *
             * 初始化逻辑：
             * 1. 根据灵魂之井等级确定基础法术
             * 2. 检查召唤者的强化治疗石天赋
             * 3. 选择对应等级的治疗石制造法术
             * 4. 从法术中提取治疗石物品ID
             */
            go_soulwellAI(GameObject* go) : GameObjectAI(go)
            {
                _stoneSpell = 0;
                _stoneId = 0;
                switch (go->GetEntry())
                {
                    case GO_SOUL_WELL_R1:  // 大师级灵魂之井
                        _stoneSpell = SPELL_CREATE_MASTER_HEALTH_STONE_R0;
                        if (Unit* owner = go->GetOwner())
                        {
                            // 检查召唤者的强化治疗石天赋
                            if (owner->HasAura(SPELL_IMPROVED_HEALTH_STONE_R1))
                                _stoneSpell = SPELL_CREATE_MASTER_HEALTH_STONE_R1;
                            else if (owner->HasAura(SPELL_CREATE_MASTER_HEALTH_STONE_R2))
                                _stoneSpell = SPELL_CREATE_MASTER_HEALTH_STONE_R2;
                        }
                        break;
                    case GO_SOUL_WELL_R2:  // 恶魔级灵魂之井
                        _stoneSpell = SPELL_CREATE_FEL_HEALTH_STONE_R0;
                        if (Unit* owner = go->GetOwner())
                        {
                            // 检查召唤者的强化治疗石天赋
                            if (owner->HasAura(SPELL_IMPROVED_HEALTH_STONE_R1))
                                _stoneSpell = SPELL_CREATE_FEL_HEALTH_STONE_R1;
                            else if (owner->HasAura(SPELL_CREATE_MASTER_HEALTH_STONE_R2))
                                _stoneSpell = SPELL_CREATE_FEL_HEALTH_STONE_R2;
                        }
                        break;
                }
                // 验证法术有效性（不应发生）
                if (_stoneSpell == 0)
                    return;

                // 从法术中提取治疗石物品ID
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(_stoneSpell);
                if (!spellInfo)
                    return;

                _stoneId = spellInfo->GetEffect(EFFECT_0).ItemType;
            }

            /**
             * @brief 玩家点击游戏对象时调用
             * @param player 点击对象的玩家指针
             * @return 返回true阻止默认处理，返回false允许
             *
             * 处理逻辑：
             * 1. 验证灵魂之井状态（法术和物品ID有效）
             * 2. 验证玩家与召唤者的团队关系
             * 3. 检查玩家是否已有治疗石
             * 4. 让召唤者对玩家施放治疗石制造法术
             * 5. 如果成功创建物品，消耗灵魂之井使用次数
             */
            bool OnGossipHello(Player* player) override
            {
                Unit* owner = me->GetOwner();
                // 验证灵魂之井状态
                if (_stoneSpell == 0 || _stoneId == 0)
                    return true;

                // 验证召唤者存在且为玩家，并且玩家与召唤者在同一团队
                if (!owner || owner->GetTypeId() != TYPEID_PLAYER || !player->IsInSameRaidWith(owner->ToPlayer()))
                    return true;

                // 检查玩家是否已有治疗石
                if (player->HasItemCount(_stoneId))
                {
                    if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(_stoneSpell))
                        Spell::SendCastResult(player, spell, 0, SPELL_FAILED_TOO_MANY_OF_ITEM);
                    return true;
                }

                // 召唤者对玩家施放治疗石制造法术
                owner->CastSpell(player, _stoneSpell, true);
                // 如果成功创建物品，消耗灵魂之井使用次数
                if (player->HasItemCount(_stoneId))
                    me->AddUse();

                return false;
            }
        };

        /**
         * @brief 获取游戏对象AI实例
         * @param go 游戏对象指针
         * @return 返回新创建的AI实例
         */
        GameObjectAI* GetAI(GameObject* go) const override
        {
            return new go_soulwellAI(go);
        }
};

/*######
## go_amberpine_outhouse
######*/

#define GOSSIP_USE_OUTHOUSE "Use the outhouse."
#define GO_ANDERHOLS_SLIDER_CIDER_NOT_FOUND "Quest item Anderhol's Slider Cider not found."

enum AmberpineOuthouse
{
    ITEM_ANDERHOLS_SLIDER_CIDER     = 37247,
    NPC_OUTHOUSE_BUNNY              = 27326,
    QUEST_DOING_YOUR_DUTY           = 12227,
    SPELL_INDISPOSED                = 53017,
    SPELL_INDISPOSED_III            = 48341,
    SPELL_CREATE_AMBERSEEDS         = 48330,
    GOSSIP_OUTHOUSE_INUSE           = 12775,
    GOSSIP_OUTHOUSE_VACANT          = 12779
};

class go_amberpine_outhouse : public GameObjectScript
{
public:
    go_amberpine_outhouse() : GameObjectScript("go_amberpine_outhouse") { }

    struct go_amberpine_outhouseAI : public GameObjectAI
    {
        go_amberpine_outhouseAI(GameObject* go) : GameObjectAI(go) { }

        bool OnGossipHello(Player* player) override
        {
            QuestStatus status = player->GetQuestStatus(QUEST_DOING_YOUR_DUTY);
            if (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_REWARDED)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_USE_OUTHOUSE, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                SendGossipMenuFor(player, GOSSIP_OUTHOUSE_VACANT, me->GetGUID());
            }
            else
                SendGossipMenuFor(player, GOSSIP_OUTHOUSE_INUSE, me->GetGUID());

            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);
            if (action == GOSSIP_ACTION_INFO_DEF + 1)
            {
                CloseGossipMenuFor(player);
                Creature* target = GetClosestCreatureWithEntry(player, NPC_OUTHOUSE_BUNNY, 3.0f);
                if (target)
                {
                    target->AI()->SetData(1, player->GetNativeGender());
                    me->CastSpell(target, SPELL_INDISPOSED_III);
                }
                me->CastSpell(player, SPELL_INDISPOSED);
                if (player->HasItemCount(ITEM_ANDERHOLS_SLIDER_CIDER))
                    me->CastSpell(player, SPELL_CREATE_AMBERSEEDS);
                return true;
            }
            else
            {
                CloseGossipMenuFor(player);
                player->GetSession()->SendNotification(GO_ANDERHOLS_SLIDER_CIDER_NOT_FOUND);
                return false;
            }
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_amberpine_outhouseAI(go);
    }
};

class go_massive_seaforium_charge : public GameObjectScript
{
    public:
        go_massive_seaforium_charge() : GameObjectScript("go_massive_seaforium_charge") { }

        struct go_massive_seaforium_chargeAI : public GameObjectAI
        {
            go_massive_seaforium_chargeAI(GameObject* go) : GameObjectAI(go) { }

            bool OnGossipHello(Player* /*player*/) override
            {
                me->SetLootState(GO_JUST_DEACTIVATED);
                return true;
            }
        };

        GameObjectAI* GetAI(GameObject* go) const override
        {
            return new go_massive_seaforium_chargeAI(go);
        }
};

/*######
#### go_veil_skith_cage
#####*/

enum MissingFriends
{
   QUEST_MISSING_FRIENDS    = 10852,
   NPC_CAPTIVE_CHILD        = 22314,
   SAY_FREE_0               = 0,
};

class go_veil_skith_cage : public GameObjectScript
{
    public:
       go_veil_skith_cage() : GameObjectScript("go_veil_skith_cage") { }

       struct go_veil_skith_cageAI : public GameObjectAI
       {
           go_veil_skith_cageAI(GameObject* go) : GameObjectAI(go) { }

           bool OnGossipHello(Player* player) override
           {
               me->UseDoorOrButton();
               if (player->GetQuestStatus(QUEST_MISSING_FRIENDS) == QUEST_STATUS_INCOMPLETE)
               {
                   std::vector<Creature*> childrenList;
                   GetCreatureListWithEntryInGrid(childrenList, me, NPC_CAPTIVE_CHILD, INTERACTION_DISTANCE);
                   for (Creature* creature : childrenList)
                   {
                       player->KilledMonsterCredit(NPC_CAPTIVE_CHILD, creature->GetGUID());
                       creature->DespawnOrUnsummon(5s);
                       creature->GetMotionMaster()->MovePoint(1, me->GetPositionX() + 5, me->GetPositionY(), me->GetPositionZ());
                       creature->AI()->Talk(SAY_FREE_0);
                       creature->GetMotionMaster()->Clear();
                   }
               }
               return false;
           }
       };

       GameObjectAI* GetAI(GameObject* go) const override
       {
           return new go_veil_skith_cageAI(go);
       }
};

/*######
## go_midsummer_bonfire
######*/

enum MidsummerBonfire
{
    STAMP_OUT_BONFIRE_QUEST_COMPLETE    = 45458,
};

class go_midsummer_bonfire : public GameObjectScript
{
public:
    go_midsummer_bonfire() : GameObjectScript("go_midsummer_bonfire") { }

    struct go_midsummer_bonfireAI : public GameObjectAI
    {
        go_midsummer_bonfireAI(GameObject* go) : GameObjectAI(go) { }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 /*gossipListId*/) override
        {
            player->CastSpell(player, STAMP_OUT_BONFIRE_QUEST_COMPLETE, true);
            CloseGossipMenuFor(player);
            return false;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_midsummer_bonfireAI(go);
    }
};

/**
 * @brief 仲夏火焰节彩带柱相关法术和NPC ID
 */
enum MidsummerPoleRibbon
{
    SPELL_TEST_RIBBON_POLE_1 = 29705,   ///< 法术：测试彩带柱1（彩带效果）
    SPELL_TEST_RIBBON_POLE_2 = 29726,   ///< 法术：测试彩带柱2（彩带效果）
    SPELL_TEST_RIBBON_POLE_3 = 29727,   ///< 法术：测试彩带柱3（彩带效果）
    NPC_POLE_RIBBON_BUNNY    = 17066,   ///< NPC：彩带柱兔子（视觉效果控制器）
    ACTION_COSMETIC_FIRES    = 0        ///< 动作：视觉火焰效果
};

/// 彩带柱法术数组，随机选择一个施放
uint32 const RibbonPoleSpells[3] =
{
    SPELL_TEST_RIBBON_POLE_1,
    SPELL_TEST_RIBBON_POLE_2,
    SPELL_TEST_RIBBON_POLE_3
};

/**
 * @class go_midsummer_ribbon_pole
 * @brief 仲夏火焰节彩带柱游戏对象脚本
 *
 * 玩家点击彩带柱时，会获得随机彩带效果并开始跳舞。
 * 这是仲夏火焰节的互动活动之一。
 */
class go_midsummer_ribbon_pole : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_midsummer_ribbon_pole() : GameObjectScript("go_midsummer_ribbon_pole") { }

    /**
     * @class go_midsummer_ribbon_poleAI
     * @brief 彩带柱AI实现
     */
    struct go_midsummer_ribbon_poleAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_midsummer_ribbon_poleAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 玩家点击游戏对象时调用
         * @param player 点击对象的玩家指针
         * @return 返回true阻止默认处理
         *
         * 触发逻辑：
         * 1. 查找附近的彩带柱兔子NPC
         * 2. 触发视觉火焰效果
         * 3. 对玩家施放随机彩带法术
         */
        bool OnGossipHello(Player* player) override
        {
            // 查找10码内的彩带柱兔子NPC
            if (Creature* creature = me->FindNearestCreature(NPC_POLE_RIBBON_BUNNY, 10.0f))
            {
                // 触发视觉火焰效果
                creature->GetAI()->DoAction(ACTION_COSMETIC_FIRES);
                // 对玩家施放随机彩带法术
                player->CastSpell(player, RibbonPoleSpells[urand(0, 2)], true);
            }
            return true;
        }
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_midsummer_ribbon_poleAI(go);
    }
};

/*####
## go_brewfest_music - 美酒节音乐播放器
####*/

/**
 * @brief 美酒节音乐事件ID（对应不同音乐曲目）
 */
enum BrewfestMusic
{
    EVENT_BREWFESTDWARF01 = 11810,  ///< 矮人音乐1（1.35分钟）
    EVENT_BREWFESTDWARF02 = 11812,  ///< 矮人音乐2（1.55分钟）
    EVENT_BREWFESTDWARF03 = 11813,  ///< 矮人音乐3（0.23分钟）
    EVENT_BREWFESTGOBLIN01 = 11811, ///< 哥布林音乐1（1.08分钟）
    EVENT_BREWFESTGOBLIN02 = 11814, ///< 哥布林音乐2（1.33分钟）
    EVENT_BREWFESTGOBLIN03 = 11815  ///< 哥布林音乐3（0.28分钟）
};

/// 各音乐播放时长（秒）
constexpr Seconds EVENT_BREWFESTDWARF01_TIME = 95s;
constexpr Seconds EVENT_BREWFESTDWARF02_TIME = 155s;
constexpr Seconds EVENT_BREWFESTDWARF03_TIME = 23s;
constexpr Seconds EVENT_BREWFESTGOBLIN01_TIME = 68s;
constexpr Seconds EVENT_BREWFESTGOBLIN02_TIME = 93s;
constexpr Seconds EVENT_BREWFESTGOBLIN03_TIME = 28s;

/**
 * @brief 美酒节音乐播放区域ID
 */
enum BrewfestMusicAreas
{
    SILVERMOON = 3430,     ///< 银月城（部落）
    UNDERCITY = 1497,      ///< 幽暗城
    ORGRIMMAR_1 = 1296,    ///< 奥格瑞玛区域1
    ORGRIMMAR_2 = 14,      ///< 奥格瑞玛区域2
    THUNDERBLUFF = 1638,   ///< 雷霆崖
    IRONFORGE_1 = 809,     ///< 铁炉堡区域1（联盟）
    IRONFORGE_2 = 1,       ///< 铁炉堡区域2
    STORMWIND = 12,        ///< 暴风城
    EXODAR = 3557,         ///< 埃索达
    DARNASSUS = 1657,      ///< 达纳苏斯
    SHATTRATH = 3703       ///< 沙塔斯（中立）
};

/**
 * @brief 美酒节音乐播放事件类型
 */
enum BrewfestMusicEvents
{
    EVENT_BM_SELECT_MUSIC = 1,  ///< 选择音乐事件
    EVENT_BM_START_MUSIC = 2    ///< 开始播放音乐事件
};

/**
 * @class go_brewfest_music
 * @brief 美酒节音乐播放器游戏对象脚本
 *
 * 在美酒节期间自动播放音乐。根据所在区域和玩家阵营播放不同的音乐：
 * - 部落区域播放哥布林音乐
 * - 联盟区域播放矮人音乐
 * - 沙塔斯根据玩家阵营分别播放
 *
 * 机制：
 * - 随机选择3首音乐中的一首
 * - 播放完成后自动选择下一首
 * - 每5秒向客户端发送音乐数据包
 */
class go_brewfest_music : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_brewfest_music() : GameObjectScript("go_brewfest_music") { }

    /**
     * @class go_brewfest_musicAI
     * @brief 美酒节音乐播放器AI实现
     */
    struct go_brewfest_musicAI : public GameObjectAI
    {
        uint32 rnd = 0;               ///< 当前随机音乐索引
        Milliseconds musicTime = 1s;  ///< 当前音乐播放时长

        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         *
         * 初始化事件调度器：
         * - 1秒后选择音乐
         * - 2秒后开始播放
         */
        go_brewfest_musicAI(GameObject* go) : GameObjectAI(go)
        {
            _events.ScheduleEvent(EVENT_BM_SELECT_MUSIC, 1s);
            _events.ScheduleEvent(EVENT_BM_START_MUSIC, 2s);
        }

        /**
         * @brief 更新AI，处理定时事件
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 处理两个主要事件：
         * 1. EVENT_BM_SELECT_MUSIC：选择下一首音乐
         * 2. EVENT_BM_START_MUSIC：向客户端发送音乐播放数据包
         */
        void UpdateAI(uint32 diff) override
        {
            _events.Update(diff);
            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                case EVENT_BM_SELECT_MUSIC:
                    // 检查美酒节是否激活
                    if (!IsHolidayActive(HOLIDAY_BREWFEST))
                        break;
                    // 随机选择音乐索引（0-2）
                    rnd = urand(0, 2);
                    // 在当前音乐播放完成后选择新音乐
                    _events.ScheduleEvent(EVENT_BM_SELECT_MUSIC, musicTime);
                    break;
                case EVENT_BM_START_MUSIC:
                    // 检查美酒节是否激活
                    if (!IsHolidayActive(HOLIDAY_BREWFEST))
                        break;

                    // 根据区域ID播放对应阵营的音乐
                    switch (me->GetAreaId())
                    {
                        // 部落区域
                        case SILVERMOON:
                        case UNDERCITY:
                        case ORGRIMMAR_1:
                        case ORGRIMMAR_2:
                        case THUNDERBLUFF:
                            // 播放哥布林音乐
                            if (rnd == 0)
                            {
                                me->PlayDirectMusic(EVENT_BREWFESTGOBLIN01);
                                musicTime = EVENT_BREWFESTGOBLIN01_TIME;
                            }
                            else if (rnd == 1)
                            {
                                me->PlayDirectMusic(EVENT_BREWFESTGOBLIN02);
                                musicTime = EVENT_BREWFESTGOBLIN02_TIME;
                            }
                            else
                            {
                                me->PlayDirectMusic(EVENT_BREWFESTGOBLIN03);
                                musicTime = EVENT_BREWFESTGOBLIN03_TIME;
                            }
                            break;
                        // 联盟区域
                        case IRONFORGE_1:
                        case IRONFORGE_2:
                        case STORMWIND:
                        case EXODAR:
                        case DARNASSUS:
                            // 播放矮人音乐
                            if (rnd == 0)
                            {
                                me->PlayDirectMusic(EVENT_BREWFESTDWARF01);
                                musicTime = EVENT_BREWFESTDWARF01_TIME;
                            }
                            else if (rnd == 1)
                            {
                                me->PlayDirectMusic(EVENT_BREWFESTDWARF02);
                                musicTime = EVENT_BREWFESTDWARF02_TIME;
                            }
                            else
                            {
                                me->PlayDirectMusic(EVENT_BREWFESTDWARF03);
                                musicTime = EVENT_BREWFESTDWARF03_TIME;
                            }
                            break;
                        // 中立区域
                        case SHATTRATH:
                            // 获取附近所有玩家
                            std::vector<Player*> playersNearby;
                            me->GetPlayerListInGrid(playersNearby, me->GetVisibilityRange());
                            // 根据玩家阵营播放不同音乐
                            for (Player* player : playersNearby)
                            {
                                if (player->GetTeamId() == TEAM_HORDE)
                                {
                                    // 部落玩家播放哥布林音乐
                                    if (rnd == 0)
                                    {
                                        me->PlayDirectMusic(EVENT_BREWFESTGOBLIN01);
                                        musicTime = EVENT_BREWFESTGOBLIN01_TIME;
                                    }
                                    else if (rnd == 1)
                                    {
                                        me->PlayDirectMusic(EVENT_BREWFESTGOBLIN02);
                                        musicTime = EVENT_BREWFESTGOBLIN02_TIME;
                                    }
                                    else
                                    {
                                        me->PlayDirectMusic(EVENT_BREWFESTGOBLIN03);
                                        musicTime = EVENT_BREWFESTGOBLIN03_TIME;
                                    }
                                }
                                else
                                {
                                    // 联盟玩家播放矮人音乐
                                    if (rnd == 0)
                                    {
                                        me->PlayDirectMusic(EVENT_BREWFESTDWARF01);
                                        musicTime = EVENT_BREWFESTDWARF01_TIME;
                                    }
                                    else if (rnd == 1)
                                    {
                                        me->PlayDirectMusic(EVENT_BREWFESTDWARF02);
                                        musicTime = EVENT_BREWFESTDWARF02_TIME;
                                    }
                                    else
                                    {
                                        me->PlayDirectMusic(EVENT_BREWFESTDWARF03);
                                        musicTime = EVENT_BREWFESTDWARF03_TIME;
                                    }
                                }
                            }
                            break;
                    }

                    // 每5秒向客户端发送一次音乐播放数据包
                    _events.ScheduleEvent(EVENT_BM_START_MUSIC, 5s);
                    break;
                default:
                    break;
                }
            }
        }
    private:
        EventMap _events;  ///< 事件调度器
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_brewfest_musicAI(go);
    }
};

/*####
## go_midsummer_music - 仲夏火焰节音乐播放器
####*/

/**
 * @brief 仲夏火焰节音乐ID
 */
enum MidsummerMusic
{
    EVENTMIDSUMMERFIREFESTIVAL_A = 12319, ///< 联盟火焰节音乐（1.08分钟）
    EVENTMIDSUMMERFIREFESTIVAL_H = 12325, ///< 部落火焰节音乐（1.12分钟）
};

/**
 * @brief 仲夏音乐播放事件类型
 */
enum MidsummerMusicEvents
{
    EVENT_MM_START_MUSIC = 1  ///< 开始播放音乐事件
};

/**
 * @class go_midsummer_music
 * @brief 仲夏火焰节音乐播放器游戏对象脚本
 *
 * 在仲夏火焰节期间播放音乐。根据玩家阵营播放不同版本：
 * - 联盟玩家听到联盟版本
 * - 部落玩家听到部落版本
 *
 * 机制：
 * - 每5秒向附近玩家发送音乐数据包
 * - 需要火焰节节日激活
 */
class go_midsummer_music : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_midsummer_music() : GameObjectScript("go_midsummer_music") { }

    /**
     * @class go_midsummer_musicAI
     * @brief 仲夏音乐播放器AI实现
     */
    struct go_midsummer_musicAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_midsummer_musicAI(GameObject* go) : GameObjectAI(go)
        {
            _events.ScheduleEvent(EVENT_MM_START_MUSIC, 1s);
        }

        /**
         * @brief 更新AI，处理定时事件
         * @param diff 距离上次更新的时间间隔（毫秒）
         */
        void UpdateAI(uint32 diff) override
        {
            _events.Update(diff);
            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_MM_START_MUSIC:
                    {
                        // 检查火焰节是否激活
                        if (!IsHolidayActive(HOLIDAY_FIRE_FESTIVAL))
                            break;

                        // 获取附近所有玩家
                        std::vector<Player*> playersNearby;
                        me->GetPlayerListInGrid(playersNearby, me->GetVisibilityRange());
                        // 根据玩家阵营播放不同音乐
                        for (Player* player : playersNearby)
                        {
                            if (player->GetTeamId() == TEAM_HORDE)
                                me->PlayDirectMusic(EVENTMIDSUMMERFIREFESTIVAL_H, player);  // 部落音乐
                            else
                                me->PlayDirectMusic(EVENTMIDSUMMERFIREFESTIVAL_A, player);  // 联盟音乐
                        }
                        // 每5秒发送一次音乐数据包（抓包值）
                        _events.ScheduleEvent(EVENT_MM_START_MUSIC, 5s);
                        break;
                    }
                default:
                    break;
                }
            }
        }
    private:
        EventMap _events;  ///< 事件调度器
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_midsummer_musicAI(go);
    }
};

/*####
## go_darkmoon_faire_music - 暗月马戏团音乐播放器
####*/

/**
 * @brief 暗月马戏团音乐ID
 */
enum DarkmoonFaireMusic
{
    MUSIC_DARKMOON_FAIRE_MUSIC = 8440  ///< 暗月马戏团音乐
};

/**
 * @brief 暗月马戏团音乐播放事件类型
 */
enum DarkmoonFaireMusicEvents
{
    EVENT_DFM_START_MUSIC = 1  ///< 开始播放音乐事件
};

/**
 * @class go_darkmoon_faire_music
 * @brief 暗月马戏团音乐播放器游戏对象脚本
 *
 * 在暗月马戏团活动期间播放音乐。暗月马戏团可能在以下地点：
 * - 艾尔文森林（联盟）
 * - 雷霆崖（部落）
 * - 沙塔斯（中立）
 *
 * 机制：
 * - 每5秒向客户端发送音乐数据包
 * - 需要暗月马戏团节日激活
 */
class go_darkmoon_faire_music : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_darkmoon_faire_music() : GameObjectScript("go_darkmoon_faire_music") { }

    /**
     * @class go_darkmoon_faire_musicAI
     * @brief 暗月马戏团音乐播放器AI实现
     */
    struct go_darkmoon_faire_musicAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_darkmoon_faire_musicAI(GameObject* go) : GameObjectAI(go)
        {
            _events.ScheduleEvent(EVENT_DFM_START_MUSIC, 1s);
        }

        /**
         * @brief 更新AI，处理定时事件
         * @param diff 距离上次更新的时间间隔（毫秒）
         */
        void UpdateAI(uint32 diff) override
        {
            _events.Update(diff);
            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_DFM_START_MUSIC:
                        // 检查暗月马戏团是否在任一地点激活
                        if (!IsHolidayActive(HOLIDAY_DARKMOON_FAIRE_ELWYNN) && !IsHolidayActive(HOLIDAY_DARKMOON_FAIRE_THUNDER) && !IsHolidayActive(HOLIDAY_DARKMOON_FAIRE_SHATTRATH))
                            break;
                        // 播放暗月马戏团音乐
                        me->PlayDirectMusic(MUSIC_DARKMOON_FAIRE_MUSIC);
                        // 每5秒发送一次音乐数据包（抓包值）
                        _events.ScheduleEvent(EVENT_DFM_START_MUSIC, 5s);
                        break;
                    default:
                        break;
                }
            }
        }
    private:
        EventMap _events;  ///< 事件调度器
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_darkmoon_faire_musicAI(go);
    }
};

/*####
## go_pirate_day_music - 海盗日音乐播放器
####*/

/**
 * @brief 海盗日音乐ID
 */
enum PirateDayMusic
{
    MUSIC_PIRATE_DAY_MUSIC = 12845  ///< 海盗日音乐
};

/**
 * @brief 海盗日音乐播放事件类型
 */
enum PirateDayMusicEvents
{
    EVENT_PDM_START_MUSIC = 1  ///< 开始播放音乐事件
};

/**
 * @class go_pirate_day_music
 * @brief 海盗日音乐播放器游戏对象脚本
 *
 * 在海盗日（9月19日）期间播放海盗主题音乐。
 *
 * 机制：
 * - 每5秒向客户端发送音乐数据包
 * - 需要海盗日节日激活
 */
class go_pirate_day_music : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_pirate_day_music() : GameObjectScript("go_pirate_day_music") { }

    /**
     * @class go_pirate_day_musicAI
     * @brief 海盗日音乐播放器AI实现
     */
    struct go_pirate_day_musicAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_pirate_day_musicAI(GameObject* go) : GameObjectAI(go)
        {
            _events.ScheduleEvent(EVENT_PDM_START_MUSIC, 1s);
        }

        /**
         * @brief 更新AI，处理定时事件
         * @param diff 距离上次更新的时间间隔（毫秒）
         */
        void UpdateAI(uint32 diff) override
        {
            _events.Update(diff);
            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                case EVENT_PDM_START_MUSIC:
                    // 检查海盗日是否激活
                    if (!IsHolidayActive(HOLIDAY_PIRATES_DAY))
                        break;
                    // 播放海盗日音乐
                    me->PlayDirectMusic(MUSIC_PIRATE_DAY_MUSIC);
                    // 每5秒发送一次音乐数据包（抓包值）
                    _events.ScheduleEvent(EVENT_PDM_START_MUSIC, 5s);
                    break;
                default:
                    break;
                }
            }
        }
    private:
        EventMap _events;  ///< 事件调度器
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_pirate_day_musicAI(go);
    }
};

/*####
## go_bells - 城市钟声系统
####*/

/**
 * @brief 钟声音效ID
 */
enum BellHourlySoundFX
{
    BELLTOLLHORDE      = 6595,  ///< 幽暗城钟声
    BELLTOLLTRIBAL     = 6675,  ///< 奥格瑞玛/雷霆崖鼓声
    BELLTOLLALLIANCE   = 6594,  ///< 暴风城钟声
    BELLTOLLNIGHTELF   = 6674,  ///< 达纳苏斯钟声
    BELLTOLLDWARFGNOME = 7234,  ///< 铁炉堡号角声
    BELLTOLLKHARAZHAN  = 9154   ///< 卡拉赞钟声
};

/**
 * @brief 钟声播放区域ID
 */
enum BellHourlySoundZones
{
    TIRISFAL_ZONE            = 85,     ///< 提瑞斯法林地
    UNDERCITY_ZONE           = 1497,   ///< 幽暗城
    DUN_MOROGH_ZONE          = 1,      ///< 丹莫罗
    IRONFORGE_ZONE           = 1537,   ///< 铁炉堡
    TELDRASSIL_ZONE          = 141,    ///< 泰达希尔
    DARNASSUS_ZONE           = 1657,   ///< 达纳苏斯
    ASHENVALE_ZONE           = 331,    ///< 灰谷
    HILLSBRAD_FOOTHILLS_ZONE = 267,    ///< 希尔斯布莱德丘陵
    DUSKWOOD_ZONE            = 10      ///< 暮色森林
};

/**
 * @brief 钟声游戏对象ID
 */
enum BellHourlyObjects
{
    GO_HORDE_BELL          = 175885,   ///< 部落钟
    GO_ALLIANCE_BELL       = 176573,   ///< 联盟钟
    GO_KHARAZHAN_BELL      = 182064    ///< 卡拉赞钟
};

/**
 * @brief 钟声相关事件和游戏事件ID
 */
enum BellHourlyMisc
{
    GAME_EVENT_HOURLY_BELLS = 73,  ///< 整点钟声游戏事件ID
    EVENT_RING_BELL        = 1     ///< 敲钟事件ID
};

/**
 * @class go_bells
 * @brief 城市钟声游戏对象脚本
 *
 * 实现游戏中各主要城市的整点钟声系统。
 * 根据钟的类型和所在区域播放不同的音效：
 * - 部落：亡灵钟声或兽人鼓声
 * - 联盟：人类钟声、暗夜精灵钟声或矮人号角
 * - 中立：卡拉赞钟声
 *
 * 机制：
 * - 每小时整点触发（通过游戏事件）
 * - 敲钟次数等于当前小时数（12小时制）
 * - 铁炉堡只响一次号角
 * - 每次敲钟间隔4秒
 */
class go_bells : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_bells() : GameObjectScript("go_bells") { }

    /**
     * @class go_bellsAI
     * @brief 钟声AI实现
     */
    struct go_bellsAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_bellsAI(GameObject* go) : GameObjectAI(go), _soundId(0) { }

        /**
         * @brief 初始化AI
         *
         * 根据游戏对象类型和所在区域选择合适的钟声音效：
         * - 部落钟：亡灵区域用亡灵钟声，其他用兽人鼓声
         * - 联盟钟：铁炉堡用号角，暗夜精灵区域用暗夜钟声，其他用人类钟声
         * - 卡拉赞钟：固定使用卡拉赞钟声
         */
        void InitializeAI() override
        {
            uint32 zoneId = me->GetZoneId();

            switch (me->GetEntry())
            {
                case GO_HORDE_BELL:  // 部落钟
                {
                    switch (zoneId)
                    {
                        case TIRISFAL_ZONE:
                        case UNDERCITY_ZONE:
                        case HILLSBRAD_FOOTHILLS_ZONE:
                        case DUSKWOOD_ZONE:
                            _soundId = BELLTOLLHORDE;  // 亡灵钟声
                            break;
                        default:
                            _soundId = BELLTOLLTRIBAL; // 兽人鼓声
                            break;
                    }
                    break;
                }
                case GO_ALLIANCE_BELL:  // 联盟钟
                {
                    switch (zoneId)
                    {
                        case IRONFORGE_ZONE:
                        case DUN_MOROGH_ZONE:
                            _soundId = BELLTOLLDWARFGNOME; // 矮人号角声
                            break;
                        case DARNASSUS_ZONE:
                        case TELDRASSIL_ZONE:
                        case ASHENVALE_ZONE:
                            _soundId = BELLTOLLNIGHTELF;   // 暗夜精灵钟声
                            break;
                        default:
                            _soundId = BELLTOLLALLIANCE;   // 人类钟声
                    }
                    break;
                }
                case GO_KHARAZHAN_BELL:  // 卡拉赞钟
                {
                    _soundId = BELLTOLLKHARAZHAN;
                    break;
                }
            }
        }

        /**
         * @brief 游戏事件回调
         * @param start 事件是否开始（true=开始，false=结束）
         * @param eventId 游戏事件ID
         *
         * 当整点钟声事件开始时：
         * 1. 获取当前游戏时间
         * 2. 计算敲钟次数（12小时制）
         * 3. 如果是铁炉堡号角，只响一次
         * 4. 调度敲钟事件，每次间隔4秒
         */
        void OnGameEvent(bool start, uint16 eventId) override
        {
            if (eventId == GAME_EVENT_HOURLY_BELLS && start)
            {
                time_t time = GameTime::GetGameTime();
                tm localTm;
                localtime_r(&time, &localTm);
                // 计算敲钟次数（12小时制）
                uint8 _rings = (localTm.tm_hour) % 12;
                if (_rings == 0) // 00:00和12:00敲12次
                {
                    _rings = 12;
                }

                // 矮人整点号角只在每小时开始时响一次
                if (_soundId == BELLTOLLDWARFGNOME)
                {
                    _rings = 1;
                }

                // 调度敲钟事件，每次间隔4秒
                for (auto i = 0; i < _rings; ++i)
                    _events.ScheduleEvent(EVENT_RING_BELL, Seconds(i * 4 + 1));
            }
        }

        /**
         * @brief 更新AI，处理敲钟事件
         * @param diff 距离上次更新的时间间隔（毫秒）
         */
        void UpdateAI(uint32 diff) override
        {
            _events.Update(diff);

            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_RING_BELL:
                        // 播放钟声音效
                        me->PlayDirectSound(_soundId);
                        break;
                    default:
                        break;
                }
            }
        }
    private:
        EventMap _events;  ///< 事件调度器
        uint32 _soundId;   ///< 钟声音效ID
    };

    /**
     * @brief 获取游戏对象AI实例
     * @param go 游戏对象指针
     * @return 返回新创建的AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_bellsAI(go);
    }
};

/**
 * @brief 注册所有游戏对象脚本
 *
 * 该函数由脚本系统在启动时调用，用于注册本文件中定义的所有游戏对象脚本。
 * 每个脚本实例化后会自动注册到脚本管理器中。
 */
void AddSC_go_scripts()
{
    new go_gilded_brazier();          // 镀金火盆（圣骑士任务）
    new go_southfury_moonstone();     // 南月石
    new go_tablet_of_the_seven();     // 七贤石板
    new go_ethereum_prison();         // 以太监狱
    new go_ethereum_stasis();         // 以太储藏器
    new go_resonite_cask();           // 共振酒桶
    new go_tele_to_dalaran_crystal(); // 达拉然传送水晶
    new go_tele_to_violet_stand();    // 紫罗兰立场传送器
    new go_blood_filled_orb();        // 血球
    new go_soulwell();                // 灵魂之井
    new go_amberpine_outhouse();      // 琥珀松小屋厕所
    new go_massive_seaforium_charge();// 大型海度斯炸药
    new go_veil_skith_cage();         // 维尔斯基斯笼子
    new go_midsummer_bonfire();       // 仲夏篝火
    new go_midsummer_ribbon_pole();   // 仲夏彩带柱
    new go_brewfest_music();          // 美酒节音乐
    new go_midsummer_music();         // 仲夏火焰节音乐
    new go_darkmoon_faire_music();    // 暗月马戏团音乐
    new go_pirate_day_music();        // 海盗日音乐
    new go_bells();                   // 城市钟声
}

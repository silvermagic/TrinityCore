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

/* ScriptData
SDName: Npc_Innkeeper
SDAuthor: WarHead
SD%Complete: 99%
SDComment: Complete
SDCategory: NPCs
EndScriptData */

/**
 * @file npc_innkeeper.cpp
 * @brief 旅店老板NPC脚本模块
 *
 * 本模块实现了旅店老板NPC的核心功能，包括：
 * - 设置炉石绑定位置（家园绑定）
 * - 商店交易功能
 * - 万圣节活动的"不给糖就捣蛋"互动
 *
 * 旅店老板是玩家在游戏中的重要交互NPC，提供休息和绑定功能。
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "GameEventMgr.h"
#include "Player.h"
#include "WorldSession.h"

/**
 * @brief 法术ID枚举定义
 *
 * 定义旅店老板脚本中使用的法术ID
 */
enum Spells
{
    SPELL_TRICK_OR_TREATED      = 24755,  ///< 万圣节"已接受不给糖就捣蛋"标记法术，防止玩家重复参与
    SPELL_TREAT                 = 24715   ///< 万圣节奖励法术，给予玩家糖果奖励
};

/**
 * @brief NPC菜单ID枚举定义
 *
 * 定义旅店老板交互时使用的菜单ID
 */
enum Npc
{
    NPC_GOSSIP_MENU = 9733,        ///< 旅店老板标准菜单ID
    NPC_GOSSIP_MENU_EVENT = 342,   ///< 万圣节活动菜单ID
};

/**
 * @brief 旅店老板NPC脚本类
 *
 * 继承自CreatureScript，用于处理旅店老板NPC的所有交互逻辑。
 * 提供标准的旅店服务功能以及节日特殊互动。
 */
class npc_innkeeper : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化旅店老板脚本，注册脚本名称为"npc_innkeeper"
     */
    npc_innkeeper() : CreatureScript("npc_innkeeper") { }

    /**
     * @brief 旅店老板AI结构体
     *
     * 继承自ScriptedAI，实现旅店老板的具体交互逻辑。
     * 处理玩家与旅店老板的对话菜单和选项响应。
     */
    struct npc_innkeeperAI : public ScriptedAI
    {
        /**
         * @brief AI构造函数
         * @param creature NPC生物对象指针
         *
         * 初始化旅店老板AI实例
         */
        npc_innkeeperAI(Creature* creature) : ScriptedAI(creature) { }

        /**
         * @brief 处理玩家右键点击NPC时的对话菜单显示
         * @param player 与NPC交互的玩家对象指针
         * @return bool 返回true表示成功处理交互
         *
         * 当玩家右键点击旅店老板时调用此函数。
         * 根据当前游戏事件和NPC功能标志，动态构建对话菜单选项：
         * - 如果万圣节活动开启且玩家未参与过，显示"不给糖就捣蛋"选项
         * - 如果NPC有任务提供者标志，显示任务菜单
         * - 如果NPC有商贩标志，显示商店选项
         * - 如果NPC有旅店老板标志，显示绑定炉石选项
         *
         * @note 此函数在玩家每次点击NPC时都会被调用
         */
        bool OnGossipHello(Player* player) override
        {
            // 初始化标准旅店老板对话菜单
            InitGossipMenuFor(player, NPC_GOSSIP_MENU);

            // 万圣节活动逻辑：如果万圣节活动开启且玩家没有"已接受不给糖就捣蛋"标记
            if (IsHolidayActive(HOLIDAY_HALLOWS_END) && !player->HasAura(SPELL_TRICK_OR_TREATED))
                AddGossipItemFor(player, NPC_GOSSIP_MENU_EVENT, 0, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

            // 如果NPC提供任务，准备任务菜单
            if (me->IsQuestGiver())
                player->PrepareQuestMenu(me->GetGUID());

            // 如果NPC是商贩，添加商店交易选项
            if (me->IsVendor())
                AddGossipItemFor(player, NPC_GOSSIP_MENU, 2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

            // 如果NPC是旅店老板，添加绑定炉石选项（设置家园）
            if (me->IsInnkeeper())
                AddGossipItemFor(player, NPC_GOSSIP_MENU, 1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INN);

            // 记录玩家与NPC的交互
            player->TalkedToCreature(me->GetEntry(), me->GetGUID());

            // 发送对话菜单给客户端
            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return true;
        }

        /**
         * @brief 处理玩家选择对话菜单选项
         * @param player 选择菜单选项的玩家对象指针
         * @param menuId 菜单ID（未使用）
         * @param gossipListId 菜单选项列表ID
         * @return bool 返回true表示成功处理选项
         *
         * 当玩家点击对话菜单中的选项时调用此函数。
         * 主要处理以下逻辑：
         * - 万圣节"不给糖就捣蛋"：50%几率获得糖果，50%几率获得恶作剧法术
         * - 商店交易：打开NPC商店界面
         * - 炉石绑定：将玩家的炉石绑定位置设置为当前位置
         *
         * @note 万圣节恶作剧法术包括各种变形效果（幽灵、忍者、海盗、骷髅等）
         */
        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            // 获取玩家选择的菜单选项对应的动作ID
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            // 清除当前对话菜单
            ClearGossipMenuFor(player);

            // 处理万圣节"不给糖就捣蛋"选项
            if (action == GOSSIP_ACTION_INFO_DEF + 1 && IsHolidayActive(HOLIDAY_HALLOWS_END) && !player->HasAura(SPELL_TRICK_OR_TREATED))
            {
                // 给玩家施加"已接受不给糖就捣蛋"标记，防止重复参与
                player->CastSpell(player, SPELL_TRICK_OR_TREATED, true);

                // 50%几率给予糖果奖励
                if (urand(0, 1))
                    player->CastSpell(player, SPELL_TREAT, true);
                else
                {
                    // 50%几率给予恶作剧法术（随机14种效果）
                    uint32 trickspell = 0;
                    switch (urand(0, 13))
                    {
                        case 0: trickspell = 24753; break; // 施法失败，随机30秒
                        case 1: trickspell = 24713; break; // 麻风侏儒变形
                        case 2: trickspell = 24735; break; // 男性幽灵变形
                        case 3: trickspell = 24736; break; // 女性幽灵变形
                        case 4: trickspell = 24710; break; // 男性忍者变形
                        case 5: trickspell = 24711; break; // 女性忍者变形
                        case 6: trickspell = 24708; break; // 男性海盗变形
                        case 7: trickspell = 24709; break; // 女性海盗变形
                        case 8: trickspell = 24723; break; // 骷髅变形
                        case 9: trickspell = 24753; break; // 恶作剧
                        case 10: trickspell = 24924; break; // 万圣节糖果
                        case 11: trickspell = 24925; break; // 万圣节糖果
                        case 12: trickspell = 24926; break; // 万圣节糖果
                        case 13: trickspell = 24927; break; // 万圣节糖果
                    }
                    player->CastSpell(player, trickspell, true);
                }
                // 关闭对话菜单
                CloseGossipMenuFor(player);
                return true;
            }

            // 关闭对话菜单
            CloseGossipMenuFor(player);

            // 处理其他菜单选项
            switch (action)
            {
                case GOSSIP_ACTION_TRADE:  // 商店交易选项
                    player->GetSession()->SendListInventory(me->GetGUID());
                    break;
                case GOSSIP_ACTION_INN:    // 炉石绑定选项
                    player->SetBindPoint(me->GetGUID());
                    break;
            }
            return true;
        }
    };

    /**
     * @brief 获取AI实例工厂函数
     * @param creature 需要创建AI的生物对象指针
     * @return CreatureAI* 返回新创建的旅店老板AI实例
     *
     * 当旅店老板NPC需要创建AI实例时调用此函数。
     * 该函数是CreatureScript框架的标准接口。
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_innkeeperAI(creature);
    }
};

/**
 * @brief 注册旅店老板脚本到脚本系统
 *
 * 此函数由脚本加载器在服务器启动时调用，用于将旅店老板脚本注册到游戏中。
 * 创建并初始化旅店老板脚本实例，使其能够在游戏中生效。
 *
 * @note 此函数在服务器启动时由脚本系统自动调用，不应手动调用
 */
void AddSC_npc_innkeeper()
{
    new npc_innkeeper();
}

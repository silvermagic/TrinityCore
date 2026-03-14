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
 * @file    deadmines.cpp
 * @brief   死亡矿坑副本通用脚本实现
 *
 * @details 该模块实现了死亡矿坑副本中的通用功能，主要包括：
 *          - 迪菲亚火药物品脚本：用于在大炮处引爆铁门
 *          该物品是副本进程的关键道具，用于触发铁门爆破事件。
 *
 * @note    该文件目前只包含一个物品脚本，可能在未来添加更多通用功能。
 *
 * ScriptData
 * SDName: Deadmines
 * SD%Complete: 0
 * SDComment: Placeholder
 * SDCategory: Deadmines
 * EndScriptData
 */

#include "ScriptMgr.h"
#include "deadmines.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Item.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Spell.h"
#include "WorldSession.h"

/*#####
# item_Defias_Gunpowder
#####*/

/**
 * @class item_defias_gunpowder
 * @brief 迪菲亚火药物品脚本类
 *
 * @details 实现了迪菲亚火药物品的使用逻辑。
 *          该物品用于在死亡矿坑副本中引爆大炮，炸开铁门。
 *
 *          使用流程：
 *          1. 玩家使用迪菲亚火药物品
 *          2. 目标必须是迪菲亚大炮
 *          3. 如果大炮和铁门存在，设置副本状态为"火药已使用"
 *          4. 消耗一个火药物品
 *          5. 副本脚本会检测状态并触发爆炸事件
 *
 * @note 该物品只能使用一次，使用后会被消耗。
 */
class item_defias_gunpowder : public ItemScript
{
public:
    /**
     * @brief 构造函数，注册物品脚本名称
     */
    item_defias_gunpowder() : ItemScript("item_defias_gunpowder") { }

    /**
     * @brief 物品使用回调函数
     *
     * @param player 使用物品的玩家
     * @param item 物品对象指针
     * @param targets 施法目标信息
     *
     * @return bool 返回true表示成功处理，false表示允许默认行为
     *
     * @details 当玩家使用迪菲亚火药时触发。
     *          检查以下条件：
     *          1. 玩家是否在副本中
     *          2. 副本状态是否允许使用火药（大炮未被使用）
     *          3. 目标是否为迪菲亚大炮
     *
     *          如果所有条件满足，设置副本状态并消耗物品。
     *
     * @note 性能注意事项：该函数会访问副本数据，确保副本脚本已初始化。
     */
    bool OnUse(Player* player, Item* item, SpellCastTargets const& targets) override
    {
        // 获取玩家所在的副本实例脚本
        InstanceScript* instance = player->GetInstanceScript();

        // 如果不在副本中或副本脚本未初始化，发送错误消息并返回
        if (!instance)
        {
            player->GetSession()->SendNotification("Instance script not initialized");
            return true;  // 返回true阻止物品使用
        }

        // 检查副本状态，如果大炮已经被使用过，则不允许再次使用
        if (instance->GetData(EVENT_STATE) != CANNON_NOT_USED)
            return false;  // 返回false允许默认行为（可能会显示错误信息）

        // 检查目标是否为迪菲亚大炮
        if (targets.GetGOTarget() && targets.GetGOTarget()->GetEntry() == GO_DEFIAS_CANNON)
        {
            // 设置副本状态为"火药已使用"，触发爆炸事件
            instance->SetData(EVENT_STATE, CANNON_GUNPOWDER_USED);
        }

        // 消耗一个火药物品
        player->DestroyItemCount(item->GetEntry(), 1, true);
        return true;  // 返回true表示已成功处理物品使用
    }
};

/**
 * @brief 注册死亡矿坑副本通用脚本
 *
 * @details 该函数在服务器启动时被调用，用于注册各种脚本实例。
 *          目前只注册了迪菲亚火药物品脚本。
 *          这是TrinityCore脚本系统的标准入口点。
 */
void AddSC_deadmines()
{
    new item_defias_gunpowder();
}

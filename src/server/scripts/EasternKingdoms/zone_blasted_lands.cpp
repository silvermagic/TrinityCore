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
 * @file zone_blasted_lands.cpp
 * @brief 诅咒之地(Blased Lands)区域脚本模块
 *
 * 本模块实现了诅咒之地区域内的游戏逻辑，包括：
 * - 任务相关法术脚本
 * - 特殊传送机制
 *
 * 诅咒之地位于东部王国大陆的南部，是一片被恶魔能量腐蚀的荒芜之地。
 */

#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Player.h"
#include "Group.h"

/*######
## Quest 3628: You Are Rakh'likh, Demon
## 任务3628：你是拉克利赫，恶魔
######*/

/**
 * @brief 传送到拉泽利克法术枚举
 *
 * 定义了用于将玩家传送到拉泽利克身边的法术ID
 */
enum TeleportToRazelikh
{
    SPELL_TELEPORT_SINGLE               = 12885,  ///< 单人传送法术 - 将单个玩家传送到拉泽利克身边
    SPELL_TELEPORT_SINGLE_IN_GROUP      = 13142   ///< 小队传送法术 - 将小队成员传送到拉泽利克身边
};

/**
 * @brief 拉泽利克小队传送法术脚本
 *
 * 实现任务"你是拉克利赫，恶魔"(Quest 3628)中的特殊传送机制。
 * 该法术会检查目标玩家是否在小队中，并据此选择不同的传送方式：
 * - 如果玩家在小队中，会传送玩家及其附近的队友
 * - 如果玩家不在小队中，仅传送该玩家
 *
 * 该脚本对应法术ID: 27686 - Teleport to Razelikh (GROUP)
 */
// 27686 - Teleport to Razelikh (GROUP)
class spell_razelikh_teleport_group : public SpellScript
{
    PrepareSpellScript(spell_razelikh_teleport_group);

    /**
     * @brief 验证法术数据有效性
     *
     * 在法术脚本注册时调用，验证所需的法术是否存在于数据库中
     *
     * @param spell 法术信息指针（未使用）
     * @return bool 如果所有依赖的法术都存在则返回true，否则返回false
     *
     * @note 此方法在服务器启动时调用，用于提前发现配置错误
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_TELEPORT_SINGLE, SPELL_TELEPORT_SINGLE_IN_GROUP });
    }

    /**
     * @brief 处理脚本效果
     *
     * 当法术效果命中目标时调用，执行实际的传送逻辑。
     *
     * @param effIndex 效果索引（未使用）
     *
     * 执行流程：
     * 1. 获取被法术命中的玩家
     * 2. 检查玩家是否在小队中
     * 3. 如果在小队中：
     *    - 遍历所有小队成员
     *    - 对距离目标玩家20码内且存活的成员施加小队传送法术
     * 4. 如果不在小队中：
     *    - 仅对目标玩家施加单人传送法术
     *
     * @note 传送法术使用instant cast（true参数）以避免施法时间
     * @note 性能考虑：小队成员数量通常较少，遍历开销可接受
     */
    void HandleScriptEffect(SpellEffIndex /* effIndex */)
    {
        // 获取被法术命中的玩家
        if (Player* player = GetHitPlayer())
        {
            // 检查玩家是否在小队中
            if (Group* group = player->GetGroup())
            {
                // 遍历小队成员
                for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                {
                    if (Player* member = itr->GetSource())
                    {
                        // 检查成员是否在目标玩家20码范围内且存活
                        if (member->IsWithinDistInMap(player, 20.0f) && !member->isDead())
                        {
                            // 对符合条件的成员施放小队传送法术
                            member->CastSpell(member, SPELL_TELEPORT_SINGLE_IN_GROUP, true);
                        }
                    }
                }
            }
            else
            {
                // 单人情况：仅传送目标玩家
                player->CastSpell(player, SPELL_TELEPORT_SINGLE, true);
            }
        }
    }

    /**
     * @brief 注册法术效果回调
     *
     * 在脚本初始化时调用，将HandleScriptEffect方法绑定到法术效果上。
     * 当法术的EFFECT_0（第0个效果）触发SPELL_EFFECT_SCRIPT_EFFECT类型时，
     * 会调用HandleScriptEffect方法。
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_razelikh_teleport_group::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 注册诅咒之地区域脚本
 *
 * 该函数由脚本系统在服务器启动时自动调用，用于注册本文件中定义的所有法术脚本。
 *
 * @note 函数名遵循TrinityCore命名规范：AddSC_<区域名称>
 * @note 该函数会在WorldSession初始化期间被调用，不应手动调用
 */
void AddSC_blasted_lands()
{
    // 注册拉泽利克小队传送法术脚本
    RegisterSpellScript(spell_razelikh_teleport_group);
}

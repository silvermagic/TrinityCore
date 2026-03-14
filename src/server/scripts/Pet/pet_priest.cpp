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
 * @file pet_priest.cpp
 * @brief 牧师宠物AI模块
 *
 * 本模块实现牧师职业相关宠物的AI行为，包括：
 * - 光明之泉 (Lightwell)：被动宠物，提供治疗光环，玩家可点击获得治疗
 * - 暗影恶魔 (Shadowfiend)：攻击宠物，为牧师恢复法力值
 *
 * 特殊行为：
 * - 光明之泉不会进入战斗，不会反击
 * - 暗影恶魔在召唤时，如果牧师有暗影恶魔雕文，会对周围施放死亡效果
 *
 * Ordered alphabetically using scriptname.
 * Scriptnames of files in this file should be prefixed with "npc_pet_pri_".
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "PassiveAI.h"
#include "PetAI.h"

/**
 * @brief 牧师法术ID枚举
 *
 * 定义牧师宠物相关的法术ID
 */
enum PriestSpells
{
    SPELL_PRIEST_GLYPH_OF_SHADOWFIEND       = 58228,  ///< 暗影恶魔雕文 - 召唤暗影恶魔时施放死亡效果
    SPELL_PRIEST_SHADOWFIEND_DEATH          = 57989,  ///< 暗影恶魔死亡效果 - 对周围敌人造成伤害
    SPELL_PRIEST_LIGHTWELL_CHARGES          = 59907   ///< 光明之泉充能 - 光明之泉的治疗次数
};

/**
 * @brief 光明之泉AI
 *
 * 继承自PassiveAI，实现牧师光明之泉的行为逻辑。
 * 光明之泉是一个被动宠物，不参与战斗，只提供治疗光环。
 *
 * 特殊行为：
 * - 召唤时自动施放光明之泉充能光环
 * - 不会反击或进入战斗
 * - 脱战时不重置任何状态
 */
struct npc_pet_pri_lightwell : public PassiveAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 功能：召唤时立即施放光明之泉充能光环
     */
    npc_pet_pri_lightwell(Creature* creature) : PassiveAI(creature)
    {
        DoCast(me, SPELL_PRIEST_LIGHTWELL_CHARGES, false);
    }

    /**
     * @brief 进入脱战模式
     * @param why 脱战原因
     *
     * 调用时机：失去所有敌对目标或目标变为无效时
     *
     * 功能：
     * 1. 如果死亡，直接返回
     * 2. 停止战斗
     * 3. 结束交战状态
     * 4. 重置玩家伤害需求
     *
     * 性能注意：脱战时一次性调用
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        if (!me->IsAlive())
            return;

        me->CombatStop(true);
        EngagementOver();
        me->ResetPlayerDamageReq();
    }
};

/**
 * @brief 暗影恶魔AI
 *
 * 继承自PetAI，实现牧师暗影恶魔的行为逻辑。
 * 暗影恶魔会攻击目标，并为牧师恢复法力值。
 *
 * 特殊行为：
 * - 召唤时检查牧师是否有暗影恶魔雕文
 * - 如果有雕文，施放死亡效果对周围敌人造成伤害
 * - 使用标准宠物AI进行攻击
 */
struct npc_pet_pri_shadowfiend : public PetAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_pet_pri_shadowfiend(Creature* creature) : PetAI(creature) { }

    /**
     * @brief 被召唤时的回调
     * @param summonerWO 召唤者对象
     *
     * 调用时机：暗影恶魔被召唤时
     *
     * 功能：
     * 1. 获取召唤者（牧师）
     * 2. 如果召唤者有暗影恶魔雕文，施放死亡效果
     *
     * 性能注意：召唤时一次性调用
     */
    void IsSummonedBy(WorldObject* summonerWO) override
    {
        Unit* summoner = summonerWO->ToUnit();
        if (!summoner)
            return;

        // 检查是否有暗影恶魔雕文
        if (summoner->HasAura(SPELL_PRIEST_GLYPH_OF_SHADOWFIEND))
            DoCastAOE(SPELL_PRIEST_SHADOWFIEND_DEATH);
    }
};

/**
 * @brief 注册牧师宠物脚本
 *
 * 调用时机：服务器启动时由脚本加载器调用
 *
 * 功能：注册所有牧师宠物AI到脚本系统
 */
void AddSC_priest_pet_scripts()
{
    RegisterCreatureAI(npc_pet_pri_lightwell);
    RegisterCreatureAI(npc_pet_pri_shadowfiend);
}

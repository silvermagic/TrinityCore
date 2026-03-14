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
 * @file zone_eastern_plaguelands.cpp
 * @brief 东瘟疫之地(Eastern Plaguelands)区域脚本模块
 *
 * 本模块实现了东瘟疫之地区域内的游戏逻辑，包括：
 * - 任务相关法术脚本
 * - 特殊物品测试机制
 *
 * 东瘟疫之地位于东部王国大陆的北部，是天灾军团的核心领地之一，
 * 充满了亡灵生物和被瘟疫腐蚀的土地。
 */

#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Unit.h"

/*######
## Quest 5206: Marauders of Darrowshire
## 任务5206：达隆郡的劫掠者
######*/

/**
 * @brief 达隆郡劫掠者任务法术枚举
 *
 * 定义了测试腐化头骨时可能产生的物品创建法术ID
 */
enum MaraudersOfDarrowshire
{
    SPELL_CREATE_RESONATING_SKULL   = 17269,  ///< 创建共鸣头骨法术 - 测试成功时施放
    SPELL_CREATE_BONE_DUST          = 17270   ///< 创建骨尘法术 - 测试失败时施放
};

/**
 * @brief 测试腐化头骨法术脚本
 *
 * 实现任务"达隆郡的劫掠者"(Quest 5206)中的物品测试机制。
 * 玩家使用腐化头骨时，有50%的几率将其转化为有用的共鸣头骨，
 * 或者测试失败产生骨尘。
 *
 * 该脚本对应法术ID: 17271 - Test Fetid Skull
 */
// 17271 - Test Fetid Skull
class spell_eastern_plaguelands_test_fetid_skull : public SpellScript
{
    PrepareSpellScript(spell_eastern_plaguelands_test_fetid_skull);

    /**
     * @brief 验证法术数据有效性
     *
     * 在法术脚本注册时调用，验证所需的物品创建法术是否存在于数据库中
     *
     * @param spellInfo 法术信息指针（未使用）
     * @return bool 如果所有依赖的法术都存在则返回true，否则返回false
     *
     * @note 此方法在服务器启动时调用，用于提前发现配置错误
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CREATE_RESONATING_SKULL, SPELL_CREATE_BONE_DUST });
    }

    /**
     * @brief 处理虚拟效果
     *
     * 当法术效果命中时调用，执行随机的物品创建逻辑。
     *
     * @param effIndex 效果索引（未使用）
     *
     * 执行流程：
     * 1. 使用roll_chance_i(50)进行50%的随机判定
     * 2. 成功(50%)：施放SPELL_CREATE_RESONATING_SKULL，创建共鸣头骨
     * 3. 失败(50%)：施放SPELL_CREATE_BONE_DUST，创建骨尘
     *
     * @note 使用TRIGGERED_IGNORE_POWER_AND_REAGENT_COST标志避免消耗资源
     * @note 这是一个零和机制，无论成功失败都会产生一个物品
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        // 50%几率创建共鸣头骨，50%几率创建骨尘
        GetCaster()->CastSpell(GetCaster(), roll_chance_i(50) ? SPELL_CREATE_RESONATING_SKULL : SPELL_CREATE_BONE_DUST, TRIGGERED_IGNORE_POWER_AND_REAGENT_COST);
    }

    /**
     * @brief 注册法术效果回调
     *
     * 在脚本初始化时调用，将HandleDummy方法绑定到法术效果上。
     * 当法术的EFFECT_0（第0个效果）触发SPELL_EFFECT_DUMMY类型时，
     * 会调用HandleDummy方法。
     */
    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_eastern_plaguelands_test_fetid_skull::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 注册东瘟疫之地区域脚本
 *
 * 该函数由脚本系统在服务器启动时自动调用，用于注册本文件中定义的所有法术脚本。
 *
 * @note 函数名遵循TrinityCore命名规范：AddSC_<区域名称>
 * @note 该函数会在WorldSession初始化期间被调用，不应手动调用
 */
void AddSC_eastern_plaguelands()
{
    // 注册测试腐化头骨法术脚本
    RegisterSpellScript(spell_eastern_plaguelands_test_fetid_skull);
}

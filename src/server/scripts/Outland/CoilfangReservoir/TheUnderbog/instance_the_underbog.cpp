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
 * @file instance_the_underbog.cpp
 * @brief 幽暗沼泽副本实例脚本实现
 *
 * 模块职责：
 * - 管理幽暗沼泽副本的实例数据和状态
 * - 处理副本中首领击杀事件
 * - 支持随机副本查找器完成奖励机制
 *
 * 副本信息：
 * - 地图ID：546
 * - 名称：幽暗沼泽（The Underbog）
 * - 所在区域：盘牙水库（Coilfang Reservoir）
 * - 首领数量：4个（霍加尔芬、加兹安、黑色阔步者、沼地领主穆塞莱克）
 *
 * 重要说明：
 * 此实例占位符对于副本查找器至关重要。
 * 它允许在lastEncounterDungeon定义的首领被击杀后给予完成奖励。
 * 如果没有此脚本，进行随机副本的小队将无法获得战利品袋，
 * 反而会获得逃兵者debuff。
 */

/*
This placeholder for the instance is needed for dungeon finding to be able
to give credit after the boss defined in lastEncounterDungeon is killed.
Without it, the party doing random dungeon won't get satchel of spoils and
gets instead the deserter debuff.
*/

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Unit.h"
#include "the_underbog.h"

/**
 * @class instance_the_underbog
 * @brief 幽暗沼泽副本实例脚本类
 *
 * 继承自InstanceMapScript，为幽暗沼泽副本提供实例级别的管理功能。
 * 负责创建和管理副本实例脚本对象。
 *
 * 注意：脚本名称使用"TheUndebogScriptName"（存在拼写错误，应为"TheUnderbog"）
 */
class instance_the_underbog : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化副本脚本，设置脚本名称和地图ID（546）。
     */
    instance_the_underbog() : InstanceMapScript(TheUndebogScriptName, 546) { }

    /**
     * @struct instance_the_underbog_InstanceMapScript
     * @brief 幽暗沼泽实例脚本实现
     *
     * 继承自InstanceScript，实现幽暗沼泽副本的具体实例逻辑。
     * 管理副本中的首领状态，特别处理加兹安和沼地领主穆塞莱克的击杀事件。
     *
     * 设计说明：
     * - 霍加尔芬和黑色阔步者由各自的BossAI脚本处理状态
     * - 加兹安和沼地领主穆塞莱克没有BossAI脚本，因此在此处处理状态
     */
    struct instance_the_underbog_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 实例地图指针
         *
         * 初始化实例脚本：
         * - 设置数据头标识
         * - 设置首领数量
         */
        instance_the_underbog_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(TheUndebogDataHeader);
            SetBossNumber(TheUnderbogBossCount);
        }

        /**
         * @brief 单位死亡事件处理函数
         * @param unit 死亡的单位
         *
         * 当副本中有单位死亡时调用。
         * 检查死亡单位的Entry ID，如果是特定首领则更新其状态为完成。
         *
         * 处理的首领：
         * - 加兹安（NPC_GHAZAN）：第二个首领，多头蛇
         * - 沼地领主穆塞莱克（NPC_SWAMPLORD_MUSELEK）：最后一个首领
         *
         * 注意：
         * - 霍加尔芬和黑色阔步者由各自的BossAI脚本通过JustDied处理状态更新
         * - 这两个首领没有专门的BossAI脚本，因此在此处集中处理
         *
         * 调用时机：副本中有单位死亡时
         */
        void OnUnitDeath(Unit* unit) override
        {
            switch (unit->GetEntry())
            {
                case NPC_GHAZAN:
                    // 加兹安死亡，设置首领状态为完成
                    SetBossState(DATA_GHAZAN, DONE);
                    break;
                case NPC_SWAMPLORD_MUSELEK:
                    // 沼地领主穆塞莱克死亡，设置首领状态为完成
                    // 这是最后一个首领，完成后副本状态为已完成
                    SetBossState(DATA_SWAMPLORD_MUSELEK, DONE);
                    break;
                default:
                    break;
            }
        }
    };

    /**
     * @brief 获取实例脚本
     * @param map 实例地图指针
     * @return 新创建的实例脚本对象
     *
     * 创建并返回幽暗沼泽实例脚本的具体实现对象。
     *
     * 调用时机：当副本地图需要获取其实例脚本时
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_the_underbog_InstanceMapScript(map);
    }
};

/**
 * @brief 注册脚本函数
 *
 * 这是脚本的入口点函数，用于将幽暗沼泽实例脚本注册到脚本系统中。
 * 当服务器启动时，脚本系统会调用此函数来注册实例脚本。
 *
 * 调用时机：服务器启动时，在脚本初始化阶段
 */
void AddSC_instance_the_underbog()
{
    new instance_the_underbog();
}

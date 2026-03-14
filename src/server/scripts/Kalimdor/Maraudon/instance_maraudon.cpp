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
 * @file instance_maraudon.cpp
 * @brief 玛拉顿副本实例脚本
 *
 * 该模块实现了玛拉顿副本的实例脚本。
 *
 * 主要功能:
 * - 为随机副本系统提供支持
 * - 确保击败最终BOSS后能正确奖励玩家
 * - 允许完成随机副本的队伍获得战利品袋
 *
 * @note 此实例脚本是一个占位符,主要用于支持地下城查找器系统。
 *       如果没有这个脚本,完成随机副本的队伍将无法获得战利品袋,
 *       反而会获得逃亡者减益效果。
 *
 * @see lastEncounterDungeon 配置定义了哪个BOSS死亡时视为副本完成
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "maraudon.h"

/**
 * @brief 玛拉顿副本实例脚本类
 *
 * 继承自 InstanceMapScript,负责管理玛拉顿副本实例
 *
 * 玛拉顿的地图ID为349
 */
class instance_maraudon : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称和地图ID(349 = 玛拉顿)
     */
    instance_maraudon() : InstanceMapScript(MaraudonScriptName, 349) { }

    /**
     * @brief 获取实例脚本
     * @param map 实例地图对象
     * @return 返回玛拉顿实例脚本实例
     *
     * 为指定的实例地图创建实例脚本对象
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_maraudon_InstanceMapScript(map);
    }

    /**
     * @brief 玛拉顿实例脚本实现类
     *
     * 继承自 InstanceScript,管理副本内的状态和数据
     *
     * 当前实现:
     * - 仅提供基本实例脚本框架
     * - 支持随机副本奖励系统
     *
     * @note 如需添加更多实例功能,可以在此类中扩展:
     *       - BOSS状态保存
     * - 门/机关控制
     * - 特殊事件触发
     */
    struct instance_maraudon_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 实例地图对象
         */
        instance_maraudon_InstanceMapScript(InstanceMap* map) : InstanceScript(map) { }
    };
};

/**
 * @brief 注册玛拉顿实例脚本
 *
 * 该函数由脚本加载器调用,用于将实例脚本注册到系统中
 */
void AddSC_instance_maraudon()
{
    new instance_maraudon();
}

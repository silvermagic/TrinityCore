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
 * @file instance_ragefire_chasm.cpp
 * @brief 怒焰裂谷副本实例脚本
 *
 * 本模块实现了怒焰裂谷副本的实例数据管理。
 *
 * 主要功能：
 * - 为副本查找器系统提供实例支持
 * - 在最终BOSS被击杀后给予玩家副本完成奖励
 * - 确保随机副本队列的玩家能够获得奖励包而非逃亡者减益效果
 *
 * 注意事项：
 * 怒焰裂谷是一个低级别副本，没有复杂的实例机制。
 * 此脚本主要是占位符，用于支持副本查找器功能。
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"

/**
 * @class instance_ragefire_chasm
 * @brief 怒焰裂谷副本脚本类
 *
 * 继承自 InstanceMapScript，负责创建和管理怒焰裂谷的实例脚本
 */
class instance_ragefire_chasm : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化副本脚本，地图ID为389（怒焰裂谷）
     */
    instance_ragefire_chasm() : InstanceMapScript("instance_ragefire_chasm", 389) { }

    /**
     * @brief 获取实例脚本
     * @param map 副本地图指针
     * @return 新创建的实例脚本对象
     *
     * @调用时机 当副本地图创建时由核心调用
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_ragefire_chasm_InstanceMapScript(map);
    }

    /**
     * @class instance_ragefire_chasm_InstanceMapScript
     * @brief 怒焰裂谷实例脚本实现类
     *
     * 继承自 InstanceScript，实现怒焰裂谷的实例数据管理
     * 由于怒焰裂谷没有特殊的实例机制，此类为空实现
     */
    struct instance_ragefire_chasm_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         */
        instance_ragefire_chasm_InstanceMapScript(InstanceMap* map) : InstanceScript(map) { }
    };
};

/**
 * @brief 注册脚本
 *
 * 将怒焰裂谷实例脚本注册到脚本系统
 */
void AddSC_instance_ragefire_chasm()
{
    new instance_ragefire_chasm();
}

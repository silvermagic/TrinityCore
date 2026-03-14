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
 * @file instance_the_slave_pens.cpp
 * @brief 奴隶围栏副本实例脚本实现
 *
 * 模块职责：
 * - 管理奴隶围栏副本的实例数据和状态
 * - 处理副本中首领和NPC的创建和数据绑定
 * - 支持随机副本查找器完成奖励机制
 *
 * 副本信息：
 * - 地图ID：547
 * - 名称：奴隶围栏（The Slave Pens）
 * - 所在区域：盘牙水库（Coilfang Reservoir）
 * - 首领数量：4个（包括节日首领埃霍恩）
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
#include "Creature.h"
#include "InstanceScript.h"
#include "the_slave_pens.h"

/**
 * @brief 生物数据映射表
 *
 * 将NPC的Entry ID映射到数据ID，用于实例脚本快速查找和管理生物对象。
 * 表格以{0, 0}结尾表示数组结束。
 *
 * 包含的生物：
 * - 埃霍恩及其核心（节日首领）
 * - 各种辅助生物（位置和光束辅助单位）
 * - 天空之母露玛（节日事件NPC）
 */
ObjectData const creatureData[] =
{
    { NPC_AHUNE,                    DATA_AHUNE             },  ///< 埃霍恩 - 节日火焰首领
    { NPC_FROZEN_CORE,              DATA_FROZEN_CORE       },  ///< 冰冻核心 - 埃霍恩的弱点阶段
    { NPC_AHUNE_LOC_BUNNY,          DATA_AHUNE_BUNNY       },  ///< 埃霍恩位置辅助单位
    { NPC_SHAMAN_BONFIRE_BUNNY_000, DATA_BONFIRE_BUNNY_000 },  ///< 萨满篝火辅助单位0
    { NPC_SHAMAN_BONFIRE_BUNNY_001, DATA_BONFIRE_BUNNY_001 },  ///< 萨满篝火辅助单位1
    { NPC_SHAMAN_BONFIRE_BUNNY_002, DATA_BONFIRE_BUNNY_002 },  ///< 萨满篝火辅助单位2
    { NPC_SHAMAN_BEAM_BUNNY_000,    DATA_BEAM_BUNNY_000    },  ///< 萨满光束辅助单位0
    { NPC_SHAMAN_BEAM_BUNNY_001,    DATA_BEAM_BUNNY_001    },  ///< 萨满光束辅助单位1
    { NPC_SHAMAN_BEAM_BUNNY_002,    DATA_BEAM_BUNNY_002    },  ///< 萨满光束辅助单位2
    { NPC_LUMA_SKYMOTHER,           DATA_LUMA_SKYMOTHER    },  ///< 天空之母露玛 - 节日事件NPC
    { 0,                            0,                     }   ///< 数组结束标记
};

/**
 * @class instance_the_slave_pens
 * @brief 奴隶围栏副本实例脚本类
 *
 * 继承自InstanceMapScript，为奴隶围栏副本提供实例级别的管理功能。
 * 负责创建和管理副本实例脚本对象。
 */
class instance_the_slave_pens : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化副本脚本，设置脚本名称和地图ID（547）。
     */
    instance_the_slave_pens() : InstanceMapScript(SPScriptName, 547) { }

    /**
     * @struct instance_the_slave_pens_InstanceMapScript
     * @brief 奴隶围栏实例脚本实现
     *
     * 继承自InstanceScript，实现奴隶围栏副本的具体实例逻辑。
     * 管理副本中的首领状态、生物GUID和副本数据。
     */
    struct instance_the_slave_pens_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 实例地图指针
         *
         * 初始化实例脚本：
         * - 设置计数器初始值为第一个火焰召唤者的数据ID
         * - 加载生物数据映射表
         * - 设置首领数量
         */
        instance_the_slave_pens_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            counter = DATA_FLAMECALLER_000;
            LoadObjectData(creatureData, nullptr);
            SetBossNumber(EncounterCount);
        }

        /**
         * @brief 生物创建事件处理函数
         * @param creature 新创建的生物对象
         *
         * 当副本中有生物被创建时调用。
         * 首先调用基类的OnCreatureCreate处理标准逻辑（如生物数据绑定）。
         * 然后特别处理大地之环火焰召唤者，按创建顺序保存其GUID。
         *
         * 处理逻辑：
         * - 检查生物是否为大地之环火焰召唤者（NPC_EARTHEN_RING_FLAMECALLER）
         * - 根据计数器值将GUID保存到对应数组位置
         * - 递增计数器以处理下一个火焰召唤者
         *
         * 调用时机：副本中有生物被创建时（包括玩家进入副本时加载的生物）
         */
        void OnCreatureCreate(Creature* creature) override
        {
            // 调用基类方法，处理标准生物数据绑定
            InstanceScript::OnCreatureCreate(creature);

            // 特殊处理大地之环火焰召唤者
            if (creature->GetEntry() == NPC_EARTHEN_RING_FLAMECALLER)
            {
                switch (counter)
                {
                    case DATA_FLAMECALLER_000:
                        FlameCallerGUIDs[0] = creature->GetGUID();  // 保存第一个火焰召唤者GUID
                        break;
                    case DATA_FLAMECALLER_001:
                        FlameCallerGUIDs[1] = creature->GetGUID();  // 保存第二个火焰召唤者GUID
                        break;
                    case DATA_FLAMECALLER_002:
                        FlameCallerGUIDs[2] = creature->GetGUID();  // 保存第三个火焰召唤者GUID
                        break;
                    default:
                        break;
                }
                ++counter;  // 递增计数器
            }
        }

        /**
         * @brief 获取GUID数据
         * @param type 数据类型ID
         * @return 对应类型生物的GUID，如果不存在则返回空GUID
         *
         * 根据数据类型ID返回对应火焰召唤者的GUID。
         * 用于其他脚本获取特定火焰召唤者的引用。
         *
         * 调用时机：其他脚本需要获取特定生物GUID时
         *
         * @see ObjectGuid::Empty
         */
        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_FLAMECALLER_000:
                    return FlameCallerGUIDs[0];  // 返回第一个火焰召唤者GUID
                case DATA_FLAMECALLER_001:
                    return FlameCallerGUIDs[1];  // 返回第二个火焰召唤者GUID
                case DATA_FLAMECALLER_002:
                    return FlameCallerGUIDs[2];  // 返回第三个火焰召唤者GUID
                default:
                    break;
            }
            return ObjectGuid::Empty;  // 未找到则返回空GUID
        }

    protected:
        ObjectGuid FlameCallerGUIDs[3];  ///< 三个大地之环火焰召唤者的GUID数组
        uint8 counter;                   ///< 火焰召唤者计数器，用于按顺序保存GUID
    };

    /**
     * @brief 获取实例脚本
     * @param map 实例地图指针
     * @return 新创建的实例脚本对象
     *
     * 创建并返回奴隶围栏实例脚本的具体实现对象。
     *
     * 调用时机：当副本地图需要获取其实例脚本时
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_the_slave_pens_InstanceMapScript(map);
    }
};

/**
 * @brief 注册脚本函数
 *
 * 这是脚本的入口点函数，用于将奴隶围栏实例脚本注册到脚本系统中。
 * 当服务器启动时，脚本系统会调用此函数来注册实例脚本。
 *
 * 调用时机：服务器启动时，在脚本初始化阶段
 */
void AddSC_instance_the_slave_pens()
{
    new instance_the_slave_pens();
}

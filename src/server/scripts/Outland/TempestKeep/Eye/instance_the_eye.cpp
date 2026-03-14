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
 * @file instance_the_eye.cpp
 * @brief 风暴要塞-风暴之眼副本实例脚本实现
 *
 * 本模块实现了风暴之眼副本的实例管理脚本，包括:
 * - Boss状态管理
 * - 游戏对象(门、雕像等)状态控制
 * - 生物数据缓存
 *
 * 风暴之眼包含4个Boss:
 * 0 - 奥尔(Al'ar)
 * 1 - 虚空掠夺者(Void Reaver)
 * 2 - 星术师索拉瑞恩(Solarian)
 * 3 - 凯尔萨斯·逐日者(Kael'thas)
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "the_eye.h"

/* The Eye encounters:
0 - Al'ar
1 - Void Reaver
2 - Solarian
3 - Kael'thas
*/

/**
 * @brief 门数据数组
 *
 * 定义凯尔萨斯战斗区域的门控制
 * 当凯尔萨斯战斗开始时关闭门，战斗结束时打开
 */
DoorData const doorData[] =
{
    { GO_ARCANE_DOOR_LEFT,  DATA_KAELTHAS, DOOR_TYPE_ROOM/*, BOUNDARY_SW  */ }, ///< 左侧奥术门
    { GO_ARCANE_DOOR_RIGHT, DATA_KAELTHAS, DOOR_TYPE_ROOM/*, BOUNDARY_SE  */ }, ///< 右侧奥术门
    {                    0,             0, DOOR_TYPE_ROOM } // END
};

/**
 * @brief 生物数据数组
 *
 * 定义所有Boss和顾问的生物ID与数据ID映射关系
 * 用于在实例中快速查找和缓存生物对象
 */
ObjectData const creatureData[] =
{
    { NPC_ALAR,        DATA_ALAR        },        ///< 奥尔
    { NPC_VOID_REAVER, DATA_VOID_REAVER },        ///< 虚空掠夺者
    { NPC_SOLARIAN,    DATA_SOLARIAN    },        ///< 索拉瑞恩
    { NPC_KAELTHAS,    DATA_KAELTHAS    },        ///< 凯尔萨斯
    { NPC_CAPERNIAN,   DATA_CAPERNIAN   },        ///< 顾问卡珀尼安
    { NPC_SANGUINAR,   DATA_SANGUINAR   },        ///< 顾问萨拉雷恩
    { NPC_TELONICUS,   DATA_TELONICUS   },        ///< 顾问泰隆尼库斯
    { NPC_THALADRED,   DATA_THALADRED   },        ///< 顾问塔隆血魔
    { 0,               0                } // END
};

/**
 * @brief 游戏对象数据数组
 *
 * 定义凯尔萨斯战斗中涉及的装饰物对象
 * 在阶段转换时会被激活/摧毁
 */
ObjectData const gameObjectData[] =
{
    { GO_KAEL_STATUE_RIGHT,      DATA_KAEL_STATUE_RIGHT     },     ///< 凯尔萨斯右侧雕像
    { GO_KAEL_STATUE_LEFT,       DATA_KAEL_STATUE_LEFT      },     ///< 凯尔萨斯左侧雕像
    { GO_TEMPEST_BRIDDGE_WINDOW, DATA_TEMPEST_BRIDGE_WINDOW },     ///< 风暴之桥窗户
    {                         0, 0                          } // END
};

/**
 * @class instance_the_eye
 * @brief 风暴之眼副本实例脚本
 *
 * 继承自InstanceMapScript，管理风暴之眼副本的整体状态。
 * 地图ID: 550
 */
class instance_the_eye : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册风暴之眼副本脚本，地图ID为550
         */
        instance_the_eye() : InstanceMapScript(TheEyeScriptName, 550) { }

        /**
         * @struct instance_the_eye_InstanceMapScript
         * @brief 风暴之眼副本实例脚本实现
         *
         * 继承自InstanceScript，实现副本的具体管理逻辑
         */
        struct instance_the_eye_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图指针
             *
             * 初始化副本数据头、Boss数量、门数据和对象数据
             */
            instance_the_eye_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);           // 设置数据头，用于存档识别
                SetBossNumber(EncounterCount);    // 设置Boss数量(4个)
                LoadDoorData(doorData);           // 加载门控制数据
                LoadObjectData(creatureData, gameObjectData);  // 加载生物和游戏对象数据
            }
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图指针
         * @return 新创建的实例脚本对象
         *
         * 当副本被创建时调用，返回风暴之眼的实例脚本实例
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_the_eye_InstanceMapScript(map);
        }
};

/**
 * @brief 注册风暴之眼副本实例脚本
 *
 * 此函数由脚本系统在服务器启动时调用，注册风暴之眼副本实例脚本
 */
void AddSC_instance_the_eye()
{
    new instance_the_eye;
}

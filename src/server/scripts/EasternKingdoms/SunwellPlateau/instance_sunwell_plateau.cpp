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
 * @file instance_sunwell_plateau.cpp
 * @brief 太阳之井高地副本实例脚本模块
 *
 * 本模块实现了太阳之井高地副本的实例管理功能，包括：
 * - 副本内Boss状态管理
 * - 门和障碍物的开关控制
 * - Boss战斗边界检测
 * - 关键NPC的对象数据管理
 *
 * 太阳之井高地包含以下Boss：
 * 0 - Kalecgos 和 Sathrovarr（卡雷苟斯和萨索瓦尔）
 * 1 - Brutallus（布鲁塔卢斯）
 * 2 - Felmyst（菲米丝）
 * 3 - Eredar Twins（艾瑞达双子）
 * 4 - M'uru（穆鲁）
 * 5 - Kil'Jaeden（基尔加丹）
 */

#include "ScriptMgr.h"
#include "AreaBoundary.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "sunwell_plateau.h"

/* Sunwell Plateau:
0 - Kalecgos and Sathrovarr
1 - Brutallus
2 - Felmyst
3 - Eredar Twins (Alythess and Sacrolash)
4 - M'uru
5 - Kil'Jaeden
*/

/**
 * @brief 门数据配置表
 *
 * 定义副本中各个门与Boss状态的关联关系
 * 当Boss状态改变时，相关的门会自动开启或关闭
 */
DoorData const doorData[] =
{
    { GO_FIRE_BARRIER,     DATA_FELMYST,  DOOR_TYPE_PASSAGE },  // 火焰屏障 - 菲米丝通过通道
    { GO_MURUS_GATE_1,     DATA_MURU,     DOOR_TYPE_ROOM },     // 穆鲁之门1 - 穆鲁房间门
    { GO_MURUS_GATE_2,     DATA_MURU,     DOOR_TYPE_PASSAGE },  // 穆鲁之门2 - 穆鲁通道门
    { GO_BOSS_COLLISION_1, DATA_KALECGOS, DOOR_TYPE_ROOM },     // Boss碰撞墙1 - 卡雷苟斯房间
    { GO_BOSS_COLLISION_2, DATA_KALECGOS, DOOR_TYPE_ROOM },     // Boss碰撞墙2 - 卡雷苟斯房间
    { GO_FORCE_FIELD,      DATA_KALECGOS, DOOR_TYPE_ROOM },     // 力场 - 卡雷苟斯房间
    { 0,                   0,             DOOR_TYPE_ROOM }      // 结束标记
};

/**
 * @brief 生物对象数据配置表
 *
 * 定义副本中关键NPC与其数据标识的对应关系
 * 用于快速查找和引用副本中的关键NPC
 */
ObjectData const creatureData[] =
{
    { NPC_KALECGOS,               DATA_KALECGOS_DRAGON      },  // 卡雷苟斯（龙形态）
    { NPC_KALECGOS_HUMAN,         DATA_KALECGOS_HUMAN       },  // 卡雷苟斯（人形态）
    { NPC_SATHROVARR,             DATA_SATHROVARR           },  // 萨索瓦尔
    { NPC_BRUTALLUS,              DATA_BRUTALLUS            },  // 布鲁塔卢斯
    { NPC_MADRIGOSA,              DATA_MADRIGOSA            },  // 玛德里戈萨
    { NPC_FELMYST,                DATA_FELMYST              },  // 菲米丝
    { NPC_GRAND_WARLOCK_ALYTHESS, DATA_ALYTHESS             },  // 大术士奥蕾瑟尔
    { NPC_LADY_SACROLASH,         DATA_SACROLASH            },  // 萨克拉什女士
    { NPC_MURU,                   DATA_MURU                 },  // 穆鲁
    { NPC_KILJAEDEN,              DATA_KILJAEDEN            },  // 基尔加丹
    { NPC_KILJAEDEN_CONTROLLER,   DATA_KILJAEDEN_CONTROLLER },  // 基尔加丹控制器
    { NPC_ANVEENA,                DATA_ANVEENA              },  // 安薇娜
    { NPC_KALECGOS_KJ,            DATA_KALECGOS_KJ          },  // 卡雷苟斯（基尔加丹战）
    { 0,                          0                         }   // 结束标记
};

/**
 * @brief Boss战斗边界数据
 *
 * 定义各个Boss战斗区域的边界，用于检测玩家是否脱离战斗区域
 * 当前仅配置了卡雷苟斯的战斗边界
 */
BossBoundaryData const boundaries =
{
    // 卡雷苟斯战斗区域：圆形边界与矩形边界的并集
    { DATA_KALECGOS, new BoundaryUnionBoundary(new CircleBoundary(Position(1704.9f, 928.4f), 34.0), new RectangleBoundary(1689.2f, 1713.3f, 762.2f, 1074.8f)) }
};

/**
 * @brief 太阳之井高地副本实例脚本类
 *
 * 管理整个副本的实例数据、Boss状态、门控制和边界检测
 */
class instance_sunwell_plateau : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化太阳之井高地副本脚本，地图ID为580
         */
        instance_sunwell_plateau() : InstanceMapScript(SunwellPlateauScriptName, 580) { }

        /**
         * @brief 太阳之井高地实例地图脚本实现类
         *
         * 继承自InstanceScript，实现副本的具体管理逻辑
         */
        struct instance_sunwell_plateau_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图指针
             *
             * 初始化实例脚本，加载门数据、对象数据和Boss边界
             */
            instance_sunwell_plateau_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);               // 设置数据头标识
                SetBossNumber(EncounterCount);        // 设置Boss数量
                LoadDoorData(doorData);               // 加载门数据
                LoadObjectData(creatureData, nullptr); // 加载对象数据
                LoadBossBoundaries(boundaries);       // 加载Boss边界
            }

            /**
             * @brief 获取副本中的有效玩家
             * @return 返回副本中第一个有效玩家的指针，如果没有则返回nullptr
             *
             * 遍历副本中的所有玩家，返回第一个没有特定光环效果的玩家
             * 排除拥有光环45839（复仇之蓝龙）的玩家
             *
             * @note 此函数用于确定某些机制的目标玩家
             */
            Player const* GetPlayerInMap() const
            {
                Map::PlayerList const& players = instance->GetPlayers();

                if (!players.isEmpty())
                {
                    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
                    {
                        Player* player = itr->GetSource();
                        // 排除拥有复仇之蓝龙光环的玩家（该玩家正在控制蓝龙）
                        if (player && !player->HasAura(45839))
                            return player;
                    }
                }
                else
                    TC_LOG_DEBUG("scripts", "Instance Sunwell Plateau: GetPlayerInMap, but PlayerList is empty!");

                return nullptr;
            }

            /**
             * @brief 根据数据ID获取GUID
             * @param id 数据标识符
             * @return 返回对应的对象GUID，如果不存在则返回空GUID
             *
             * 根据请求的数据类型返回相应的GUID
             * 目前支持DATA_PLAYER_GUID类型，返回副本中有效玩家的GUID
             *
             * @note 其他类型的请求会委托给基类处理
             */
            ObjectGuid GetGuidData(uint32 id) const override
            {
                switch (id)
                {
                    case DATA_PLAYER_GUID:
                    {
                        Player const* target = GetPlayerInMap();
                        return target ? target->GetGUID() : ObjectGuid::Empty;
                    }
                    default:
                        break;
                }
                return ObjectGuid::Empty;
            }
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图指针
         * @return 返回新创建的实例脚本对象
         *
         * 工厂方法，创建并返回太阳之井高地实例脚本实例
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_sunwell_plateau_InstanceMapScript(map);
        }
};

/**
 * @brief 注册太阳之井高地副本脚本
 *
 * 将太阳之井高地副本脚本注册到脚本系统中
 * 此函数在服务器启动时被调用
 */
void AddSC_instance_sunwell_plateau()
{
    new instance_sunwell_plateau();
}

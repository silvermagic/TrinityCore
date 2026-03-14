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
 * @file instance_black_temple.cpp
 * @brief 黑暗神殿副本实例脚本
 *
 * 本模块实现了黑暗神殿副本的实例管理功能，包括：
 * - 9个Boss的状态管理和门控制
 * - Boss战边界区域限制
 * - 阿卡玛事件状态管理
 * - 灰舌NPC阵营转换（阿卡玛之影死亡后）
 * - 死亡之门开启条件检测（需击杀4个特定Boss）
 */

#include "ScriptMgr.h"
#include "AreaBoundary.h"
#include "black_temple.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"

/**
 * @brief 门数据配置表
 *
 * 定义副本中各个门与Boss状态的关联关系
 * - DOOR_TYPE_PASSAGE: 通道类型门，Boss死亡后自动打开
 * - DOOR_TYPE_ROOM: 房间类型门，战斗期间关闭
 */
DoorData const doorData[] =
{
    { GO_NAJENTUS_GATE,         DATA_HIGH_WARLORD_NAJENTUS, DOOR_TYPE_PASSAGE },  // 高阶督军纳因图斯门
    { GO_NAJENTUS_GATE,         DATA_SUPREMUS,              DOOR_TYPE_ROOM    },  // 苏普雷姆斯门
    { GO_SUPREMUS_GATE,         DATA_SUPREMUS,              DOOR_TYPE_PASSAGE },  // 苏普雷姆斯通道门
    { GO_SHADE_OF_AKAMA_DOOR,   DATA_SHADE_OF_AKAMA,        DOOR_TYPE_ROOM    },  // 阿卡玛之影门
    { GO_TERON_DOOR_1,          DATA_TERON_GOREFIEND,       DOOR_TYPE_ROOM    },  // 泰隆·血魔门1
    { GO_TERON_DOOR_2,          DATA_TERON_GOREFIEND,       DOOR_TYPE_ROOM    },  // 泰隆·血魔门2
    { GO_GURTOGG_DOOR,          DATA_GURTOGG_BLOODBOIL,     DOOR_TYPE_PASSAGE },  // 古尔图格·血沸门
    { GO_MOTHER_SHAHRAZ_DOOR,   DATA_MOTHER_SHAHRAZ,        DOOR_TYPE_PASSAGE },  // 莎赫拉丝主母门
    { GO_COUNCIL_DOOR_1,        DATA_ILLIDARI_COUNCIL,      DOOR_TYPE_ROOM    },  // 伊利达雷议会门1
    { GO_COUNCIL_DOOR_2,        DATA_ILLIDARI_COUNCIL,      DOOR_TYPE_ROOM    },  // 伊利达雷议会门2
    { GO_ILLIDAN_DOOR_R,        DATA_ILLIDAN_STORMRAGE,     DOOR_TYPE_ROOM    },  // 伊利丹右门
    { GO_ILLIDAN_DOOR_L,        DATA_ILLIDAN_STORMRAGE,     DOOR_TYPE_ROOM    },  // 伊利丹左门
    { 0,                        0,                          DOOR_TYPE_ROOM    }   // 结束标记
};

/**
 * @brief Boss战边界区域配置
 *
 * 定义各个Boss战的战斗区域边界，玩家超出边界会触发Boss脱战
 * 使用不同类型的边界：
 * - RectangleBoundary: 矩形边界
 * - ZRangeBoundary: 高度范围边界
 * - EllipseBoundary: 椭圆形边界
 */
BossBoundaryData const boundaries =
{
    { DATA_HIGH_WARLORD_NAJENTUS, new RectangleBoundary(394.0f, 479.4f, 707.8f, 859.1f)      },  // 高阶督军纳因图斯
    { DATA_SUPREMUS,              new RectangleBoundary(556.1f, 850.2f, 542.0f, 1001.0f)     },  // 苏普雷姆斯
    { DATA_SHADE_OF_AKAMA,        new RectangleBoundary(406.8f, 564.0f, 327.9f, 473.5f)      },  // 阿卡玛之影
    { DATA_TERON_GOREFIEND,       new RectangleBoundary(512.5f, 613.3f, 373.2f, 432.0f)      },  // 泰隆·血魔（平面）
    { DATA_TERON_GOREFIEND,       new ZRangeBoundary(179.5f, 223.6f)                         },  // 泰隆·血魔（高度）
    { DATA_GURTOGG_BLOODBOIL,     new RectangleBoundary(720.5f, 864.5f, 159.3f, 316.0f)      },  // 古尔图格·血沸
    { DATA_RELIQUARY_OF_SOULS,    new RectangleBoundary(435.9f, 660.3f, 21.2f, 229.6f)       },  // 灵魂之匣（平面）
    { DATA_RELIQUARY_OF_SOULS,    new ZRangeBoundary(81.8f, 148.0f)                          },  // 灵魂之匣（高度）
    { DATA_MOTHER_SHAHRAZ,        new RectangleBoundary(903.4f, 982.1f, 92.4f, 313.2f)       },  // 莎赫拉丝主母
    { DATA_ILLIDARI_COUNCIL,      new EllipseBoundary(Position(696.6f, 305.0f), 70.0 , 85.0) },  // 伊利达雷议会
    { DATA_ILLIDAN_STORMRAGE,     new EllipseBoundary(Position(694.8f, 309.0f), 80.0 , 95.0) }   // 伊利丹·怒风
};

/**
 * @brief 生物数据配置表
 *
 * 定义副本中关键生物与数据ID的映射关系
 * 用于存储和查询特定生物的GUID
 */
ObjectData const creatureData[] =
{
    { NPC_HIGH_WARLORD_NAJENTUS,        DATA_HIGH_WARLORD_NAJENTUS      },  // 高阶督军纳因图斯
    { NPC_SUPREMUS,                     DATA_SUPREMUS                   },  // 苏普雷姆斯
    { NPC_SHADE_OF_AKAMA,               DATA_SHADE_OF_AKAMA             },  // 阿卡玛之影
    { NPC_TERON_GOREFIEND,              DATA_TERON_GOREFIEND            },  // 泰隆·血魔
    { NPC_GURTOGG_BLOODBOIL,            DATA_GURTOGG_BLOODBOIL          },  // 古尔图格·血沸
    { NPC_RELIQUARY_OF_SOULS,           DATA_RELIQUARY_OF_SOULS         },  // 灵魂之匣
    { NPC_MOTHER_SHAHRAZ,               DATA_MOTHER_SHAHRAZ             },  // 莎赫拉丝主母
    { NPC_ILLIDARI_COUNCIL,             DATA_ILLIDARI_COUNCIL           },  // 伊利达雷议会
    { NPC_ILLIDAN_STORMRAGE,            DATA_ILLIDAN_STORMRAGE          },  // 伊利丹·怒风
    { NPC_AKAMA_SHADE,                  DATA_AKAMA_SHADE                },  // 阿卡玛（阿卡玛之影战）
    { NPC_AKAMA,                        DATA_AKAMA                      },  // 阿卡玛（伊利丹战）
    { NPC_GATHIOS_THE_SHATTERER,        DATA_GATHIOS_THE_SHATTERER      },  // 碎裂者加西奥斯（议会）
    { NPC_HIGH_NETHERMANCER_ZEREVOR,    DATA_HIGH_NETHERMANCER_ZEREVOR  },  // 高阶虚空法师泽雷沃（议会）
    { NPC_LADY_MALANDE,                 DATA_LADY_MALANDE               },  // 玛兰德女士（议会）
    { NPC_VERAS_DARKSHADOW,             DATA_VERAS_DARKSHADOW           },  // 维拉斯·暗影（议会）
    { NPC_BLOOD_ELF_COUNCIL_VOICE,      DATA_BLOOD_ELF_COUNCIL_VOICE    },  // 血精灵议会之音
    { NPC_BLACK_TEMPLE_TRIGGER,         DATA_BLACK_TEMPLE_TRIGGER       },  // 黑暗神殿触发器
    { NPC_MAIEV_SHADOWSONG,             DATA_MAIEV                      },  // 玛维·影歌
    { NPC_RELIQUARY_COMBAT_TRIGGER,     DATA_RELIQUARY_COMBAT_TRIGGER   },  // 灵魂之匣战斗触发器
    { 0,                                0                               }   // 结束标记
};

/**
 * @brief 游戏对象数据配置表
 *
 * 定义副本中关键游戏对象与数据ID的映射关系
 */
ObjectData const gameObjectData[] =
{
    { GO_ILLIDAN_GATE,                DATA_GO_ILLIDAN_GATE          },  // 伊利丹之门
    { GO_DEN_OF_MORTAL_DOOR,          DATA_GO_DEN_OF_MORTAL_DOOR    },  // 死亡之门
    { GO_ILLIDAN_MUSIC_CONTROLLER,    DATA_ILLIDAN_MUSIC_CONTROLLER },  // 伊利丹音乐控制器
    { 0,                              0                             }   // 结束标记
};

/**
 * @class instance_black_temple
 * @brief 黑暗神殿副本脚本类
 *
 * 继承自InstanceMapScript，负责注册和管理整个副本实例
 */
class instance_black_temple : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册黑暗神殿副本脚本，地图ID为564
         */
        instance_black_temple() : InstanceMapScript(BTScriptName, 564) { }

        /**
         * @struct instance_black_temple_InstanceMapScript
         * @brief 副本实例的核心逻辑实现
         *
         * 继承自InstanceScript，管理副本中的Boss状态、门控制、阿卡玛事件等
         */
        struct instance_black_temple_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图指针
             *
             * 初始化副本实例：
             * - 设置数据头标识
             * - 设置Boss数量
             * - 加载门数据、对象数据和Boss边界
             * - 初始化阿卡玛状态
             */
            instance_black_temple_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
                LoadDoorData(doorData);
                LoadObjectData(creatureData, gameObjectData);
                LoadBossBoundaries(boundaries);
                AkamaState = AKAMA_INTRO;        // 阿卡玛初始状态
                AkamaIllidanIntro = 1;           // 伊利丹战阿卡玛介绍状态
            }

            /**
             * @brief 游戏对象创建回调
             * @param go 新创建的游戏对象指针
             *
             * 当副本中有游戏对象创建时调用
             * 特殊处理死亡之门：如果已满足开启条件，直接打开
             *
             * 调用时机：游戏对象在副本中生成时
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                InstanceScript::OnGameObjectCreate(go);

                // 检查死亡之门是否应该打开
                if (go->GetEntry() == GO_DEN_OF_MORTAL_DOOR)
                    if (CheckDenOfMortalDoor())
                        HandleGameObject(ObjectGuid::Empty, true, go);
            }

            /**
             * @brief 生物创建回调
             * @param creature 新创建的生物指针
             *
             * 当副本中有生物创建时调用，处理：
             * - 保存灰舌NPC的GUID
             * - 如果阿卡玛之影已死，将灰舌NPC转为友好阵营
             *
             * 调用时机：生物在副本中生成时
             */
            void OnCreatureCreate(Creature* creature) override
            {
                InstanceScript::OnCreatureCreate(creature);

                switch (creature->GetEntry())
                {
                    case NPC_ASHTONGUE_STALKER:         // 灰舌潜行者
                    case NPC_ASHTONGUE_BATTLELORD:      // 灰舌战斗领主
                    case NPC_ASHTONGUE_MYSTIC:          // 灰舌秘术师
                    case NPC_ASHTONGUE_PRIMALIST:       // 灰舌原始主义者
                    case NPC_ASHTONGUE_STORMCALLER:     // 灰舌唤雷者
                    case NPC_ASHTONGUE_FERAL_SPIRIT:    // 灰舌野性之魂
                    case NPC_STORM_FURY:                // 风暴之怒
                        AshtongueGUIDs.push_back(creature->GetGUID());
                        // 如果阿卡玛之影已被击杀，灰舌转为友好阵营
                        if (GetBossState(DATA_SHADE_OF_AKAMA) == DONE)
                            creature->SetFaction(FACTION_ASHTONGUE_DEATHSWORN);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 获取数据
             * @param type 数据类型标识
             * @return 对应的数据值
             *
             * 查询副本实例数据，主要用于阿卡玛相关状态
             */
            uint32 GetData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_AKAMA:
                        return AkamaState;                    // 阿卡玛事件状态
                    case DATA_AKAMA_ILLIDAN_INTRO:
                        return AkamaIllidanIntro;             // 伊利丹战阿卡玛介绍状态
                    default:
                        return 0;
                }
            }

            /**
             * @brief 设置数据
             * @param type 数据类型标识
             * @param data 数据值
             *
             * 设置副本实例数据，处理：
             * - 阿卡玛状态更新
             * - 打开伊利丹之门的动作
             * - 伊利丹战阿卡玛介绍状态
             */
            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case DATA_AKAMA:
                        AkamaState = data;
                        break;
                    case ACTION_OPEN_DOOR:
                        // 打开伊利丹之门
                        if (GameObject* illidanGate = GetGameObject(DATA_GO_ILLIDAN_GATE))
                            HandleGameObject(ObjectGuid::Empty, true, illidanGate);
                        break;
                    case DATA_AKAMA_ILLIDAN_INTRO:
                        AkamaIllidanIntro = data;
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 设置Boss状态
             * @param type Boss类型标识
             * @param state 战斗状态
             * @return 是否设置成功
             *
             * 当Boss状态改变时调用，处理：
             * - 纳因图斯死亡：触发台词
             * - 阿卡玛之影死亡：转换灰舌阵营，检查死亡之门
             * - 泰隆/古尔图格/灵魂之匣死亡：检查死亡之门
             * - 伊利达雷议会死亡：激活阿卡玛
             */
            bool SetBossState(uint32 type, EncounterState state) override
            {
                if (!InstanceScript::SetBossState(type, state))
                    return false;

                switch (type)
                {
                    case DATA_HIGH_WARLORD_NAJENTUS:
                        // 纳因图斯死亡时播放台词
                        if (state == DONE)
                            if (Creature* trigger = GetCreature(DATA_BLACK_TEMPLE_TRIGGER))
                                trigger->AI()->Talk(EMOTE_HIGH_WARLORD_NAJENTUS_DIED);
                        break;
                    case DATA_SHADE_OF_AKAMA:
                        // 阿卡玛之影死亡时，所有灰舌转为友好阵营
                        if (state == DONE)
                            for (ObjectGuid ashtongueGuid : AshtongueGUIDs)
                                if (Creature* ashtongue = instance->GetCreature(ashtongueGuid))
                                    ashtongue->SetFaction(FACTION_ASHTONGUE_DEATHSWORN);
                        [[fallthrough]];  // 继续检查死亡之门
                    case DATA_TERON_GOREFIEND:
                    case DATA_GURTOGG_BLOODBOIL:
                    case DATA_RELIQUARY_OF_SOULS:
                        // 检查是否应该打开死亡之门
                        if (state == DONE && CheckDenOfMortalDoor())
                        {
                            // 播放开门台词
                            if (Creature* trigger = GetCreature(DATA_BLACK_TEMPLE_TRIGGER))
                                trigger->AI()->Talk(EMOTE_DEN_OF_MORTAL_DOOR_OPEN);

                            // 打开死亡之门
                            if (GameObject* door = GetGameObject(DATA_GO_DEN_OF_MORTAL_DOOR))
                                HandleGameObject(ObjectGuid::Empty, true, door);
                        }
                        break;
                    case DATA_ILLIDARI_COUNCIL:
                        // 伊利达雷议会死亡时，激活阿卡玛开始伊利丹战前的事件
                        if (state == DONE)
                            if (Creature* akama = GetCreature(DATA_AKAMA))
                                akama->AI()->DoAction(ACTION_ACTIVE_AKAMA_INTRO);
                        break;
                    default:
                        break;
                }

                return true;
            }

            /**
             * @brief 检查死亡之门开启条件
             * @return 是否满足开启条件
             *
             * 检查是否已击杀以下4个Boss：
             * - 阿卡玛之影
             * - 泰隆·血魔
             * - 灵魂之匣
             * - 古尔图格·血沸
             *
             * 只有这4个Boss都被击杀后，死亡之门才会打开
             */
            bool CheckDenOfMortalDoor()
            {
                for (BTDataTypes boss : {DATA_SHADE_OF_AKAMA, DATA_TERON_GOREFIEND, DATA_RELIQUARY_OF_SOULS, DATA_GURTOGG_BLOODBOIL})
                    if (GetBossState(boss) != DONE)
                        return false;
                return true;
            }

        protected:
            GuidVector AshtongueGUIDs;     ///< 灰舌NPC的GUID列表，用于阵营转换
            uint8 AkamaState;              ///< 阿卡玛事件状态
            uint8 AkamaIllidanIntro;       ///< 伊利丹战阿卡玛介绍状态
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图指针
         * @return 新创建的实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_black_temple_InstanceMapScript(map);
        }
};

/**
 * @brief 注册副本脚本
 *
 * 在服务器启动时调用，注册黑暗神殿副本脚本
 */
void AddSC_instance_black_temple()
{
    new instance_black_temple();
}

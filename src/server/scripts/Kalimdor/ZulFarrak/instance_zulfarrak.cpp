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
 * @file instance_zulfarrak.cpp
 * @brief 祖尔法拉克副本实例脚本
 *
 * 该模块实现祖尔法拉克副本的核心事件:金字塔防御战
 *
 * 副本背景:
 * 祖尔法拉克是沙漠巨魔的城市,玩家需要解救被俘虏的士兵并防御金字塔
 *
 * 金字塔事件流程:
 * 1. 玩家打开牢笼,释放布莱中士和他的队友
 * 2. NPC移动到楼梯顶部,等待玩家
 * 3. 第1波怪物从金字塔底部刷新,缓慢爬上楼梯
 * 4. 玩家需要防御NPC,击杀所有怪物
 * 5. 第2波更强的怪物刷新
 * 6. 第3波包含Boss,击杀后事件完成
 * 7. 布莱中士和队友变为敌对,玩家需要击杀他们
 * 8. 威利炸毁最终大门,玩家可以继续前进
 *
 * 技术要点:
 * - 大量怪物刷新(54个),需要优化性能
 * - 怪物分批爬楼梯,避免一次性刷新
 * - NPC状态变化(被动->友好->敌对)
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "zulfarrak.h"

/**
 * @enum Misc
 * @brief 杂项枚举定义
 */
enum Misc
{
    // Creatures
    NPC_GAHZRILLA       = 7273,  ///< 加兹瑞拉Boss(召唤BOSS,本脚本不处理其AI)

    // Paths
    PATH_ADDS           = 81553  ///< 怪物爬楼梯的路径ID
};

/// 金字塔怪物刷新点总数
int const pyramidSpawnTotal = 54;

/**
 * @brief 金字塔怪物刷新数据数组
 *
 * 格式: {波次ID, 怪物ID, X坐标, Y坐标}
 * 所有怪物的Z坐标相同(8.87f)
 *
 * 怪物类型:
 * - 7789: 祖尔法拉克僵尸
 * - 7787: 祖尔法拉克猎头者
 * - 7788: 祖尔法拉克影行者
 * - 8876: 祖尔法拉克斧投手
 * - 8877: 祖尔法拉克巫医
 * - 7275: 祖尔法拉克侍僧(Boss)
 * - 7796: 祖尔法拉克刽子手(Boss)
 */
float pyramidSpawns [pyramidSpawnTotal][4] = {
    // 第1波: 22个怪物
    {1, 7789, 1894.64f, 1206.29f},
    {1, 7787, 1890.08f, 1218.68f},
    {1, 8876, 1883.76f, 1222.3f},
    {1, 7789, 1874.18f, 1221.24f},
    {1, 7787, 1892.28f, 1225.49f},
    {1, 7788, 1889.94f, 1212.21f},
    {1, 7787, 1879.02f, 1223.06f},
    {1, 7789, 1874.45f, 1204.44f},
    {1, 8876, 1898.23f, 1217.97f},
    {1, 7787, 1882.07f, 1225.7f},
    {1, 8877, 1896.46f, 1205.62f},
    {1, 7787, 1886.97f, 1225.86f},
    {1, 7787, 1894.72f, 1221.91f},
    {1, 7787, 1883.5f, 1218.25f},
    {1, 7787, 1886.93f, 1221.4f},
    {1, 8876, 1889.82f, 1222.51f},
    {1, 7788, 1893.07f, 1215.26f},
    {1, 7788, 1878.57f, 1214.16f},
    {1, 7788, 1883.74f, 1212.35f},
    {1, 8877, 1877, 1207.27f},
    {1, 8877, 1873.63f, 1204.65f},
    {1, 8876, 1877.4f, 1216.41f},
    {1, 8877, 1899.63f, 1202.52f},
    // 第2波: 25个怪物(与第1波类似但更强)
    {2, 7789, 1902.83f, 1223.41f},
    {2, 8876, 1889.82f, 1222.51f},
    {2, 7787, 1883.5f, 1218.25f},
    {2, 7788, 1883.74f, 1212.35f},
    {2, 8877, 1877, 1207.27f},
    {2, 7787, 1890.08f, 1218.68f},
    {2, 7789, 1894.64f, 1206.29f},
    {2, 8876, 1877.4f, 1216.41f},
    {2, 7787, 1892.28f, 1225.49f},
    {2, 7788, 1893.07f, 1215.26f},
    {2, 8877, 1896.46f, 1205.62f},
    {2, 7789, 1874.45f, 1204.44f},
    {2, 7789, 1874.18f, 1221.24f},
    {2, 7787, 1879.02f, 1223.06f},
    {2, 8876, 1898.23f, 1217.97f},
    {2, 7787, 1882.07f, 1225.7f},
    {2, 8877, 1873.63f, 1204.65f},
    {2, 7787, 1886.97f, 1225.86f},
    {2, 7788, 1878.57f, 1214.16f},
    {2, 7787, 1894.72f, 1221.91f},
    {2, 7787, 1886.93f, 1221.4f},
    {2, 8876, 1883.76f, 1222.3f},
    {2, 7788, 1889.94f, 1212.21f},
    {2, 8877, 1899.63f, 1202.52f},
    // 第3波: 7个怪物(包含2个Boss)
    {3, 7788, 1878.57f, 1214.16f},
    {3, 7787, 1894.72f, 1221.91f},
    {3, 7787, 1886.93f, 1221.4f},
    {3, 8876, 1883.76f, 1222.3f},
    {3, 7788, 1889.94f, 1212.21f},
    {3, 7275, 1889.23f, 1207.72f},  // 祖尔法拉克侍僧(Boss)
    {3, 7796, 1879.77f, 1207.96f}   // 祖尔法拉克刽子手(Boss)
};

/**
 * @brief NPC移动路径点
 *
 * 定义NPC从金字塔底部到顶部的移动路径
 * 格式: {X, Y, Z}
 */
float Spawnsway[2][3] =
{
    {1884.86f, 1228.62f, 9},      // 路径点1: 金字塔底部
    {1887.53f, 1263, 41}          // 路径点2: 楼梯顶部
};

/**
 * @class instance_zulfarrak
 * @brief 祖尔法拉克副本实例脚本主类
 *
 * 继承自 InstanceMapScript,管理金字塔事件和Boss状态
 */
class instance_zulfarrak : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称和地图ID(209为祖尔法拉克)
     */
    instance_zulfarrak() : InstanceMapScript(ZFScriptName, 209) { }

    /**
     * @brief 创建实例脚本对象
     * @param map 副本地图指针
     * @return 新创建的实例脚本对象
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_zulfarrak_InstanceMapScript(map);
    }

    /**
     * @class instance_zulfarrak_InstanceMapScript
     * @brief 祖尔法拉克实例脚本实现类
     *
     * 管理金字塔事件和重要NPC的状态
     */
    struct instance_zulfarrak_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数,初始化所有状态
         * @param map 副本地图指针
         */
        instance_zulfarrak_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            GahzRillaEncounter = NOT_STARTED;
            PyramidPhase = 0;
            major_wave_Timer = 0;
            minor_wave_Timer = 0;
            addGroupSize = 0;
            waypoint = 0;
        }

        /// 加兹瑞拉Boss遭遇战状态
        uint32 GahzRillaEncounter;
        /// 祖尔拉姆Boss的GUID
        ObjectGuid ZumrahGUID;
        /// 布莱中士的GUID
        ObjectGuid BlyGUID;
        /// 威利·炸 fuse的GUID
        ObjectGuid WeegliGUID;
        /// 奥罗的GUID
        ObjectGuid OroGUID;
        /// 雷文的GUID
        ObjectGuid RavenGUID;
        /// 穆尔塔的GUID
        ObjectGuid MurtaGUID;
        /// 最终大门的GUID
        ObjectGuid EndDoorGUID;

        /// 金字塔事件当前阶段
        uint32 PyramidPhase;
        /// 主要波次计时器(波次间延迟)
        uint32 major_wave_Timer;
        /// 次要波次计时器(怪物爬楼梯间隔)
        uint32 minor_wave_Timer;
        /// 每次爬楼梯的怪物数量
        uint32 addGroupSize;
        /// 当前路径点(未使用)
        uint32 waypoint;

        /**
         * @brief 生物创建时的回调
         * @param creature 新创建的生物指针
         *
         * 记录重要NPC的GUID,并设置初始状态
         * 对于布莱和他的队友,设置为被动状态(他们在牢笼中)
         */
        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case ENTRY_ZUM_RAH:
                    ZumrahGUID = creature->GetGUID();
                    break;
                case ENTRY_BLY:
                    BlyGUID = creature->GetGUID();
                    creature->SetReactState(REACT_PASSIVE); // 开始时被动(在牢笼中)
                    break;
                case ENTRY_RAVEN:
                    RavenGUID = creature->GetGUID();
                    creature->SetReactState(REACT_PASSIVE);// 开始时被动(在牢笼中)
                    break;
                case ENTRY_ORO:
                    OroGUID = creature->GetGUID();
                    creature->SetReactState(REACT_PASSIVE);// 开始时被动(在牢笼中)
                    break;
                case ENTRY_WEEGLI:
                    WeegliGUID = creature->GetGUID();
                    creature->SetReactState(REACT_PASSIVE);// 开始时被动(在牢笼中)
                    break;
                case ENTRY_MURTA:
                    MurtaGUID = creature->GetGUID();
                    creature->SetReactState(REACT_PASSIVE);// 开始时被动(在牢笼中)
                    break;
                case NPC_GAHZRILLA:
                    // 加兹瑞拉只允许召唤一次
                    if (GahzRillaEncounter >= IN_PROGRESS)
                        creature->DisappearAndDie();
                    else
                        GahzRillaEncounter = IN_PROGRESS;
                    break;
            }
        }

        /**
         * @brief 游戏对象创建时的回调
         * @param go 新创建的游戏对象指针
         *
         * 记录最终大门的GUID,用于威利炸门事件
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_END_DOOR:
                    EndDoorGUID = go->GetGUID();
                    break;
            }
        }

        /**
         * @brief 获取事件数据
         * @param type 事件类型
         * @return 当前事件阶段
         *
         * 主要用于查询金字塔事件进度
         */
        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case EVENT_PYRAMID:
                    return PyramidPhase;
            }
            return 0;
        }

        /**
         * @brief 获取NPC或对象的GUID
         * @param data 数据ID
         * @return 对应的全局唯一标识符
         *
         * 用于其他脚本获取重要NPC和对象的GUID
         */
        ObjectGuid GetGuidData(uint32 data) const override
        {
            switch (data)
            {
                case ENTRY_ZUM_RAH:
                    return ZumrahGUID;
                case ENTRY_BLY:
                    return BlyGUID;
                case ENTRY_RAVEN:
                    return RavenGUID;
                case ENTRY_ORO:
                    return OroGUID;
                case ENTRY_WEEGLI:
                    return WeegliGUID;
                case ENTRY_MURTA:
                    return MurtaGUID;
                case GO_END_DOOR:
                    return EndDoorGUID;
            }
            return ObjectGuid::Empty;
        }

        /**
         * @brief 设置事件数据
         * @param type 事件类型
         * @param data 新的阶段数据
         *
         * 主要用于更新金字塔事件进度
         */
        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case EVENT_PYRAMID:
                    PyramidPhase = data;
                    break;
            }
        }

        /**
         * @brief 主更新函数,每帧调用
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 管理金字塔事件的完整流程:
         *
         * 状态机流程:
         * 1. PYRAMID_NOT_STARTED: 等待玩家打开牢笼
         * 2. PYRAMID_ARRIVED_AT_STAIR: NPC到达楼梯顶部,开始第1波
         * 3. PYRAMID_WAVE_1: 第1波进行中,分批发送怪物爬楼梯
         * 4. PYRAMID_PRE_WAVE_2: 第1波结束,10秒后开始第2波
         * 5. PYRAMID_WAVE_2: 第2波进行中,分批发送怪物爬楼梯
         * 6. PYRAMID_PRE_WAVE_3: 第2波结束,生成第3波并移动NPC到底部
         * 7. PYRAMID_WAVE_3: 第3波进行中(包含Boss)
         * 8. PYRAMID_KILLED_ALL_TROLLS: 所有怪物被击杀,事件完成
         *
         * 性能优化:
         * - 怪物分批爬楼梯,避免一次性刷新54个怪物
         * - 使用计时器控制波次间隔
         */
        virtual void Update(uint32 diff) override
        {
            switch (PyramidPhase)
            {
                case PYRAMID_NOT_STARTED:
                case PYRAMID_KILLED_ALL_TROLLS:
                    break;
                case PYRAMID_ARRIVED_AT_STAIR:
                    // NPC到达楼梯顶部,开始第1波
                    SpawnPyramidWave(1);
                    SetData(EVENT_PYRAMID, PYRAMID_WAVE_1);
                    major_wave_Timer=120000;  // 2分钟超时(未使用)
                    minor_wave_Timer=0;
                    addGroupSize=2;  // 初始每组2个怪物
                    break;
                case PYRAMID_WAVE_1:
                    if (IsWaveAllDead())
                    {
                        // 第1波完成,准备第2波
                        SetData(EVENT_PYRAMID, PYRAMID_PRE_WAVE_2);
                        major_wave_Timer = 10000; // 10秒延迟让玩家重整
                    }
                    else
                        if (minor_wave_Timer<diff)
                        {
                            // 定期发送怪物爬楼梯
                            SendAddsUpStairs(addGroupSize++);
                            minor_wave_Timer=10000;  // 每10秒发送一组
                        }
                        else
                            minor_wave_Timer -= diff;
                    break;
                case PYRAMID_PRE_WAVE_2:
                    if (major_wave_Timer<diff)
                    {
                        // 开始第2波
                        SpawnPyramidWave(2);
                        SetData(EVENT_PYRAMID, PYRAMID_WAVE_2);
                        minor_wave_Timer = 0;
                        addGroupSize=2;
                    }
                    else
                        major_wave_Timer -= diff;
                    break;
                case PYRAMID_WAVE_2:
                    if (IsWaveAllDead())
                    {
                        // 第2波完成,生成第3波并移动NPC
                        SpawnPyramidWave(3);
                        SetData(EVENT_PYRAMID, PYRAMID_PRE_WAVE_3);
                        major_wave_Timer = 5000; // 5秒让NPC移动到底部
                    }
                    else
                        if (minor_wave_Timer<diff)
                        {
                            SendAddsUpStairs(addGroupSize++);
                            minor_wave_Timer=10000;
                        }
                        else
                            minor_wave_Timer -= diff;
                    break;
                case PYRAMID_PRE_WAVE_3:
                    if (major_wave_Timer<diff)
                    {
                        // 移动NPC到楼梯底部,准备最终战斗
                        MoveNPCIfAlive(ENTRY_BLY, 1887.92f, 1228.179f, 9.98f, 4.78f);
                        MoveNPCIfAlive(ENTRY_MURTA, 1891.57f, 1228.68f, 9.69f, 4.78f);
                        MoveNPCIfAlive(ENTRY_ORO, 1897.23f, 1228.34f, 9.43f, 4.78f);
                        MoveNPCIfAlive(ENTRY_RAVEN, 1883.68f, 1227.95f, 9.543f, 4.78f);
                        MoveNPCIfAlive(ENTRY_WEEGLI, 1878.02f, 1227.65f, 9.485f, 4.78f);
                        SetData(EVENT_PYRAMID, PYRAMID_WAVE_3);
                    }
                    else
                        major_wave_Timer -= diff;
                    break;
                case PYRAMID_WAVE_3:
                    if (IsWaveAllDead()) // 所有怪物被击杀,事件完成
                    {
                        SetData(EVENT_PYRAMID, PYRAMID_KILLED_ALL_TROLLS);
                        // 移动NPC到最终位置,准备与玩家对话
                        MoveNPCIfAlive(ENTRY_BLY, 1883.82f, 1200.83f, 8.87f, 1.32f);
                        MoveNPCIfAlive(ENTRY_MURTA, 1891.83f, 1201.45f, 8.87f, 1.32f);
                        MoveNPCIfAlive(ENTRY_ORO, 1894.50f, 1204.40f, 8.87f, 1.32f);
                        MoveNPCIfAlive(ENTRY_RAVEN, 1874.11f, 1206.17f, 8.87f, 1.32f);
                        MoveNPCIfAlive(ENTRY_WEEGLI, 1877.52f, 1199.63f, 8.87f, 1.32f);
                    }
                    break;
            };
        }

        /// 在金字塔底部的怪物列表
        GuidList addsAtBase;
        /// 已爬上楼梯的怪物列表
        GuidList movedadds;

        /**
         * @brief 移动NPC到指定位置
         * @param entry NPC的Entry ID
         * @param x 目标X坐标
         * @param y 目标Y坐标
         * @param z 目标Z坐标
         * @param o 目标朝向
         *
         * 如果NPC存活,设置其行走模式并移动到目标位置
         */
        void MoveNPCIfAlive(uint32 entry, float x, float y, float z, float o)
        {
           if (Creature* npc = instance->GetCreature(GetGuidData(entry)))
           {
               if (npc->IsAlive())
               {
                    npc->SetWalk(true);
                    npc->GetMotionMaster()->MovePoint(1, x, y, z);
                    npc->SetHomePosition(x, y, z, o);
               }
            }
        }

        /**
         * @brief 刷新金字塔波次怪物
         * @param wave 波次ID(1, 2, 或 3)
         *
         * 根据波次ID从数组中查找对应的怪物,在金字塔底部刷新
         * 怪物初始在底部随机移动,稍后会被发送爬楼梯
         *
         * 性能注意:
         * - 第1波和第2波包含大量怪物(22-25个)
         * - 使用临时召唤,尸体在15秒后消失
         */
        void SpawnPyramidWave(uint32 wave)
        {
            for (int i = 0; i < pyramidSpawnTotal; i++)
            {
                if (pyramidSpawns[i][0] == (float)wave)
                {
                    Position pos = {pyramidSpawns[i][2], pyramidSpawns[i][3], 8.87f, 0};
                    TempSummon* ts = instance->SummonCreature(uint32(pyramidSpawns[i][1]), pos);
                    ts->GetMotionMaster()->MoveRandom(10);  // 在底部随机移动
                    addsAtBase.push_back(ts->GetGUID());
                }
            }
        }

        /**
         * @brief 检查当前波次是否所有怪物都被击杀
         * @return true如果所有怪物都死亡
         *
         * 遍历两个列表(底部和已爬楼梯),检查是否有存活的怪物
         *
         * 性能注意:
         * - 每帧都会被调用(在波次进行中)
         * - 需要遍历所有怪物GUID
         * - 优化建议:可以使用计数器替代遍历
         */
        bool IsWaveAllDead()
        {
            for (GuidList::iterator itr = addsAtBase.begin(); itr != addsAtBase.end(); ++itr)
            {
                if (Creature* add = instance->GetCreature((*itr)))
                {
                    if (add->IsAlive())
                        return false;
                }
            }
            for (GuidList::iterator itr = movedadds.begin(); itr != movedadds.end(); ++itr)
            {
                if (Creature* add = instance->GetCreature(((*itr))))
                {
                    if (add->IsAlive())
                        return false;
                }
            }
            return true;
        }

        /**
         * @brief 发送怪物爬楼梯
         * @param count 本次发送的怪物数量
         *
         * 从底部列表中取出怪物,让它们沿着路径爬上楼梯
         * 怪物从底部列表移动到已爬楼梯列表
         */
        void SendAddsUpStairs(uint32 count)
        {
            // 取出怪物并发送它们爬楼梯...
            for (uint32 addCount = 0; addCount<count && !addsAtBase.empty(); addCount++)
            {
                if (Creature* add = instance->GetCreature(*addsAtBase.begin()))
                {
                    add->GetMotionMaster()->MovePath(PATH_ADDS, false);
                    movedadds.push_back(add->GetGUID());
                }
                addsAtBase.erase(addsAtBase.begin());
            }
        }
    };

};

/**
 * @brief 注册祖尔法拉克实例脚本
 *
 * 此函数在脚本加载时被调用,创建实例脚本对象
 */
void AddSC_instance_zulfarrak()
{
    new instance_zulfarrak();
}

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
 * @file BattlegroundScore.h
 * @brief 战场计分系统头文件
 *
 * 本文件定义了战场计分系统的核心数据结构和枚举类型。
 * 战场计分用于记录和追踪玩家在战场中的表现，包括击杀、死亡、伤害、治疗等统计数据，
 * 以及特定战场类型的特殊目标完成情况（如夺旗、占领基地等）。
 *
 * @see Battleground
 * @see Arena
 */

#ifndef TRINITY_BATTLEGROUND_SCORE_H
#define TRINITY_BATTLEGROUND_SCORE_H

#include "Errors.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"

class WorldPacket;

/**
 * @enum ScoreType
 * @brief 战场计分类型枚举
 *
 * 定义了战场中各种计分项目的类型标识。
 * 这些类型用于更新和追踪玩家在战场中的各种表现数据。
 *
 * 计分类型分为以下几类：
 * - 基础计分：适用于所有战场类型（击杀、死亡、伤害、治疗等）
 * - 夺旗相关：战歌峡谷(WS)和风暴之眼(EY)专用
 * - 基地相关：阿拉希盆地(AB)和征服之岛(IC)专用
 * - 奥特兰克山谷特定：墓地、塔楼、矿洞相关
 * - 远古海滩特定：攻城器械和城墙相关
 */
enum ScoreType
{
    // ========== 基础计分类型（适用于所有战场） ==========

    SCORE_KILLING_BLOWS         = 1,  ///< 击杀 blows（最后一击击杀敌人的次数）

    SCORE_DEATHS                = 2,  ///< 死亡次数

    SCORE_HONORABLE_KILLS       = 3,  ///< 荣誉击杀次数（击杀荣誉目标）

    SCORE_BONUS_HONOR           = 4,  ///< 奖励荣誉值

    SCORE_DAMAGE_DONE           = 5,  ///< 造成的伤害总量

    SCORE_HEALING_DONE          = 6,  ///< 造成的治疗总量

    // ========== 夺旗相关计分（战歌峡谷 WS 和风暴之眼 EY） ==========

    SCORE_FLAG_CAPTURES         = 7,  ///< 夺旗成功次数

    SCORE_FLAG_RETURNS          = 8,  ///< 旗帜归还次数（夺回己方旗帜）

    // ========== 基地相关计分（阿拉希盆地 AB 和征服之岛 IC） ==========

    SCORE_BASES_ASSAULTED       = 9,  ///< 突袭基地次数（攻击敌方基地）

    SCORE_BASES_DEFENDED        = 10, ///< 防守基地次数（保卫己方基地）

    // ========== 奥特兰克山谷(AV)特定计分 ==========

    SCORE_GRAVEYARDS_ASSAULTED  = 11, ///< 突袭墓地次数

    SCORE_GRAVEYARDS_DEFENDED   = 12, ///< 防守墓地次数

    SCORE_TOWERS_ASSAULTED      = 13, ///< 突袭塔楼次数

    SCORE_TOWERS_DEFENDED       = 14, ///< 防守塔楼次数

    SCORE_MINES_CAPTURED        = 15, ///< 占领矿洞次数

    // ========== 远古海滩(SOTA)特定计分 ==========

    SCORE_DESTROYED_DEMOLISHER  = 16, ///< 摧毁攻城车次数

    SCORE_DESTROYED_WALL        = 17  ///< 摧毁城墙次数
};

/**
 * @struct BattlegroundScore
 * @brief 战场计分基类
 *
 * 这是所有战场计分类的基类，用于记录玩家在战场中的表现数据。
 * 每个参与战场的玩家都会有一个对应的计分对象，用于追踪其战斗统计和目标完成情况。
 *
 * 该类提供了以下核心功能：
 * - 存储基础战斗数据（击杀、死亡、伤害、治疗等）
 * - 通过 UpdateScore() 方法更新各种计分数据
 * - 通过 AppendToPacket() 方法将计分数据序列化到网络数据包
 * - 通过 BuildObjectivesBlock() 方法构建特定战场的目标数据块（纯虚函数，由子类实现）
 *
 * 继承说明：
 * - 不同的战场类型可以继承此类并添加特定的计分字段
 * - 子类必须实现 BuildObjectivesBlock() 纯虚函数
 * - 子类可以重写 GetAttr1-5() 方法来提供额外的属性数据
 *
 * 访问控制：
 * - Arena 和 Battleground 类被声明为友元，可以访问保护成员
 * - 其他类通过公共 getter 方法访问计分数据
 *
 * @see ScoreType
 * @see Battleground
 * @see Arena
 */
struct BattlegroundScore
{
    friend class Arena;      ///< 竞技场类可以访问保护成员
    friend class Battleground; ///< 战场类可以访问保护成员

    protected:
        /**
         * @brief 构造函数
         *
         * 初始化战场计分对象，将所有计分字段初始化为 0。
         *
         * @param playerGuid 玩家的 GUID（全局唯一标识符）
         *
         * @note 此构造函数为保护类型，只能由子类或友元类调用
         */
        BattlegroundScore(ObjectGuid playerGuid) : PlayerGuid(playerGuid), KillingBlows(0), Deaths(0),
            HonorableKills(0), BonusHonor(0), DamageDone(0), HealingDone(0) { }

        /**
         * @brief 虚析构函数
         *
         * 确保子类对象能够正确析构，避免内存泄漏。
         */
        virtual ~BattlegroundScore() { }

        /**
         * @brief 更新计分数据
         *
         * 根据指定的计分类型更新相应的计分值。此方法采用累加方式，
         * 即新的值会加到现有值上，而不是替换。
         *
         * 基类实现处理所有基础计分类型（SCORE_KILLING_BLOWS 到 SCORE_HEALING_DONE）。
         * 子类可以重写此方法以处理特定的战场计分类型。
         *
         * @param type 计分类型，使用 ScoreType 枚举值
         * @param value 要增加的值（正值表示增加，通常不使用负值）
         *
         * @warning 如果传入未实现的计分类型，将触发 ABORT_MSG 终止程序
         *
         * @see ScoreType
         *
         * @code
         * // 示例：增加击杀 blows
         * score->UpdateScore(SCORE_KILLING_BLOWS, 1);
         *
         * // 示例：增加伤害量
         * score->UpdateScore(SCORE_DAMAGE_DONE, damageAmount);
         * @endcode
         */
        virtual void UpdateScore(uint32 type, uint32 value)
        {
            switch (type)
            {
                case SCORE_KILLING_BLOWS:   // 击杀 blows
                    KillingBlows += value;
                    break;
                case SCORE_DEATHS:          // 死亡次数
                    Deaths += value;
                    break;
                case SCORE_HONORABLE_KILLS: // 荣誉击杀
                    HonorableKills += value;
                    break;
                case SCORE_BONUS_HONOR:     // 荣誉奖励
                    BonusHonor += value;
                    break;
                case SCORE_DAMAGE_DONE:     // 造成的伤害
                    DamageDone += value;
                    break;
                case SCORE_HEALING_DONE:    // 造成的治疗
                    HealingDone += value;
                    break;
                default:
                    ABORT_MSG("Not implemented Battleground score type!");
                    break;
            }
        }

        /**
         * @brief 将计分数据追加到网络数据包
         *
         * 将玩家的计分数据序列化并写入到 WorldPacket 中，用于发送给客户端。
         * 基类实现会写入所有基础计分数据。
         *
         * @param data 要写入的 WorldPacket 引用
         *
         * @see BuildObjectivesBlock()
         */
        virtual void AppendToPacket(WorldPacket& data);

        /**
         * @brief 构建目标数据块
         *
         * 纯虚函数，由子类实现。用于构建特定战场类型的目标完成数据块。
         * 不同的战场有不同的胜利条件和目标，这些数据由子类提供。
         *
         * 例如：
         * - 战歌峡谷：夺旗次数、归还次数
         * - 阿拉希盆地：基地突袭、防守次数
         * - 奥特兰克山谷：墓地、塔楼、矿洞相关数据
         *
         * @param data 要写入的 WorldPacket 引用
         *
         * @note 子类必须实现此方法
         */
        virtual void BuildObjectivesBlock(WorldPacket& /*data*/) = 0;

        /**
         * @brief 将计分数据转换为字符串
         *
         * 用于日志记录目的，返回计分数据的字符串表示。
         * 子类可以重写此方法以提供更详细的信息。
         *
         * @return 计分数据的字符串表示，基类返回空字符串
         *
         * @note 主要用于调试和日志记录
         */
        virtual std::string ToString() const { return ""; }

        /**
         * @brief 获取击杀 blows 次数
         * @return 击杀 blows（最后一击击杀敌人）的次数
         */
        uint32 GetKillingBlows() const    { return KillingBlows; }

        /**
         * @brief 获取死亡次数
         * @return 玩家在战场中的死亡次数
         */
        uint32 GetDeaths() const          { return Deaths; }

        /**
         * @brief 获取荣誉击杀次数
         * @return 荣誉击杀（击杀荣誉目标）的次数
         */
        uint32 GetHonorableKills() const  { return HonorableKills; }

        /**
         * @brief 获取奖励荣誉值
         * @return 通过战场目标获得的奖励荣誉值
         */
        uint32 GetBonusHonor() const      { return BonusHonor; }

        /**
         * @brief 获取造成的伤害总量
         * @return 玩家在战场中造成的总伤害量
         */
        uint32 GetDamageDone() const      { return DamageDone; }

        /**
         * @brief 获取造成的治疗总量
         * @return 玩家在战场中造成的总治疗量
         */
        uint32 GetHealingDone() const     { return HealingDone; }

        /**
         * @brief 获取额外属性 1
         * @return 特定战场类型的额外属性值，基类返回 0
         * @note 子类应重写此方法以提供特定战场的数据
         */
        virtual uint32 GetAttr1() const { return 0; }

        /**
         * @brief 获取额外属性 2
         * @return 特定战场类型的额外属性值，基类返回 0
         * @note 子类应重写此方法以提供特定战场的数据
         */
        virtual uint32 GetAttr2() const { return 0; }

        /**
         * @brief 获取额外属性 3
         * @return 特定战场类型的额外属性值，基类返回 0
         * @note 子类应重写此方法以提供特定战场的数据
         */
        virtual uint32 GetAttr3() const { return 0; }

        /**
         * @brief 获取额外属性 4
         * @return 特定战场类型的额外属性值，基类返回 0
         * @note 子类应重写此方法以提供特定战场的数据
         */
        virtual uint32 GetAttr4() const { return 0; }

        /**
         * @brief 获取额外属性 5
         * @return 特定战场类型的额外属性值，基类返回 0
         * @note 子类应重写此方法以提供特定战场的数据
         */
        virtual uint32 GetAttr5() const { return 0; }

        ObjectGuid PlayerGuid;  ///< 玩家的全局唯一标识符（GUID）

        // ========== 基础计分字段（所有战场类型通用） ==========

        uint32 KillingBlows;    ///< 击杀 blows 次数（最后一击击杀敌人的次数）

        uint32 Deaths;          ///< 死亡次数

        uint32 HonorableKills;  ///< 荣誉击杀次数（击杀荣誉目标）

        uint32 BonusHonor;      ///< 奖励荣誉值

        uint32 DamageDone;      ///< 造成的伤害总量

        uint32 HealingDone;     ///< 造成的治疗总量
};

#endif // TRINITY_BATTLEGROUND_SCORE_H

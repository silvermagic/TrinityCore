/**
 * @file WheatyExceptionReport.h
 * @brief Windows平台异常捕获和崩溃报告生成模块
 *
 * 本模块实现了Windows平台下的结构化异常处理（SEH）机制，
 * 用于捕获未处理的异常并生成详细的崩溃报告，包括：
 * - 异常类型和错误代码
 * - 调用堆栈回溯
 * - 局部变量和参数的值
 * - CPU寄存器状态
 * - 系统信息
 *
 * 该模块基于MSDN Magazine 2002年Matt Pietrek的文章实现，
 * 并针对TrinityCore项目进行了扩展和优化。
 *
 * @note 本文件仅适用于Windows平台
 */

#ifndef _WHEATYEXCEPTIONREPORT_
#define _WHEATYEXCEPTIONREPORT_

#define _NO_CVCONST_H

#include "Optional.h"
#include <windows.h>
#include <winnt.h>
#include <winternl.h>
#include <dbghelp.h>
#include <compare>
#include <set>
#include <cstdlib>
#include <cstdio>
#include <stack>
#include <mutex>

/**
 * @defgroup DebugConstants 调试常量定义
 * @{
 */

/**
 * @brief 数组元素最大显示数量
 *
 * 在输出数组内容时，最多显示的元素数量
 * 防止过大的数组导致日志文件过大
 */
#define WER_MAX_ARRAY_ELEMENTS_COUNT 10

/**
 * @brief 最大嵌套层级
 *
 * 在输出嵌套数据结构时，最大递归深度
 * 防止过深的嵌套导致堆栈溢出或输出过多
 */
#define WER_MAX_NESTING_LEVEL 4

/**
 * @brief 小缓冲区大小
 *
 * 用于格式化较小的输出内容
 */
#define WER_SMALL_BUFFER_SIZE 1024

/**
 * @brief 大缓冲区大小
 *
 * 用于格式化较大的输出内容（如复杂的类型信息）
 */
#define WER_LARGE_BUFFER_SIZE WER_SMALL_BUFFER_SIZE * 16

/** @} */ // DebugConstants

/**
 * @enum BasicType
 * @brief 基础数据类型枚举
 *
 * 定义了调试符号系统中使用的基础数据类型
 * 这些值来源于DIA 2.0 SDK中的CVCONST.H文件
 *
 * @note 自定义类型从101开始编号
 */
enum BasicType
{
    btNoType = 0,          ///< 未定义类型
    btVoid = 1,            ///< void类型
    btChar = 2,            ///< char类型
    btWChar = 3,           ///< 宽字符类型(wchar_t)
    btInt = 6,             ///< int类型
    btUInt = 7,            ///< unsigned int类型
    btFloat = 8,           ///< 浮点类型
    btBCD = 9,             ///< BCD(二进制编码十进制)类型
    btBool = 10,           ///< 布尔类型
    btLong = 13,           ///< long类型
    btULong = 14,          ///< unsigned long类型
    btCurrency = 25,       ///< 货币类型
    btDate = 26,           ///< 日期类型
    btVariant = 27,        ///< VARIANT类型
    btComplex = 28,        ///< 复数类型
    btBit = 29,            ///< 位类型
    btBSTR = 30,           ///< BSTR字符串类型
    btHresult = 31,        ///< HRESULT类型

    // Custom types - 自定义类型
    btStdString = 101      ///< std::string类型（自定义扩展）
};

/**
 * @enum DataKind
 * @brief 数据类别枚举
 *
 * 定义了变量和数据的存储类别
 * 这些值来源于DIA 2.0 SDK中的CVCONST.H文件
 */
enum DataKind
{
    DataIsUnknown,         ///< 未知类别
    DataIsLocal,           ///< 局部变量
    DataIsStaticLocal,     ///< 静态局部变量
    DataIsParam,           ///< 函数参数
    DataIsObjectPtr,       ///< 对象指针(this指针)
    DataIsFileStatic,      ///< 文件作用域静态变量
    DataIsGlobal,          ///< 全局变量
    DataIsMember,          ///< 类成员变量
    DataIsStaticMember,    ///< 静态成员变量
    DataIsConstant         ///< 常量
};

/**
 * @enum CpuRegister
 * @brief CPU寄存器标识枚举
 *
 * 定义了各种CPU架构的寄存器标识符
 * 这些值来源于DIA SDK中的CVCONST.H文件
 * 支持的架构包括：
 * - Intel x86 (32位)
 * - AMD64 (x64)
 * - ARM64
 */
enum CpuRegister
{
    CV_ALLREG_VFRAME=   30006,

    //
    //  Register set for the Intel 80x86 and ix86 processor series
    //
    CV_REG_NONE     =   0,
    CV_REG_AL       =   1,
    CV_REG_CL       =   2,
    CV_REG_DL       =   3,
    CV_REG_BL       =   4,
    CV_REG_AH       =   5,
    CV_REG_CH       =   6,
    CV_REG_DH       =   7,
    CV_REG_BH       =   8,
    CV_REG_AX       =   9,
    CV_REG_CX       =  10,
    CV_REG_DX       =  11,
    CV_REG_BX       =  12,
    CV_REG_SP       =  13,
    CV_REG_BP       =  14,
    CV_REG_SI       =  15,
    CV_REG_DI       =  16,
    CV_REG_EAX      =  17,
    CV_REG_ECX      =  18,
    CV_REG_EDX      =  19,
    CV_REG_EBX      =  20,
    CV_REG_ESP      =  21,
    CV_REG_EBP      =  22,
    CV_REG_ESI      =  23,
    CV_REG_EDI      =  24,
    CV_REG_EIP      =  33,

    //
    // AMD64 registers
    //
    CV_AMD64_AL       =   1,
    CV_AMD64_CL       =   2,
    CV_AMD64_DL       =   3,
    CV_AMD64_BL       =   4,
    CV_AMD64_AH       =   5,
    CV_AMD64_CH       =   6,
    CV_AMD64_DH       =   7,
    CV_AMD64_BH       =   8,
    CV_AMD64_AX       =   9,
    CV_AMD64_CX       =  10,
    CV_AMD64_DX       =  11,
    CV_AMD64_BX       =  12,
    CV_AMD64_SP       =  13,
    CV_AMD64_BP       =  14,
    CV_AMD64_SI       =  15,
    CV_AMD64_DI       =  16,
    CV_AMD64_EAX      =  17,
    CV_AMD64_ECX      =  18,
    CV_AMD64_EDX      =  19,
    CV_AMD64_EBX      =  20,
    CV_AMD64_ESP      =  21,
    CV_AMD64_EBP      =  22,
    CV_AMD64_ESI      =  23,
    CV_AMD64_EDI      =  24,
    CV_AMD64_RIP      =  33,

    // Low byte forms of some standard registers
    CV_AMD64_SIL      =  324,
    CV_AMD64_DIL      =  325,
    CV_AMD64_BPL      =  326,
    CV_AMD64_SPL      =  327,

    // 64-bit regular registers
    CV_AMD64_RAX      =  328,
    CV_AMD64_RBX      =  329,
    CV_AMD64_RCX      =  330,
    CV_AMD64_RDX      =  331,
    CV_AMD64_RSI      =  332,
    CV_AMD64_RDI      =  333,
    CV_AMD64_RBP      =  334,
    CV_AMD64_RSP      =  335,

    // 64-bit integer registers with 8-, 16-, and 32-bit forms (B, W, and D)
    CV_AMD64_R8       =  336,
    CV_AMD64_R9       =  337,
    CV_AMD64_R10      =  338,
    CV_AMD64_R11      =  339,
    CV_AMD64_R12      =  340,
    CV_AMD64_R13      =  341,
    CV_AMD64_R14      =  342,
    CV_AMD64_R15      =  343,

    CV_AMD64_R8B      =  344,
    CV_AMD64_R9B      =  345,
    CV_AMD64_R10B     =  346,
    CV_AMD64_R11B     =  347,
    CV_AMD64_R12B     =  348,
    CV_AMD64_R13B     =  349,
    CV_AMD64_R14B     =  350,
    CV_AMD64_R15B     =  351,

    CV_AMD64_R8W      =  352,
    CV_AMD64_R9W      =  353,
    CV_AMD64_R10W     =  354,
    CV_AMD64_R11W     =  355,
    CV_AMD64_R12W     =  356,
    CV_AMD64_R13W     =  357,
    CV_AMD64_R14W     =  358,
    CV_AMD64_R15W     =  359,

    CV_AMD64_R8D      =  360,
    CV_AMD64_R9D      =  361,
    CV_AMD64_R10D     =  362,
    CV_AMD64_R11D     =  363,
    CV_AMD64_R12D     =  364,
    CV_AMD64_R13D     =  365,
    CV_AMD64_R14D     =  366,
    CV_AMD64_R15D     =  367,

    //
    // Register set for ARM64
    //
    CV_ARM64_NOREG  =  CV_REG_NONE,

    // General purpose 32-bit integer registers
    CV_ARM64_W0     =  10,
    CV_ARM64_W1     =  11,
    CV_ARM64_W2     =  12,
    CV_ARM64_W3     =  13,
    CV_ARM64_W4     =  14,
    CV_ARM64_W5     =  15,
    CV_ARM64_W6     =  16,
    CV_ARM64_W7     =  17,
    CV_ARM64_W8     =  18,
    CV_ARM64_W9     =  19,
    CV_ARM64_W10    =  20,
    CV_ARM64_W11    =  21,
    CV_ARM64_W12    =  22,
    CV_ARM64_W13    =  23,
    CV_ARM64_W14    =  24,
    CV_ARM64_W15    =  25,
    CV_ARM64_W16    =  26,
    CV_ARM64_W17    =  27,
    CV_ARM64_W18    =  28,
    CV_ARM64_W19    =  29,
    CV_ARM64_W20    =  30,
    CV_ARM64_W21    =  31,
    CV_ARM64_W22    =  32,
    CV_ARM64_W23    =  33,
    CV_ARM64_W24    =  34,
    CV_ARM64_W25    =  35,
    CV_ARM64_W26    =  36,
    CV_ARM64_W27    =  37,
    CV_ARM64_W28    =  38,
    CV_ARM64_W29    =  39,
    CV_ARM64_W30    =  40,
    CV_ARM64_WZR    =  41,

    // General purpose 64-bit integer registers
    CV_ARM64_X0     =  50,
    CV_ARM64_X1     =  51,
    CV_ARM64_X2     =  52,
    CV_ARM64_X3     =  53,
    CV_ARM64_X4     =  54,
    CV_ARM64_X5     =  55,
    CV_ARM64_X6     =  56,
    CV_ARM64_X7     =  57,
    CV_ARM64_X8     =  58,
    CV_ARM64_X9     =  59,
    CV_ARM64_X10    =  60,
    CV_ARM64_X11    =  61,
    CV_ARM64_X12    =  62,
    CV_ARM64_X13    =  63,
    CV_ARM64_X14    =  64,
    CV_ARM64_X15    =  65,
    CV_ARM64_IP0    =  66,
    CV_ARM64_IP1    =  67,
    CV_ARM64_X18    =  68,
    CV_ARM64_X19    =  69,
    CV_ARM64_X20    =  70,
    CV_ARM64_X21    =  71,
    CV_ARM64_X22    =  72,
    CV_ARM64_X23    =  73,
    CV_ARM64_X24    =  74,
    CV_ARM64_X25    =  75,
    CV_ARM64_X26    =  76,
    CV_ARM64_X27    =  77,
    CV_ARM64_X28    =  78,
    CV_ARM64_FP     =  79,
    CV_ARM64_LR     =  80,
    CV_ARM64_SP     =  81,
    CV_ARM64_ZR     =  82,
};

/**
 * @brief 基础类型名称数组
 *
 * 提供BasicType枚举值对应的可读字符串表示
 * 索引对应BasicType枚举值
 */
char const* const rgBaseType[] =
{
    "<user defined>",                                     // btNoType = 0, 用户自定义类型
    "void",                                               // btVoid = 1, void类型
    "char",//char*                                        // btChar = 2, 字符类型
    "wchar_t*",                                           // btWChar = 3, 宽字符类型
    "signed char",                                        // 有符号字符
    "unsigned char",                                      // 无符号字符
    "int",                                                // btInt = 6, 整数类型
    "unsigned int",                                       // btUInt = 7, 无符号整数
    "float",                                              // btFloat = 8, 单精度浮点
    "<BCD>",                                              // btBCD = 9, BCD编码
    "bool",                                               // btBool = 10, 布尔类型
    "short",                                              // 短整数
    "unsigned short",                                     // 无符号短整数
    "long",                                               // btLong = 13, 长整数
    "unsigned long",                                      // btULong = 14, 无符号长整数
    "int8",                                               // 8位整数
    "int16",                                              // 16位整数
    "int32",                                              // 32位整数
    "int64",                                              // 64位整数
    "int128",                                             // 128位整数
    "uint8",                                              // 无符号8位整数
    "uint16",                                             // 无符号16位整数
    "uint32",                                             // 无符号32位整数
    "uint64",                                             // 无符号64位整数
    "uint128",                                            // 无符号128位整数
    "<currency>",                                         // btCurrency = 25, 货币类型
    "<date>",                                             // btDate = 26, 日期类型
    "VARIANT",                                            // btVariant = 27, VARIANT类型
    "<complex>",                                          // btComplex = 28, 复数类型
    "<bit>",                                              // btBit = 29, 位类型
    "BSTR",                                               // btBSTR = 30, BSTR字符串
    "HRESULT"                                             // btHresult = 31, HRESULT类型
};

/**
 * @struct SymbolPair
 * @brief 符号对结构体
 *
 * 用于存储符号的类型和偏移量信息
 * 在遍历调试符号时用于去重和跟踪已处理的符号
 */
struct SymbolPair
{
    /**
     * @brief 构造函数
     *
     * @param type 符号类型
     * @param offset 符号偏移量
     */
    SymbolPair(DWORD type, DWORD_PTR offset)
    {
        _type = type;
        _offset = offset;
    }

    /**
     * @brief 相等比较运算符
     */
    bool operator==(SymbolPair const& other) const = default;

    /**
     * @brief 三向比较运算符
     *
     * 用于支持std::set的排序和查找
     */
    std::strong_ordering operator<=>(SymbolPair const& other) const = default;

    DWORD _type;        ///< 符号类型
    DWORD_PTR _offset;  ///< 符号偏移量
};

/**
 * @typedef SymbolPairs
 * @brief 符号对集合类型
 *
 * 使用std::set存储符号对，自动去重并保持有序
 */
typedef std::set<SymbolPair> SymbolPairs;

/**
 * @struct SymbolDetail
 * @brief 符号详细信息结构体
 *
 * 存储符号的完整信息，包括名称、类型、值等
 * 用于构建符号的层次化输出
 */
struct SymbolDetail
{
    /**
     * @brief 默认构造函数
     *
     * 初始化所有成员为默认值
     */
    SymbolDetail() : Prefix(), Type(), Suffix(), Name(), Value(), Logged(false), HasChildren(false) {}

    /**
     * @brief 转换为字符串
     *
     * @return std::string 符号的字符串表示
     */
    std::string ToString();

    /**
     * @brief 检查符号是否为空
     *
     * @return true 如果符号没有值且没有子符号
     * @return false 如果符号有值或有子符号
     */
    bool empty() const
    {
        return Value.empty() && !HasChildren;
    }

    std::string Prefix;       ///< 前缀（如缩进、修饰符等）
    std::string Type;         ///< 类型名称
    std::string Suffix;       ///< 后缀（如数组维度等）
    std::string Name;         ///< 符号名称
    std::string Value;        ///< 符号值
    bool Logged;              ///< 是否已记录（防止重复输出）
    bool HasChildren;         ///< 是否有子符号（如结构体成员）
};

/**
 * @class WheatyExceptionReport
 * @brief Windows异常报告生成器
 *
 * 该类实现了Windows平台下的结构化异常处理机制，
 * 能够捕获未处理的异常并生成详细的崩溃报告。
 *
 * 主要功能包括：
 * - 安装异常过滤器捕获未处理异常
 * - 生成调用堆栈回溯
 * - 提取局部变量和参数值
 * - 输出CPU寄存器状态
 * - 收集系统信息
 * - 生成崩溃转储文件
 *
 * @note 该类使用单例模式，全局只有一个实例
 */
class WheatyExceptionReport
{
    public:

        /**
         * @brief 构造函数
         *
         * 初始化异常报告系统，安装异常过滤器和CRT处理器
         */
        WheatyExceptionReport();

        /**
         * @brief 析构函数
         *
         * 清理资源，恢复之前的异常过滤器
         */
        ~WheatyExceptionReport();

        /**
         * @brief 未处理异常过滤器
         *
         * 当发生未处理的异常时，系统会调用此函数
         * 该函数生成异常报告并调用之前的异常过滤器
         *
         * @param pExceptionInfo 异常信息指针
         * @return LONG 异常处理结果代码
         */
        static LONG WINAPI WheatyUnhandledExceptionFilter(
            PEXCEPTION_POINTERS pExceptionInfo);

        /**
         * @brief CRT无效参数处理器
         *
         * 当CRT函数检测到无效参数时调用此处理器
         * 例如：printf格式化字符串错误等
         *
         * @param expression 触发错误的表达式
         * @param function 包含错误的函数名
         * @param file 源文件名
         * @param line 行号
         * @param pReserved 保留参数
         */
        static void __cdecl WheatyCrtHandler(wchar_t const* expression, wchar_t const* function, wchar_t const* file, unsigned int line, uintptr_t pReserved);

        /**
         * @brief 打印所有线程的调用栈
         *
         * 枚举进程中的所有线程并输出它们的调用栈信息
         *
         * @param bWriteVariables 是否输出局部变量值
         */
        static void printTracesForAllThreads(bool);

    private:
        /**
         * @brief 生成异常报告
         *
         * 提取异常信息并生成详细的崩溃报告文件
         *
         * @param pExceptionInfo 异常信息指针
         */
        static void GenerateExceptionReport(PEXCEPTION_POINTERS pExceptionInfo);

        /**
         * @brief 打印系统信息
         *
         * 输出操作系统版本、CPU信息等系统信息
         */
        static void PrintSystemInfo();

        /**
         * @brief 获取Windows版本
         *
         * @param szVersion 版本字符串缓冲区
         * @param cntMax 缓冲区最大长度
         * @return BOOL 成功返回TRUE，失败返回FALSE
         */
        static BOOL _GetWindowsVersion(TCHAR* szVersion, DWORD cntMax);

        /**
         * @brief 通过WMI获取Windows版本
         *
         * 使用WMI查询获取更详细的Windows版本信息
         *
         * @param szVersion 版本字符串缓冲区
         * @param cntMax 缓冲区最大长度
         * @return BOOL 成功返回TRUE，失败返回FALSE
         */
        static BOOL _GetWindowsVersionFromWMI(TCHAR* szVersion, DWORD cntMax);

        /**
         * @brief 获取处理器名称
         *
         * 从注册表读取CPU的名称信息
         *
         * @param sProcessorName 处理器名称缓冲区
         * @param maxcount 缓冲区最大长度
         * @return BOOL 成功返回TRUE，失败返回FALSE
         */
        static BOOL _GetProcessorName(TCHAR* sProcessorName, DWORD maxcount);

        /**
         * @brief 获取异常代码对应的描述字符串
         *
         * @param dwCode 异常代码
         * @return LPCTSTR 异常描述字符串
         */
        static LPCTSTR GetExceptionString(DWORD dwCode);

        /**
         * @brief 获取逻辑地址
         *
         * 将虚拟地址转换为模块名和节区偏移
         *
         * @param addr 虚拟地址
         * @param szModule 模块名缓冲区
         * @param len 缓冲区长度
         * @param section 输出节区号
         * @param offset 输出偏移量
         * @return BOOL 成功返回TRUE，失败返回FALSE
         */
        static BOOL GetLogicalAddress(PVOID addr, PTSTR szModule, DWORD len,
            DWORD& section, DWORD_PTR& offset);

        /**
         * @brief 输出堆栈详细信息
         *
         * 遍历调用堆栈并输出每一帧的信息，包括函数名、源文件、行号以及局部变量
         *
         * @param pContext CPU上下文
         * @param bWriteVariables 是否输出局部变量
         * @param pThreadHandle 线程句柄
         */
        static void WriteStackDetails(PCONTEXT pContext, bool bWriteVariables, HANDLE pThreadHandle);

        /**
         * @struct EnumerateSymbolsCallbackContext
         * @brief 符号枚举回调上下文
         *
         * 在枚举符号时传递必要的上下文信息
         */
        struct EnumerateSymbolsCallbackContext
        {
            LPSTACKFRAME64 sf;  ///< 堆栈帧指针
            PCONTEXT context;   ///< CPU上下文
        };

        /**
         * @brief 符号枚举回调函数
         *
         * 由SymEnumSymbols调用的回调函数，用于处理每个找到的符号
         *
         * @param pSymInfo 符号信息
         * @param SymbolSize 符号大小
         * @param UserContext 用户上下文
         * @return BOOL 继续枚举返回TRUE，停止返回FALSE
         */
        static BOOL CALLBACK EnumerateSymbolsCallback(PSYMBOL_INFO, ULONG, PVOID);

        /**
         * @brief 格式化符号值
         *
         * 提取并格式化符号的值
         *
         * @param pSymInfo 符号信息
         * @param ctx 回调上下文
         * @return bool 成功返回true，失败返回false
         */
        static bool FormatSymbolValue(PSYMBOL_INFO, EnumerateSymbolsCallbackContext*);

        /**
         * @brief 输出类型索引信息
         *
         * 根据类型索引递归输出类型的详细信息
         *
         * @param modBase 模块基址
         * @param dwTypeIndex 类型索引
         * @param offset 偏移量
         * @param bIsPtr 是否为指针
         * @param name 变量名
         * @param suffix 后缀
         * @param bFunction 是否为函数参数
         * @param bArray 是否为数组
         */
        static void DumpTypeIndex(DWORD64, DWORD, DWORD_PTR, bool &, char const*, char const*, bool, bool);

        /**
         * @brief 格式化输出值
         *
         * 根据基本类型格式化内存中的值
         *
         * @param pszCurrBuffer 输出缓冲区
         * @param basicType 基本类型
         * @param length 数据长度
         * @param pAddress 数据地址
         * @param bufferSize 缓冲区大小
         * @param countOverride 元素数量覆盖值（用于数组）
         */
        static void FormatOutputValue(char * pszCurrBuffer, BasicType basicType, DWORD64 length, PVOID pAddress, size_t bufferSize, size_t countOverride = 0);

        /**
         * @brief 获取基本类型
         *
         * 根据类型索引查找对应的基本类型
         *
         * @param typeIndex 类型索引
         * @param modBase 模块基址
         * @return BasicType 基本类型枚举值
         */
        static BasicType GetBasicType(DWORD typeIndex, DWORD64 modBase);

        /**
         * @brief 安全解引用指针
         *
         * 尝试读取指针指向的内存，如果指针无效则返回0
         *
         * @param address 要解引用的地址
         * @return DWORD_PTR 指向的值，或0（如果指针无效）
         */
        static DWORD_PTR DereferenceUnsafePointer(DWORD_PTR address);

        /**
         * @brief 日志输出函数
         *
         * 将格式化的文本写入报告文件
         *
         * @param format 格式化字符串
         * @param ... 可变参数
         * @return int 写入的字符数
         */
        static int __cdecl Log(const TCHAR * format, ...);

        /**
         * @brief 存储符号
         *
         * 将符号添加到已处理符号集合中
         *
         * @param type 符号类型
         * @param offset 符号偏移量
         * @return bool 成功添加返回true，符号已存在返回false
         */
        static bool StoreSymbol(DWORD type , DWORD_PTR offset);

        /**
         * @brief 清空符号集合
         *
         * 清除所有已存储的符号信息
         */
        static void ClearSymbols();

        /**
         * @brief 获取整数寄存器值
         *
         * 根据寄存器ID从CPU上下文中读取寄存器的值
         *
         * @param context CPU上下文
         * @param registerId 寄存器ID
         * @return Optional<DWORD_PTR> 寄存器值，如果寄存器不存在则返回空
         */
        static Optional<DWORD_PTR> GetIntegerRegisterValue(PCONTEXT context, ULONG registerId);

        /**
         * @brief 推入符号详情
         *
         * 将当前符号详情压入栈，用于构建层次化输出
         */
        static void PushSymbolDetail();

        /**
         * @brief 弹出符号详情
         *
         * 从栈中弹出符号详情
         */
        static void PopSymbolDetail();

        /**
         * @brief 打印符号详情
         *
         * 输出当前符号的详细信息
         */
        static void PrintSymbolDetail();

        // Variables used by the class - 类使用的静态变量
        static TCHAR m_szLogFileName[MAX_PATH];                ///< 日志文件名
        static TCHAR m_szDumpFileName[MAX_PATH];               ///< 转储文件名
        static LPTOP_LEVEL_EXCEPTION_FILTER m_previousFilter;  ///< 之前的异常过滤器
        static _invalid_parameter_handler m_previousCrtHandler;///< 之前的CRT处理器
        static FILE* m_hReportFile;                            ///< 报告文件句柄
        static HANDLE m_hDumpFile;                             ///< 转储文件句柄
        static HANDLE m_hProcess;                              ///< 当前进程句柄
        static SymbolPairs symbols;                            ///< 已处理的符号集合
        static std::stack<SymbolDetail> symbolDetails;         ///< 符号详情栈
        static bool alreadyCrashed;                            ///< 是否已经崩溃（防止递归）
        static std::mutex alreadyCrashedLock;                  ///< 崩溃状态锁

        /**
         * @typedef pRtlGetVersion
         * @brief RtlGetVersion函数指针类型
         *
         * 用于获取真实的Windows版本信息
         */
        typedef NTSTATUS(NTAPI* pRtlGetVersion)(PRTL_OSVERSIONINFOW lpVersionInformation);
        static pRtlGetVersion RtlGetVersion;                   ///< RtlGetVersion函数指针

};

/**
 * @brief 全局异常报告实例
 *
 * 程序启动时创建此全局实例，自动安装异常处理器
 */
extern WheatyExceptionReport g_WheatyExceptionReport;
#endif                                                      // _WHEATYEXCEPTIONREPORT_

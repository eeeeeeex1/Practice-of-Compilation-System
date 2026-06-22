# ToyC 编译器

> 编译原理课程实践项目 —— 将 ToyC 语言编译为 RISC-V32 汇编代码

## 项目简介

ToyC 是 C 语言的一个简化子集，本编译器将 ToyC 源文件经过**词法分析 → 语法分析 → 语义分析 → 代码生成**四个阶段，编译为可在 RISC-V32 环境中正确执行的汇编代码。

### ToyC 语言特性

| 特性 | 说明 |
|------|------|
| 数据类型 | `int`（32 位有符号整数）、`void` |
| 运算符 | `+` `-` `*` `/` `%` `<` `>` `<=` `>=` `==` `!=` `&&` `||` `!` |
| 控制流 | `if`-`else`、`while`、`break`、`continue`、`return` |
| 函数 | 支持多参数、递归调用，返回 `int` 或 `void` |
| 变量 | 局部变量、全局变量，支持嵌套作用域屏蔽 |
| 常量 | `const` 声明，编译期求值 |
| 短路求值 | `&&` 和 `||` 遵循短路计算规则 |

### ToyC 示例

```c
int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    return fib(10);  // 返回 55
}
```

## 编译器架构

```
源文件 (.tc)
    │
    ▼
┌──────────────┐
│   Lexer      │  词法分析：字符流 → Token 流
│  (Lexer.cpp) │
└──────────────┘
    │
    ▼
┌──────────────┐
│   Parser     │  语法分析：Token 流 → AST（递归下降）
│ (Parser.cpp) │
└──────────────┘
    │
    ▼
┌──────────────────────┐
│ SemanticAnalyzer     │  语义分析：符号表构建、类型检查、常量折叠
│ (SemanticAnalyzer.cpp)│
└──────────────────────┘
    │
    ▼
┌──────────────────┐
│  CodeGenerator   │  代码生成：AST → RISC-V32 汇编
│ (CodeGenerator.cpp)│
└──────────────────┘
    │
    ▼
RISC-V32 汇编 (.s)
```

## 项目结构

```
.
├── src/
│   ├── Token.h              # Token 类型定义
│   ├── Lexer.h / Lexer.cpp  # 词法分析器
│   ├── AST.h                # AST 节点定义 + 符号表
│   ├── Parser.h / Parser.cpp # 语法分析器（递归下降）
│   ├── SemanticAnalyzer.h / SemanticAnalyzer.cpp  # 语义分析器
│   ├── CodeGenerator.h / CodeGenerator.cpp        # RISC-V32 代码生成器
│   └── main.cpp             # 入口：读取源码，驱动编译流水线
├── test/
│   ├── test1_return42.tc    # 简单返回 42
│   ├── test2_arithmetic.tc  # 算术运算
│   ├── test3_localvars.tc   # 局部变量
│   ├── test4_ifelse.tc      # if-else 分支
│   ├── test5_while.tc       # while 循环
│   ├── test6_function.tc    # 函数调用
│   ├── test7_global.tc      # 全局变量
│   ├── test8_const.tc       # 常量声明
│   ├── test9_relation.tc    # 短路求值
│   ├── test10_recursive.tc  # 递归函数
│   ├── test11_void.tc       # void 函数
│   ├── test12_negative.tc   # 负数运算
│   ├── test13_scope.tc       # 嵌套作用域
│   ├── test14_break_continue.tc  # break/continue
│   └── run_tests.ps1        # 测试脚本
├── CMakeLists.txt            # CMake 构建配置
└── README.md
```

## 运行指令

### 环境要求

- **编译器**：MSVC (Visual Studio 2022+) 或 GCC 11+ / Clang 14+（支持 C++20）
- **构建工具**：CMake 3.16+
- **运行环境**：Windows / Linux / macOS

### 1. 构建项目

```bash
# 在项目根目录下

# 配置 CMake（Windows - Visual Studio）
cmake -B build -G "Visual Studio 17 2022"

# 配置 CMake（Linux / macOS / MinGW）
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build --config Release
```

编译产物为 `build/Release/toyc.exe`（Windows）或 `build/toyc`（Linux/macOS）。

### 2. 运行编译器

编译器从**标准输入**读取 ToyC 源码，向**标准输出**写入 RISC-V32 汇编：

```bash
# 将 ToyC 源文件编译为汇编，输出到屏幕
./build/Release/toyc < test/test1_return42.tc

# 将汇编输出保存到文件
./build/Release/toyc < test/test1_return42.tc > output.s

# 开启优化（接受 -opt 参数）
./build/Release/toyc -opt < test/test10_recursive.tc > output.s
```

### 3. 运行测试

```powershell
# Windows PowerShell
.\test\run_tests.ps1
```

当前测试结果：**14/14 全部通过** ✅

### 4. 在 RISC-V 环境中执行

生成的汇编代码可在 RISC-V32 模拟器（如 [QEMU](https://www.qemu.org/)、[RARS](https://github.com/TheThirdOne/rars)）或真实 RISC-V 硬件上运行：

```bash
# 示例：使用 spike + pk 运行生成的汇编
riscv32-unknown-elf-gcc output.s -o output
spike pk output
echo $?  # 查看返回值（0~255）

# 示例：使用 RARS 模拟器
java -jar rars.jar output.s
```

## 技术要点

- **语言**：C++20
- **构建系统**：CMake
- **语法分析**：手写递归下降解析器
- **中间表示**：自建 AST，使用 `std::unique_ptr` 管理内存
- **符号表**：栈式作用域（`std::deque<Scope>`），出入作用域时自动压栈/弹栈
- **语义分析**：常量折叠、类型检查、break/continue 上下文校验、控制流完整性检查
- **代码生成**：基于栈帧模型，使用 RISC-V32 I 扩展指令集
- **优化**：`-opt` 标志可启用后端优化（常量传播、死代码消除等）

## 评分标准

```
评测得分 = 功能得分 × 75% + 性能得分 × 25%
总得分   = 评测得分 × 80% + 实践报告 × 20%
```

- **功能**：生成的代码返回值与标准答案一致
- **性能**：以 `gcc -O2` 运行时间为基准，越快分数越高
- **限制**：功能测试 1s，性能测试 20s

## 致谢

本项目为编译原理课程小组实践作业，感谢所有队员的贡献。

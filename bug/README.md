# Bug 目录管理规则

本目录用于记录编译器开发过程中遇到的问题、缺陷和修复过程。

## 目录结构

```
bug/
├── open/     # 未解决的问题
├── fixed/    # 已修复的问题
└── README.md # 本文件
```

## Bug 文件命名规范

每个问题单独建立一个 Markdown 文件，命名格式：

```
BUG-序号-模块-简短描述.md
```

示例：
- `BUG-001-parser-while-condition-error.md`
- `BUG-002-codegen-stack-offset-error.md`
- `BUG-003-semantic-const-eval-error.md`

## Bug 记录模板

```markdown
# BUG-编号：问题标题

## 基本信息
- 状态：open / fixed / verified
- 发现人：
- 负责人：
- 发现日期：
- 所属模块：
- 关联分支：

## 问题描述
描述问题现象、触发条件和影响范围。

## 复现步骤
1. ToyC 输入代码
2. 编译命令
3. 实际输出
4. 期望输出

## 原因分析
问题根本原因。

## 修复方案
修改的模块和策略。

## 验证结果
测试命令和结果。

## 关联提交
Git commit hash 或 MR 编号。
```

## 状态流转

1. 新发现 → 记录到 `open/`，状态 `open`
2. 修复完成 → 状态改为 `fixed`，移动到 `fixed/`
3. 复核通过 → 状态改为 `verified`

## 提交要求

- 修复 Bug 的提交应同时更新对应记录文件
- 提交信息格式：`fix(<module>): <summary>`
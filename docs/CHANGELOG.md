# 更新日志

## v1.1.0 — 2026-11-24

**新增：Boris Continuum Complete（BCC）中文支持**

BCC 2025 版开始自带 UTF-8 中文，但 AE 27 对它的处理方式与 Trapcode 一样（老式模块，会把 ANSI 再转一次），所以参数名显示成 `鏍囪瘑缁勶紝`、`鏁堟灉棰勮` 这类乱码。

- 规则表新增 `\borisfx\continuum` → `U2A`（UTF-8 → ANSI），覆盖 `MediaCore\BorisFX\Continuum` 下全部模块；
- 实测 7 个效果（BCC+Glow / Glow Darks / Glow Edges / Blur / Looks / Colorize Glow、以及经典 BCC Blur）共 1473 个含中文的属性名，**100% 可被 CP936 表示**，转换不会中途放弃。

**修复：BCC 的入口名漏钩（`PluginMain` / `main`）**

原来只挂钩 `EntryPointFunc` 和 `EffectMain` 两种导出名。BCC+ 系列 190 个模块用 `PluginMain`，经典 BCC 模块（`BCCBlur.aex`、`BCCColorizeGlow.aex` 等）只导出 `main`，两者都不在原来的识别列表里，导致整片 BCC 从未进入转换流程——这是第一次加规则后仍然乱码的真正原因。现在 `EntryPointFunc` / `EffectMain` / `PluginMain` / `main` 四种入口名都认。

**新增：外置规则文件**

`AECharsetFixer.rules.txt` 与 `.aex` 放同一目录即可补充/覆盖模块规则，**新增厂商不再需要重新编译**：

```
U2A \厂商\插件目录      # 老式模块 + UTF-8 文本
G2U \厂商\插件目录      # 新式模块 + ANSI 文本
```

优先级：环境变量 > 规则文件 > 内置表。

**其它**

- 编译加 `/utf-8`，消除中文注释引起的 C4819 警告；
- `sscanf_s` 缓冲区长度参数显式转 `unsigned int`，消除 C4477；
- 文档补充"怎么判断某个厂商该写哪边"的对照表（看 27 里是 `鏁堟灉` 型怪字还是截断/空白）。

**验证基线**（AE 26 逐条比对，未变）

```
Trapcode 全系、Adobe 渐变/线性擦除、Bang、MB Looks/Colorista、
Universe、Boris BCC、Sapphire、Deep Glow 2、Aquon
22 个效果 · 约 6900 个参数名 · 0 处差异
```

---

## v1.0.0 — 2026-11-18

首个版本。

- 解决 AE 2027 里 Trapcode 参数名乱码：Geo / Form / Particular / Tao / Mir / Lux / Echospace / Horizon 走 `U2A`；
- Shine / Starglow / 3DStroke / SoundKeys 走 `G2U`（它们是"新式模块 + ANSI 文本"）；
- 模块级白名单 + 线程局部作用域门，杜绝波及其它插件（Adobe 自带、Universe、Boris、Sapphire、汉化插件）；
- 不可表示字符一律放弃，绝不写问号；
- 验证：22 个效果、约 6900 个参数名，与 AE 26 零差异。

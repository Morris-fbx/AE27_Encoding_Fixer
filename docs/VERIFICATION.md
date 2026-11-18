# 验证方法与本机结果

## 思路

"看起来不乱码"不是标准。真正的标准是：**AE 27 显示的东西，应该和 AE 26 显示的一模一样**。
AE 26 在用户这台机器上是正常的基准（用原版 AE-UTF-8-Fixer，GBK → UTF-8 方向）。

所以做法是：同一份探针脚本，在两个版本里各跑一遍，把每个效果暴露出来的每个属性名抓下来，逐条 diff。

## 具体步骤

1. 打开 `tools/family_probe.jsx`，按需要改 `PROBE_LIST`（每项是 `['标签', 'matchName']`）；
2. 在 **AE 26** 里运行它 → 得到 `ae_probe.json`，重命名成 `probe_26.json`；
3. 在 **AE 27**（装了本插件）里运行它 → 得到 `probe_27.json`；
4. 比对：

   ```bash
   python tools/compare_probe.py probe_26.json probe_27.json --verbose
   ```

   退出码 0 = 全部一致。

探针只做一件事：新建合成 → 新建纯色层 → 逐个 `addProperty(matchName)` → 遍历
`property(i).name`（含一层子分组）→ 移除效果。不改工程内容（`beginUndoGroup` 包着，也可以直接不保存）。

## 本机结果（2026-11-18）

环境：Windows 11 26200 / AE 2026 26.5.0.89 / AE 2027 27.0.0.78 / 系统 ANSI 代码页 936。

```
Adobe GradientWipe         9 names    OK
Adobe LinearWipe           7 names    OK
Trapcode 3DStroke         94 names    OK
Trapcode Shine            61 names    OK
Trapcode Starglow         71 names    OK
Trapcode Geo             422 names    OK
Trapcode Form           3022 names    OK
Trapcode Particular     2008 names    OK
Trapcode Tao             288 names    OK
Trapcode Mir             169 names    OK
Trapcode Lux              32 names    OK
Trapcode SoundKeys        68 names    OK
Trapcode Echospace        28 names    OK
Trapcode Horizon          75 names    OK
RG VFX Bang               96 names    OK
MB Looks                   9 names    OK
MB Colorista              52 names    OK
Universe ChromaticGlow    56 names    OK
Boris BCC3Blur            90 names    OK
Sapphire S_Glow           58 names    OK
PluginEverything DeepGlow2 162 names  OK
Aquon                     45 names    OK

22 effects, 6922 names, 0 with differences
```

## 两个关键的反例（说明为什么方向不能统一）

| 反例 | 现象 | 原因 |
|---|---|---|
| 早期版本把 **Adobe 渐变擦除** 也转了 | 参数名整排空白 | Adobe 自带效果是"新式"（UTF-8 直通），不需要转 |
| 早期版本把 **Universe** 也转了 | 参数名整排空白 | `Red Giant Universe\...` 匹配了太宽的 `\red giant`，Universe 同样是"新式" |
| 早期版本漏了 **Shine / Starglow / 3DStroke / SoundKeys** | `路径` → `·`、`预设` → `Ԥ`（截断） | 它们是"新式 + GBK"，需要反方向（ANSI → UTF-8） |

这三条就是"按厂商白名单"改成"按模块 + 按方向"的直接原因。

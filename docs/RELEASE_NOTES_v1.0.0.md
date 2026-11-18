# v1.0.0 · AE 2027 中文乱码修复器

让 After Effects **2027** 里红巨星 / Trapcode 效果的中文参数名，恢复成和 **AE 2026** 完全一致。

## 下载

`!00_AE27CharsetFixer.aex`（约 938 KB）——只需要这一个文件。

## 安装

1. 完全退出 After Effects 2027；
2. 把 `!00_AE27CharsetFixer.aex` 放进：

   ```
   <AE 2027 安装目录>\Support Files\Plug-ins\
   ```

   例如 `E:\AE\Adobe After Effects 2027\Support Files\Plug-ins\`
3. 启动 AE 27。

卸载就是删掉这个文件，不留任何残留（不写注册表、不写配置）。

## 修好了什么

| 效果 | 修复前 | 修复后 |
|---|---|---|
| Trapcode Geo / Form / Particular / Tao / Mir / Lux / Echospace / Horizon | `鐎瑰簲` 类乱码 | 正常中文 |
| Trapcode Shine / Starglow / 3DStroke / SoundKeys | `Ԥ`、`ͨ`（截断） | 正常中文 |

## 没碰什么（重要）

以下一律**不挂钩、不转换**：Adobe 自带效果、Boris FX、Sapphire、Red Giant Universe、Magic Bullet、以及所有汉化插件（Aquon、Deep Glow 2 等）。

如果你之前装过别的"UTF-8 修复"插件，请先删掉，两个同时存在会重复转换。

## 验证

同一份探针脚本在 AE 26 与 AE 27 各跑一遍，逐条比对 `property(i).name`：

```
22 个效果 · 6922 个参数名 · 0 处差异
```

覆盖 Trapcode 全系、Adobe 渐变/线性擦除、Bang、MB Looks/Colorista、Universe、Boris BCC、Sapphire、Deep Glow 2、Aquon。

## 已知限制

- 只处理参数名/下拉项/复选项/滑块描述。红巨星"未授权"的蓝色链接文字由 Maxon 运行库绘制，插件够不到——**授权正常后它就不显示了**；
- **不要装到 AE 2026 上**：26 是 UTF-8 宿主，方向相反，请用原版 [AE-UTF8-Fixer](https://github.com/AdoreLowen/AE-UTF8-Fixer)；
- 白名单按模块路径匹配，红巨星以后若改目录结构，需要更新源码里的 `kRules`。

## 系统要求

- Windows 10/11 x64
- After Effects 2027（27.0.x）
- 系统 ANSI 代码页为中文（936），即中文版 Windows

## 许可

MIT。派生自 [AdoreLowen/AE-UTF8-Fixer](https://github.com/AdoreLowen/AE-UTF8-Fixer)（MIT），静态链接 [MinHook](https://github.com/TsudaKageyu/minhook)（BSD-2）。详见仓库中的 `LICENSE` 与 `NOTICE.md`。

---

**English**：AE 2027 Charset Fixer v1.0.0 — restores Chinese parameter names for Maxon/Trapcode effects in After Effects 2027. Drop `!00_AE27CharsetFixer.aex` into `Support Files\Plug-ins\` and restart. Do **not** install on AE 2026. Verified: 22 effects, 6922 parameter names, zero differences against AE 26.

# Task 1 报告：统一配置模型并迁移旧行号键

## 实现内容

- `Configuration` 改为单一 `lineNumbersVisible_` 字段，默认值为 `true`。
- 新增 `lineNumbersVisible()` / `setLineNumbersVisible()` API。
- 保留 `mainLineNumbersVisible()`、`filteredLineNumbersVisible()`、`setMainLineNumbersVisible()`、`setFilteredLineNumbersVisible()` 四个直接映射到单字段的临时兼容 wrapper，供 Task 2 迁移调用点。
- `retrieveFromStorage()` 实现新键优先；无新键时按两个旧键的 OR 规则迁移，单旧键直接采用，全部缺失默认 `true`，并写入新键、删除旧键。
- `saveToStorage()` 只写入新键并清理两个旧键。
- 新增真实临时 INI/QSettings 数据驱动测试，覆盖旧键四种布尔组合、任一旧键、缺失默认、新键覆盖冲突旧键和 setter 保存收敛。

## 修改文件

- `src/settings/include/configuration.h`
- `src/settings/src/configuration.cpp`
- `tests/ui2/CMakeLists.txt`
- `tests/ui2/linenumberconfigurationtest.cpp`

## TDD RED

命令：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_line_number_configuration_test --parallel 8
```

预期失败且实际失败：编译器报 `Configuration` 没有 `lineNumbersVisible` 和 `setLineNumbersVisible` 成员（C2039，共 4 处）。这是测试针对生产 API 缺失的预期 RED，而非测试拼写或链接错误。

## TDD GREEN

同一目标构建命令退出码为 0，生成 `zzlogg_line_number_configuration_test.exe`。

```text
zzlogg_line_number_configuration_test.vcxproj -> .../output/RelWithDebInfo/zzlogg_line_number_configuration_test.exe
EXIT=0
```

定向测试：

```powershell
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.line_number_configuration$' --output-on-failure
```

结果：`100% tests passed out of 1`，退出码 0。

## 完整验证

- 完整构建：`cmake --build --preset windows-vs2026-ui-relwithdebinfo --parallel 8`，退出码 0，生成 `ZzLogg.exe`。
- 全量 CTest：`ctest --test-dir out/ui-vs -C RelWithDebInfo --output-on-failure`，`100% tests passed out of 54`，总耗时 38.48 秒，退出码 0。
- `git diff --check` 无错误。

## 自审和疑虑

- 生产代码不再保留两个旧字段；四个旧方法仅作为 Task 2 前的临时兼容 wrapper。
- 迁移逻辑不依赖旧字段默认值，并在读取和保存两条路径清理旧键。
- 未修改计划、SDD 账本或任务范围外文件，也未触碰主工作区的 `serach.png` / `serach.svg`。
- 无已知疑虑。

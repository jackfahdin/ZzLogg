# P12 首轮：Qt 正则复用审计与预筛选复核优化

用户已选择检查现有 Qt 正则复用／JIT，优先减少维护复杂度；本轮按此授权内联执行，本地 master 中文提交，不推送。

普通 DefaultRegularExpressionMatcher 在每个 matcher 中保存 QRegularExpression，逐行复用。本机 Qt 6.11.1 在惰性编译时自动尝试 JIT；显式 optimize() 主要改变发生时机。HsPrefilterMatcher 则每次候选命中都从模式字符串重新构造 Qt 正则，并重复解码同一行。

## 实现方案

采用局部复用：HsPrefilterMatcher 构造时保存各模式的 QRegularExpression，由首次需要复核的匹配触发编译。每次调用只在有候选时解码，多个候选共享同一 QString。每个 worker 仍拥有独立 matcher，不新增全局缓存或锁。

其他路径：对普通 Qt 路径强制 optimize() 尚无吞吐收益依据；直接接 PCRE2-8 会扩大编码和偏移语义范围；本轮均不实施。

## 验收和边界

使用实际 Vectorscan 预筛选 matcher，固定结果验证环视、命中及假阳性、空行、中文、NUL、多模式、重复调用及独立 matcher 生命周期。保留 Qt-only 构建；异常 UTF-8 不外推为两端普遍等价。

使用显式 GDB 诊断确认预热后重复匹配不再从字符串构造 Qt 正则，先证明旧实现失败。诊断仅在工具中，不进入生产类和默认 CI。

在同一工具源码下保留前后二进制，对 Android 模拟日志核对完整命中位图，分别记录构建、首次行、首次扫描和热扫描，包含 Qt 同实现控制。性能只描述组件及本机条件，不承诺整应用／所有平台加速。

默认完整构建／测试、Python CI、加速后端契约及独立审查。无新增第三方库，不改正则语法、路由、高亮、取消或 JIT 环境开关。P12 直接 UTF-8 后端评估仍属后续工作。

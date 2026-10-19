# 用例 12 file-in-use-upgrade（CI 不可跑——固定 SKIP）
# 跳过原因：依赖用例 08 的真实升级链——需活协调者驱动的真实升级事务中
# ZzLogg.exe 仍被占用（文件锁）的场景，CI 无法构造。
function Invoke-Case_12_file_in_use_upgrade {
    param($Context)

    return (Skip-Case '依赖用例 08 的真实升级链（升级事务中目标文件被占用场景），CI 无法构造活协调者会话；按 VM 手册 §2.5 在隔离环境手工执行')
}

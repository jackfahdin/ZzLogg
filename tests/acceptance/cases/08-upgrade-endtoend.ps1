# 用例 08 upgrade-endtoend（CI 不可跑——固定 SKIP）
# 跳过原因：需协调者生产链路真实凭据 + 旧版应用，CI 无法构造活协调者身份。
function Invoke-Case_08_upgrade_endtoend {
    param($Context)

    return (Skip-Case '需协调者生产链路真实凭据 + 旧版应用，CI 无法构造活协调者身份；按 VM 手册 §2.1 在隔离环境手工执行')
}

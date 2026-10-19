# 用例 10 multi-admin-account（CI 不可跑——固定 SKIP）
# 跳过原因：需第二个本地管理员账户的交互登录会话，验证跨管理员账户的
# 升级/恢复授权与 ACL 主体行为，CI 单账户无人值守环境无法构造。
function Invoke-Case_10_multi_admin_account {
    param($Context)

    return (Skip-Case '需第二个管理员账户的交互会话，CI 单账户无人值守环境无法构造；按 VM 手册 §2.3 在隔离环境手工执行')
}

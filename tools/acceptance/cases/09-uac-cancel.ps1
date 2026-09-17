# 用例 09 uac-cancel（CI 不可跑——固定 SKIP）
# 跳过原因：CI 无交互桌面，无法弹真实 UAC 并在 consent 界面点击"取消"
# （ERROR_CANCELLED → Cancelled 映射需真实提权链路）。
function Invoke-Case_09_uac_cancel {
    param($Context)

    return (Skip-Case 'CI 无交互桌面，无法弹真实 UAC 并走用户取消路径；按 VM 手册 §2.2 在隔离环境手工执行')
}

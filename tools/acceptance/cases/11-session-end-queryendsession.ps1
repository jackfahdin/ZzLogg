# 用例 11 session-end-queryendsession（CI 不可跑——固定 SKIP）
# 跳过原因：需真实关机/注销交互，观察 Prepared 窗口期内 WM_QUERYENDSESSION
# 否决行为及窗口期结束后的恢复（协议文档评估结论：有界且可接受，真实交互
# 抽查列入阶段 4 验收）。
function Invoke-Case_11_session_end_queryendsession {
    param($Context)

    return (Skip-Case '需真实关机/注销交互（Prepared 期 WM_QUERYENDSESSION 否决抽查），CI 无法触发真实会话结束；按 VM 手册 §2.4 在隔离环境手工执行')
}

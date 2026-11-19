set pagination off
set breakpoint pending on
set debuginfod enabled off
python
import gdb
active = False
started = False
finished = False
calls = 0
warmup_calls = 0
class Constructor(gdb.Breakpoint):
    def stop(self):
        global calls, warmup_calls
        if active:
            calls += 1
        elif not started:
            warmup_calls += 1
        return False
class Begin(gdb.Breakpoint):
    def stop(self):
        global active, started
        active = True
        started = True
        return False
class End(gdb.Breakpoint):
    def stop(self):
        global active, finished
        active = False
        finished = True
        return False
Constructor('QRegularExpression::QRegularExpression(QString const&, QFlags<QRegularExpression::PatternOption>)')
Begin('qt_regex_probe_begin')
End('qt_regex_probe_end')
gdb.execute('run')
print('Hot-path Qt pattern constructions:', calls)
print('Observed warmup Qt pattern constructions:', warmup_calls)
code = gdb.parse_and_eval('$_exitcode')
if not started or not finished or code != 0 or calls != 0 or warmup_calls == 0:
    gdb.execute('quit 1')
end

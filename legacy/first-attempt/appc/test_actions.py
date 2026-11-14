# Action/sequence test (Python 1.5.2). Script functions return 0 (done) or 1 (still open).
import sys
import Appc
import App

Appc.engine_init()
log = []

def step(pAction, name, ret):
    log.append(name)
    return ret

def finish_later(pAction, name):
    log.append(name + ' (open)')
    return 1

# 1. sequence with delays: A at once, B 1.0 after A, C 2.0 after B, D (null anchor) at 0.5
seq = App.TGSequence_Create()
a = App.TGScriptAction_Create(__name__, 'step', 'A', 0)
b = App.TGScriptAction_Create(__name__, 'step', 'B', 0)
c = App.TGScriptAction_Create(__name__, 'step', 'C', 0)
d = App.TGScriptAction_Create(__name__, 'step', 'D', 0)
seq.AppendAction(a, 0.0)
seq.AppendAction(b, 1.0)
seq.AppendAction(c, 2.0)
seq.AddAction(d, App.TGAction_CreateNull(), 0.5)
print 'actions in sequence:', seq.GetNumActions()
done = []
ev = App.TGIntEvent_Create()
ET = App.UtopiaModule_GetNextEventType()
ev.SetEventType(ET)
seq.AddCompletedEvent(ev)
def on_done(pObject, pEvent):
    done.append(1)
App.g_kEventManager.AddBroadcastPythonFuncHandler(ET, App.g_kTGActionManager, __name__ + '.on_done')
seq.Play()
print 'after Play:', log
t = 0.0
while t < 5.0:
    t = t + 0.25
    Appc.engine_tick(t, t)
    if log != getattr(sys, 'lastlog', None):
        print 't=%.2f' % t, log
        sys.lastlog = log[:]
print 'sequence done event fired:', len(done)

# 2. an open script action completes only when the script says so
log[:] = []
seq2 = App.TGSequence_Create()
x = App.TGScriptAction_Create(__name__, 'finish_later', 'X')
y = App.TGScriptAction_Create(__name__, 'step', 'Y', 0)
seq2.AppendAction(x, 0.0)
seq2.AppendAction(y, 0.0)
seq2.Play()
Appc.engine_tick(10.0, 10.0)
print 'X open, Y waits:', log
x.Completed()
Appc.engine_tick(10.5, 10.5)
print 'after X.Completed():', log

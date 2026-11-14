# Event system test (Python 1.5.2 syntax). Run with engine_init/engine_tick from the host.
import Appc
import App
import sys

Appc.engine_init()
log = []

def broadcast_any(pObject, pEvent):
    log.append(('broadcast_any', pEvent.GetEventType(), pEvent.GetInt()))

def inst1(pObject, pEvent):
    log.append(('inst1', pEvent.GetEventType()))

def inst2(pObject, pEvent):
    log.append(('inst2', pEvent.GetEventType()))
    pObject.CallNextHandler(pEvent)

ET = App.UtopiaModule_GetNextEventType()
print 'event type', ET
w = App.TGPythonInstanceWrapper_Create()
w.AddPythonFuncHandlerForInstance(ET, __name__ + '.inst1')
w.AddPythonFuncHandlerForInstance(ET, __name__ + '.inst2')    # newest first: inst2 runs first and chains to inst1
App.g_kEventManager.AddBroadcastPythonFuncHandler(ET, w, __name__ + '.broadcast_any')

e = App.TGIntEvent_Create()
e.SetEventType(ET)
e.SetInt(42)
e.SetDestination(w)
App.g_kEventManager.AddEvent(e)
print 'queued; log so far', log
Appc.engine_run_events()
print 'after drain:'
for l in log: print '  ', l

# timer: fires every 1.0 s from t=1.0 for 3.5 s
log[:] = []
t = App.TGTimer_Create()
te = App.TGIntEvent_Create()
te.SetEventType(ET)
te.SetInt(7)
t.SetEvent(te)
t.SetTimerStart(1.0)
t.SetDelay(1.0)
t.SetDuration(3.5)
App.g_kTimerManager.AddTimer(t)
for now in (0.5, 1.2, 1.7, 2.3, 3.4, 4.7, 5.1):
    Appc.engine_tick(now, now)
    print 'tick', now, 'events', len(log)
print 'timer log:', log

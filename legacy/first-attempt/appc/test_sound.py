# Sound test (Python 1.5.2): slots, volumes, end events, priorities, single shot, fades, music fades. Headless: the audio clock follows engine_tick.
import sys
import Appc
import App

Appc.engine_init()
sm = App.g_kSoundManager
T = [0.0]
def tick(dt):
    T[0] = T[0] + dt
    Appc.engine_tick(T[0], T[0])
def advance(sec):
    n = int(sec / 0.05)
    for i in range(n):
        tick(0.05)

ok = 1
def check(name, cond):
    global ok
    if not cond:
        ok = 0
    print name, (cond and 'ok' or 'FAIL')

tick(0.1)
# 1. load, play, end event
s = App.TGSound_Create('sfx/Weapons/Disruptor.wav', 'Test A', 0)
check('loaded', s.IsLoaded())
check('original status is stopped', s.GetStatus() == 1)
check('duration ~1.28', abs(s.GetDurationSeconds() - 1.28) < 0.05)
got = []
def on_end(pObject, pEvent):
    got.append(pEvent.GetEventType())
ET = App.UtopiaModule_GetNextEventType()
App.g_kEventManager.AddBroadcastPythonFuncHandler(ET, App.g_kTGActionManager, __name__ + '.on_end')
ev = App.TGEvent_Create()
ev.SetEventType(ET)
ev.SetDestination(App.g_kTGActionManager)
slot = s.PlayAndNotify(ev, 1)
check('slot id', slot != 0)
p = sm.GetPlayingSound(slot)
check('playing copy', p is not None and p.GetStatus() == 0)
advance(0.5)
check('still playing at 0.5s', sm.GetPlayingSound(slot) is not None and not got)
advance(1.2)
check('end event after the sound', len(got) == 1)
check('slot released', sm.GetPlayingSound(slot) is None)

# 2. volumes: master * sfx, voice flag, SFX switch, interface
s.SetVolume(0.5)
slot = s.Play(1)
p = sm.GetPlayingSound(slot)
check('copy keeps volume', abs(p.GetVolume() - 0.5) < 0.001)
sm.SetMasterVolume(0.5)
check('master volume stored', abs(sm.GetMasterVolume() - 0.5) < 0.001)
sm.SetMasterVolume(1.0)
sm.SetSFXEnabled(0)
sm.SetSFXEnabled(1)
p.Stop()
check('stop frees the slot', sm.GetPlayingSound(slot) is None)

# 3. priority: fill every slot, a higher priority sound replaces the lowest
n = sm.GetMaxSoundsAtOnce()
print 'max sounds', n
longs = []
slots = []
for i in range(n):
    q = App.TGSound_Create('sfx/Music/Starbase12.mp3', 'Long %d' % i, 2)
    q.SetPriority(0.2 + i * 0.01)
    slots.append(q.Play(1))
check('all slots busy', 0 not in slots)
hi = App.TGSound_Create('sfx/Weapons/Disruptor.wav', 'High', 0)
hi.SetPriority(0.9)
sh = hi.Play(1)
check('high priority replaces', sh != 0 and sm.GetPlayingSound(slots[0]) is None)
lo = App.TGSound_Create('sfx/Weapons/Disruptor.wav', 'Low', 0)
lo.SetPriority(0.1)
check('low priority is refused', lo.Play(1) == 0)
sm.StopAllSounds()
check('stop all', sm.GetPlayingSound(sh) is None)

# 4. single shot sounds are deleted after they play
ss = App.TGSound_Create('sfx/Weapons/Disruptor.wav', 'Once', 0)
ss.SetSingleShot(1)
ss.Play(1)
advance(1.6)
check('single shot deleted', sm.GetSound('Once') is None)

# 5. fade points
f = App.TGSound_Create('sfx/Music/Starbase12.mp3', 'Fader', 2)
f.AddFadePoint(0.0, 1.0)
f.AddFadePoint(1.0, 0.0)
sl = f.Play(1)
advance(0.5)
v = sm.GetPlayingSound(sl).GetVolume()
check('fade interpolates (%.2f)' % v, 0.3 < v < 0.7)
advance(1.0)
check('fade ends at 0', sm.GetPlayingSound(sl).GetVolume() < 0.05)
sm.StopAllSounds()

# 6. music: first music starts at once, then a fade to the next on the bar
mm = App.g_kMusicManager
check('load music', mm.LoadMusic('sfx/Music/Starbase12.mp3', 'MusicA', 2.0) and mm.LoadMusic('sfx/Music/Confident-11a.mp3', 'MusicB', 2.0))
done = []
def on_music_done(pObject, pEvent):
    done.append(1)
App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_MUSIC_DONE, App.g_kTGActionManager, __name__ + '.on_music_done')
check('start music', mm.StartMusic('MusicA', 1))
advance(0.2)
a = sm.GetSound('MusicA')
check('music A plays', a is not None and a.GetStatus() == 1)
n_play = 0
for k in range(1, 80):
    if sm.GetPlayingSound(k) is not None:
        n_play = n_play + 1
check('music playing copy', n_play >= 1)
mm.StartMusic('MusicB', 0)
advance(2.5)
for k in range(1, 200):
    pp = sm.GetPlayingSound(k)
    if pp is not None:
        print 'mid fade:', pp.GetSoundName(), pp.GetVolume(), pp.GetPlayPositionSeconds()
advance(2.5)
playing = []
for k in range(1, 80):
    pp = sm.GetPlayingSound(k)
    if pp is not None:
        playing.append(pp.GetSoundName())
print 'playing music after the switch:', playing
check('B plays, A is faded out', 'MusicB' in playing and 'MusicA' not in playing)
mm.StopMusic()
mm.SetEnabled(0)
check('music disabled', not mm.IsEnabled())
mm.SetEnabled(1)

# 7. sound actions end with the sound
act = App.TGSoundAction_Create('Low', 0, None, 1.0)
seq = App.TGSequence_Create()
seq.AddAction(act)
fin = []
ev2 = App.TGEvent_Create()
ET2 = App.UtopiaModule_GetNextEventType()
ev2.SetEventType(ET2)
seq.AddCompletedEvent(ev2)
def on_seq(pObject, pEvent):
    fin.append(1)
App.g_kEventManager.AddBroadcastPythonFuncHandler(ET2, App.g_kTGActionManager, __name__ + '.on_seq')
ev2.SetDestination(App.g_kTGActionManager)
seq.Play()
advance(0.5)
check('action waits for the sound', not fin)
advance(1.5)
check('action completes after the sound', len(fin) == 1)
# 8. a fanfare fades the music out, plays, and the music comes back
mm.LoadMusic('sfx/exit warp.wav', 'Fan', 2.0)
done[:] = []
mm.StartMusic('MusicA', 1)
advance(3.0)
mm.PlayFanfare('Fan')
advance(0.5)
def playing_names():
    r = []
    for k in range(1, 200):
        pp = sm.GetPlayingSound(k)
        if pp is not None:
            r.append(pp.GetSoundName())
    return r
advance(2.0)
names = playing_names()
print 'during the fanfare:', names
check('fanfare plays', 'Fan' in names)
advance(5.0)
names = playing_names()
print 'after the fanfare:', names
check('music is back after the fanfare', 'MusicA' in names and 'Fan' not in names)
check('music done event for the fanfare', len(done) >= 1)
mm.StopMusic()

# 9. a muted region keeps its sounds from playing
reg = App.TGSoundRegion_Create('MuteTest')
rs = App.TGSound_Create('sfx/Weapons/Disruptor.wav', 'Muted', 0)
reg.AddSound(rs)
reg.SetFilter(1)
check('muted region: no play', rs.Play(1) == 0)
reg.SetFilter(0)
check('region open again: plays', rs.Play(1) != 0)
sm.StopAllSounds()
print ok and 'ALL OK' or 'SOME FAILED'

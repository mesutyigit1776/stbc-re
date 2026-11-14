# 3D sound check (Python 1.5.2): a sound to the right, left and in front of the camera, one after the other. Run with BC_AUDIO_DUMP=/tmp/x.wav and read
# the left/right levels of the dump (port/appc/test_sound3d.sh does that).
import Appc
import App
Appc.engine_init()
T = [0.0]
def advance(sec):
    n = int(sec / 0.05)
    for i in range(n):
        T[0] = T[0] + 0.05
        Appc.engine_tick(T[0], T[0])
pSet = App.SetClass_Create()
App.g_kSetManager.AddSet(pSet, 'SndTest')
cam = App.CameraObjectClass_Create(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 'cam')
pSet.AddCameraToSet(cam, 'cam')
App.g_kSetManager.MakeRenderedSet('SndTest')
advance(0.5)
snd = App.TGSound_Create('sfx/redalert.wav', 'Pan', App.TGSound.LS_3D)
snd.SetMinMaxDistance(5.0, 500.0)
App.TGSoundRegion_GetRegion('SndTest').AddSound(snd)
def shot(x, y, z):
    pt = App.TGPoint3()
    pt.SetXYZ(x, y, z)
    snd.SetPosition(pt)
    snd.Play(1)
    advance(1.5)
shot(20.0, 0.0, 0.0)      # 0.5 s to 2.0 s : right
shot(-20.0, 0.0, 0.0)     # left
shot(0.0, 20.0, 0.0)      # in front
shot(0.0, 400.0, 0.0)     # far in front: quiet
print 'done'

# Headless host: boot like stbc.exe, start a new game (module Maelstrom.Maelstrom), run frames, report what the scripts did.
import sys
import Appc
execfile('../port/appc/host_boot.py')     # same start-up as the boot script (prints its own table)
import App

name = App.TGString()
name.SetString('Test Captain')
App.g_kUtopiaModule.SetCaptainName(name)
App.Game_SetDifficulty(1)
App.g_kVarManager.SetStringVariable('Options', 'EpisodeOverride', 'Episode1')
App.g_kVarManager.SetStringVariable('Options', 'MissionOverride', 'E1M1')
print '--- engine_new_game'
game = Appc.engine_new_game('Maelstrom.Maelstrom')
print 'game:', game
t = 0.0
for i in range(60):
    t = t + 0.1
    Appc.engine_tick(t, t)
g = App.Game_GetCurrentGame()
print 'current game:', g
if g:
    ep = g.GetCurrentEpisode()
    print 'episode:', ep
    if ep:
        m = ep.GetCurrentMission()
        print 'mission:', m, 'script', m and m.GetScript()

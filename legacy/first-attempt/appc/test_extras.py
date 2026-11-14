import App
print 'Mission.GetFriendlyGroup:', App.Mission.GetFriendlyGroup
print 'TGString.GetCString:', App.TGString.GetCString
print 'SetClass.GetNavPoints:', App.SetClass.GetNavPoints
print 'PreprocessingAI.SetPreprocessingMethod:', App.PreprocessingAI.SetPreprocessingMethod
s = App.TGString('x')
print 'GetCString ->', repr(s.GetCString())

-- Prints every event, handy for checking which number each button has.
function OnEvent(event, arg, family)
    if event == "PROFILE_ACTIVATED" then
        EnablePrimaryMouseButtonEvents(true)
    end
    OutputLogMessage("event=%s arg=%s family=%s time=%d\n", event, tostring(arg), tostring(family), GetRunningTime())
end

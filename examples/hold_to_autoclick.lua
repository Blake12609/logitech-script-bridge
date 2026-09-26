-- Hold the forward button (5) to click the left button repeatedly.
function OnEvent(event, arg, family)
    if event == "PROFILE_ACTIVATED" then
        OutputLogMessage("Autoclicker ready - hold mouse button 5\n")
    end
    if event == "MOUSE_BUTTON_PRESSED" and arg == 5 then
        repeat
            PressAndReleaseMouseButton(1)
            Sleep(50)
        until not IsMouseButtonPressed(5)
    end
end

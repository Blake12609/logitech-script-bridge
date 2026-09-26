-- Back button = Ctrl+C, forward button = Ctrl+V
function OnEvent(event, arg, family)
    if event == "MOUSE_BUTTON_PRESSED" and arg == 4 then
        PressAndReleaseKey("lctrl", "c")
    elseif event == "MOUSE_BUTTON_PRESSED" and arg == 5 then
        PressAndReleaseKey("lctrl", "v")
    end
end

-- For Paint (or any drawing app): hold the back button (4) to draw a line to the right.
-- Set "Randomize" in the app (e.g. Min 1, Max 4) for a hand-drawn wobble.
function OnEvent(event, arg)
    if event == "MOUSE_BUTTON_PRESSED" and arg == 4 then
        PressMouseButton(1)
        repeat
            MoveMouseRelative(4, 0)
            Sleep(10)
        until not IsMouseButtonPressed(4)
        ReleaseMouseButton(1)
    end
end

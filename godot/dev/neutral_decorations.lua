-- Allow building over neutral scenery. Merely previewing never destroys anything.
neutralDecorations = neutralDecorations or {enabled=true}
function neutralDecorations.enable(enabled)
    assert(type(enabled)=='boolean', 'Expected true or false')
    neutralDecorations.enabled = enabled
    neutralDecorations.generation = nil
end
bfme.events.onUpdate('neutral-decoration-placement', function()
    local match = bfme.match.getStatus()
    if not match.ready or match.networkGame or match.replay then return end
    if neutralDecorations.generation ~= match.generation then
        bfme.match.setClearNeutralDecorations(neutralDecorations.enabled)
        neutralDecorations.generation = match.generation
    end
end, 500)

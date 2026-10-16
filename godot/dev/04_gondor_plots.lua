-- BFME1 Gondor floors from BFME2Lua's assets/gondor-build-plots archive.
-- Only these seven shared texture names change; snow and fortress floors stay native.
gondorPlots = gondorPlots or {}
local textures = {
    "gbarcheryn_bib", "gbbarracks_bib", "gbarmorybib", "gbbtltwrs_bib",
    "gbstablet", "gbworkshop_bib", "gbfarmt",
}
function gondorPlots.enable(enabled)
    local tickets = {}
    for _, name in ipairs(textures) do
        tickets[#tickets + 1] = bfme.graphics.setTextureOverride(
            name, enabled ~= false and ("res://mods/gondor_plots/" .. name .. ".dds") or nil)
    end
    return tickets
end
gondorPlots.tickets = gondorPlots.enable(true)

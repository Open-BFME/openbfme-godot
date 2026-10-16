// OpenBFME. GPL-3.0. Client-side two-button formation placement.
#pragma once
#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"

// The slot layout is an OpenBFME reconstruction. Orders use the existing
// move-and-orientate messages, so the preview positions are the issued goals.
class FormationPlacement : public MessageTranslator {
public:
 explicit FormationPlacement(HudContext &ctx) : m_ctx(ctx) {}
 MessageDisposition translate(const ClientMessage &msg) override;
 void cancel();
 void frame();
 bool begin(const ICoord2D &pixel, bool controller = false);
 void aim(const ICoord2D &pixel);
 void commit();
 bool consumed() const { return m_consumed; }
private:
 HudContext &m_ctx;
 bool m_left=false,m_right=false,m_consumed=false,m_controller=false;
 Coord3D m_anchor{};
 std::vector<ObjectID> m_selection, m_originalSelection;
};

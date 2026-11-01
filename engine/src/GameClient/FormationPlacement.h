// OpenBFME. GPL-3.0. Client-side two-button formation placement.
#pragma once
#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"

// DONOR FACTS: BFME2 1.06 FormationTranslator WaitForSecondButtonDownStateHandler,
// RVAs 0x431978 / 0x431BDD / 0x432038: defer the first press; the opposite press
// must be within 5 pixels on both axes and strictly less than 500 ms later.
// RVA 0x431C9D starts placement at the FIRST press's pixel.
// TARGET FACT: RotWK attaches FormationTranslator at priority 35 (RW 0x646771).
// Target parity of the donor gesture is unverified (S-4464). The slot layout and
// per-object orders remain an OpenBFME reconstruction, also reported by S-4464.
class FormationPlacement : public MessageTranslator
{
  public:
	explicit FormationPlacement(HudContext &ctx) : m_ctx(ctx)
	{
	}
	MessageDisposition translate(const ClientMessage &msg) override;
	void cancel();
	void frame();
	bool begin(const ICoord2D &pixel, bool controller = false);
	// begin()'s checks without the terrain pick: could a chord pressed at `pixel` start placement now
	bool canStart(const ICoord2D &pixel) const;
	void aim(const ICoord2D &pixel);
	void commit();
	bool consumed() const
	{
		return m_consumed;
	}
	// The gesture is live: placement is active, or a button of a chord that started placement is still held.
	bool gestureLive() const
	{
		return m_ctx.ui.formationActive || (m_consumed && (m_left || m_right));
	}
	static bool withinChordDistance(const ICoord2D &first, const ICoord2D &next);
	static bool isStartOfDrag(const ClientMessage &first, const ClientMessage &next);
	static const char *stopLine();

  private:
	void replayFirstPress(const ClientMessage &before);
	bool eligible(const ICoord2D &pixel, bool &rotatingArc, std::vector<ObjectID> &ids) const;
	HudContext &m_ctx;
	std::unique_ptr<ClientMessage> m_firstPress;
	bool m_left = false, m_right = false, m_consumed = false, m_controller = false;
	Coord3D m_anchor{}, m_end{};
	bool m_rotatingArc = false;
	std::vector<ObjectID> m_selection, m_originalSelection;
};

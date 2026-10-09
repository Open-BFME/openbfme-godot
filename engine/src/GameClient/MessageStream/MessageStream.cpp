// OpenBFME. GPL-3.0.
// See MessageStream.h.

#include "GameClient/MessageStream/MessageStream.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace
{
struct MetaName
{
	const char *name;
	int type;
};
const MetaName kMetaNames[] = {
#define CMSG_META_NAME(n) { #n, CMSG_META_##n },
	CMSG_META_LIST(CMSG_META_NAME)
#undef CMSG_META_NAME
};

bool equalsNoCase(const std::string &a, const char *b)
{
	size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return i == a.size() && b[i] == 0;
}
} // namespace

const char *ClientMessageMetaName(int type)
{
	for (const MetaName &m : kMetaNames)
	{
		if (m.type == type)
		{
			return m.name;
		}
	}
	return "";
}

int ClientMessageMetaType(const std::string &name)
{
	for (const MetaName &m : kMetaNames)
	{
		if (equalsNoCase(name, m.name))
		{
			return m.type;
		}
	}
	return CMSG_INVALID;
}

void MessageStream::attachTranslator(MessageTranslator *translator, int priority)
{
	auto it = m_translators.begin();
	while (it != m_translators.end() && it->priority <= priority)
	{
		++it;
	}
	m_translators.insert(it, { translator, priority });
}

void MessageStream::detachTranslator(MessageTranslator *translator)
{
	m_translators.erase(std::remove_if(m_translators.begin(), m_translators.end(), [&](const Attached &a) { return a.translator == translator; }), m_translators.end());
}

ClientMessage &MessageStream::append(int type)
{
	m_messages.push_back(std::make_unique<ClientMessage>(type));
	return *m_messages.back();
}

ClientMessage &MessageStream::insertAfter(int type, const ClientMessage &after)
{
	for (auto it = m_messages.begin(); it != m_messages.end(); ++it)
	{
		if (it->get() == &after)
		{
			return **m_messages.insert(std::next(it), std::make_unique<ClientMessage>(type));
		}
	}
	throw std::logic_error("MessageStream::insertAfter: the message is not in the stream");
}

std::vector<ClientMessage> MessageStream::propagate()
{
	// Each raw event goes through the complete translator pipeline (with the messages the translators derive from it) before the next event is
	// translated: a later event's translators read the state (selection, mode) that the earlier event's translation changed, so a batch gives the
	// same messages as the same events drained one by one (review HUD-1 r1 #1). Simulation dispatch stays at the logic frame (HudInput::update).
	std::list<std::unique_ptr<ClientMessage>> events;
	events.swap(m_messages);
	std::vector<ClientMessage> out;
	for (auto &event : events)
	{
		m_messages.clear();
		m_messages.push_back(std::move(event));
		for (const Attached &a : m_translators)
		{
			for (auto it = m_messages.begin(); it != m_messages.end();)
			{
				MessageDisposition disp = a.translator->translate(**it);
				auto next = std::next(it);
				if (disp == MessageDisposition::Destroy)
				{
					m_messages.erase(it);
				}
				it = next;
			}
		}
		for (auto &m : m_messages)
		{
			if (m->isLogic())
			{
				m_log.push_back(describe(*m));
			}
			out.push_back(std::move(*m));
		}
		m_messages.clear();
	}
	return out;
}

std::string MessageStream::describe(const ClientMessage &m)
{
	std::string s = GameMessageTypeName(m.type());
	if (s.empty())
	{
		s = "CMSG_" + std::to_string(m.type());
	}
	char buf[160];
	for (size_t i = 0; i < m.argumentCount(); ++i)
	{
		const ClientArg &a = m.arg(i);
		switch (a.kind)
		{
			case ClientArgKind::Integer: snprintf(buf, sizeof buf, " i%d", a.integer); break;
			case ClientArgKind::Real: snprintf(buf, sizeof buf, " r%.4f", (double)a.real); break;
			case ClientArgKind::Boolean: snprintf(buf, sizeof buf, " b%d", a.boolean ? 1 : 0); break;
			case ClientArgKind::ObjectID: snprintf(buf, sizeof buf, " o%u", (unsigned)a.objectID); break;
			case ClientArgKind::Location: snprintf(buf, sizeof buf, " L(%.3f,%.3f,%.3f)", (double)a.location.x, (double)a.location.y, (double)a.location.z); break;
			case ClientArgKind::Pixel: snprintf(buf, sizeof buf, " P(%d,%d)", a.pixel.x, a.pixel.y); break;
			case ClientArgKind::PixelRegion: snprintf(buf, sizeof buf, " R(%d,%d,%d,%d)", a.region.lo.x, a.region.lo.y, a.region.hi.x, a.region.hi.y); break;
		}
		s += buf;
	}
	return s;
}

GameMessage toGameMessage(const ClientMessage &message, int playerIndex)
{
	if (!message.isLogic())
	{
		throw std::logic_error("toGameMessage: " + std::to_string(message.type()) + " is a client message");
	}
	GameMessage out(message.type(), playerIndex);
	for (size_t i = 0; i < message.argumentCount(); ++i)
	{
		const ClientArg &a = message.arg(i);
		switch (a.kind)
		{
			case ClientArgKind::Integer: out.appendIntegerArgument(a.integer); break;
			case ClientArgKind::Real: out.appendRealArgument(a.real); break;
			case ClientArgKind::Boolean: out.appendBooleanArgument(a.boolean); break;
			case ClientArgKind::ObjectID: out.appendObjectIDArgument(a.objectID); break;
			case ClientArgKind::Location: out.appendLocationArgument(a.location); break;
			default: throw std::logic_error("toGameMessage: a pixel argument in a logic message");
		}
	}
	return out;
}

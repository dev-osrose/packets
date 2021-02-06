#include "types.h"

namespace RoseCommon {

void to_json(nlohmann::json& j, const HotbarItem& data) {
	j = nlohmann::json{
		{ "type", data.get_type() },
		{ "slotId", data.get_slotId() },
	};
}

void to_json(nlohmann::json& j, const Skill& data) {
	j = nlohmann::json{
		{ "id", data.get_id() },
		{ "level", data.get_level() },
	};
}

void to_json(nlohmann::json& j, const StatusEffect& data) {
	j = nlohmann::json{
		{ "expired(s)", data.get_expired().count() },
		{ "value", data.get_value() },
		{ "unkown", data.get_unkown() },
		{ "dt(ms)", data.get_dt().count() },
	};
}

} // rosecommon
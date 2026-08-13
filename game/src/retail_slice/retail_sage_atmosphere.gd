class_name RetailSageAtmosphere
extends RefCounted
## Compiled BFME II / RotWK water.ini + weather.ini contract.
##
## Numbers are the retail INI assignments, not render guesses. SkyEnv.tga
## reflection and CloudEffect sky-plane binding stay named unresolved until a
## pack actually carries those textures.

const SCHEMA := "openbfme.sage-atmosphere"
const SCHEMA_VERSION := 0
const WATER_INI_PATH := "data/ini/water.ini"
const WEATHER_INI_PATH := "data/ini/weather.ini"
const GAME_DATA_INI_PATH := "data/ini/gamedata.ini"
const ENVIRONMENT_INI_PATH := "data/ini/environment.ini"

const TIME_OF_DAY_NAMES: Array[String] = ["MORNING", "AFTERNOON", "EVENING", "NIGHT"]
const MAP_WEATHER_NAMES: Array[String] = ["NORMAL", "SNOWY"]
const WEATHER_DATA_NAMES: Array[String] = ["RAINY", "CLOUDYRAINY", "SUNNY", "CLOUDY", "NONE"]
const DEFAULT_TIME_OF_DAY := "AFTERNOON"
const DEFAULT_MAP_WEATHER := "NORMAL"

## WaterSet blocks from water.ini. Vertex colors are RGB8; Diffuse / transparent
## diffuse include alpha.
const WATER_SETS := {
	"MORNING": {
		"sky_texture": "TSCloudWis.tga",
		"water_texture": "TSWater.tga",
		"vertex_color_rgb8": [200, 200, 200],
		"diffuse_rgba8": [175, 175, 175, 255],
		"transparent_diffuse_rgba8": [150, 150, 150, 128],
		"u_scroll_per_ms": 0.002,
		"v_scroll_per_ms": 0.002,
		"sky_texels_per_unit": 0.8,
		"water_repeat_count": 32,
	},
	"AFTERNOON": {
		"sky_texture": "TSCloudWis.tga",
		"water_texture": "TSWater.tga",
		"vertex_color_rgb8": [225, 225, 225],
		"diffuse_rgba8": [185, 185, 185, 255],
		"transparent_diffuse_rgba8": [255, 255, 255, 128],
		"u_scroll_per_ms": 0.002,
		"v_scroll_per_ms": 0.002,
		"sky_texels_per_unit": 0.8,
		"water_repeat_count": 32,
	},
	"EVENING": {
		"sky_texture": "TSCloudSun.tga",
		"water_texture": "TSWater.tga",
		"vertex_color_rgb8": [150, 150, 150],
		"diffuse_rgba8": [225, 225, 225, 255],
		"transparent_diffuse_rgba8": [150, 150, 150, 96],
		"u_scroll_per_ms": 0.002,
		"v_scroll_per_ms": 0.002,
		"sky_texels_per_unit": 0.8,
		"water_repeat_count": 32,
	},
	"NIGHT": {
		"sky_texture": "TSStarFeld.tga",
		"water_texture": "TSWater.tga",
		"vertex_color_rgb8": [255, 255, 255],
		"diffuse_rgba8": [100, 100, 100, 255],
		"transparent_diffuse_rgba8": [255, 255, 255, 128],
		"u_scroll_per_ms": 0.0,
		"v_scroll_per_ms": 0.0,
		"sky_texels_per_unit": 1.6,
		"water_repeat_count": 32,
	},
}

const WATER_TRANSPARENCY := {
	"transparent_water_min_opacity": 1.0,
	"transparent_water_depth": 3.0,
	"river_transparency_multiplier": 1.0,
	"standing_water_color_rgb8": [255, 255, 255],
	"standing_water_texture": "TWWater01.tga",
	"additive_blending": false,
	"radar_water_color_rgb8": [140, 140, 255],
	"reflection_plane_z": 59.0,
	"reflection_on": false,
}

const WEATHER := {
	"snow_enabled": true,
	"is_snowing": false,
	"snow_texture": "EXRainDrop.tga",
	"snow_box_height": 300.0,
	"snow_spacing": 30.0,
	"number_tiles": 4,
	"snow_amplitude": 0.0,
	"snow_frequency_scale_x": 0.0,
	"snow_frequency_scale_y": 0.0,
	"snow_speed": 50.0,
	"snow_point_size": 5.0,
	"snow_min_point_size": 40.0,
	"snow_max_point_size": 80.0,
	"snow_point_sprites": false,
	"snow_quad_size": 10.0,
	"lightning_enabled": false,
	"lightning_factor": [0.5, 1.0],
	"lightning_duration_frames": 50,
	"lightning_chance": 0.05,
	"spell_enabled": true,
	"spell_duration_frames": 500,
	"ramp_control": [0.2, 0.8],
	"ramp_speed": [150.0, 250.0],
	"ramp_spacing": [30.0, 20.0],
	"cloud_texture_size": [660.0, 660.0],
	"cloud_offset_per_second": [-0.012, -0.018],
}

const WEATHER_DATA := {
	"RAINY": {"weather_sound": "RainStereoLoop", "has_lightning": true},
	"CLOUDYRAINY": {"weather_sound": "RainStereoLoop", "has_lightning": true},
	"SUNNY": {"weather_sound": "", "has_lightning": false},
	"CLOUDY": {"weather_sound": "", "has_lightning": false},
	"NONE": {"weather_sound": "", "has_lightning": false},
}

const CLOUD_EFFECT := {
	"cloud_texture": "exdarkclouda.tga",
	"dark_cloud_texture": "exdarkcloudc.tga",
	"darkening_factor_rgb8": [100, 100, 30],
	"darkening_rate_frames": 300,
	"lightening_rate_frames": 100,
	"cloud_scroll_speed": 3.0,
	"lightning_shadows": true,
	"lightning_chance": 0.005,
	"lightning_duration_frames": [10, 50],
	"lightning_intensity": [0.05, 0.4],
	"source_path": ENVIRONMENT_INI_PATH,
}


static func is_time_of_day(name: String) -> bool:
	return TIME_OF_DAY_NAMES.has(name.strip_edges().to_upper())


static func is_map_weather(name: String) -> bool:
	return MAP_WEATHER_NAMES.has(name.strip_edges().to_upper())


static func is_weather_data(name: String) -> bool:
	return WEATHER_DATA_NAMES.has(name.strip_edges().to_upper())


static func normalize_time_of_day(name: String) -> String:
	var folded := name.strip_edges().to_upper()
	return folded if is_time_of_day(folded) else ""


static func normalize_map_weather(name: String) -> String:
	var folded := name.strip_edges().to_upper()
	return folded if is_map_weather(folded) else ""


static func normalize_weather_data(name: String) -> String:
	var folded := name.strip_edges().to_upper()
	return folded if is_weather_data(folded) else ""


static func water_set(time_of_day: String) -> Dictionary:
	var key := normalize_time_of_day(time_of_day)
	if key == "" or not WATER_SETS.has(key):
		return {}
	var row: Dictionary = WATER_SETS[key]
	return row.duplicate(true)


static func weather_data(name: String) -> Dictionary:
	var key := normalize_weather_data(name)
	if key == "" or not WEATHER_DATA.has(key):
		return {}
	var row: Dictionary = WEATHER_DATA[key]
	return row.duplicate(true)


static func water_transparency() -> Dictionary:
	return WATER_TRANSPARENCY.duplicate(true)


static func weather() -> Dictionary:
	return WEATHER.duplicate(true)


static func cloud_effect() -> Dictionary:
	return CLOUD_EFFECT.duplicate(true)


static func color_rgb8(components: Array) -> Color:
	if components.size() < 3:
		return Color.TRANSPARENT
	return Color(
		float(components[0]) / 255.0,
		float(components[1]) / 255.0,
		float(components[2]) / 255.0,
		1.0
	)


static func color_rgba8(components: Array) -> Color:
	if components.size() < 4:
		return Color.TRANSPARENT
	return Color(
		float(components[0]) / 255.0,
		float(components[1]) / 255.0,
		float(components[2]) / 255.0,
		float(components[3]) / 255.0
	)


static func from_document(document: Dictionary) -> Dictionary:
	## Optional cooked overlay. Unknown or drifted identity is a miss, not a
	## silent merge onto the INI snapshot.
	if document.is_empty():
		return {"ok": false, "reason": "atmosphere document is empty"}
	if String(document.get("schema", "")) != SCHEMA:
		return {"ok": false, "reason": "unexpected atmosphere schema"}
	if int(document.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return {"ok": false, "reason": "unexpected atmosphere schemaVersion"}
	return {"ok": true, "document": document.duplicate(true)}

## Tiny INI helpers for the scaffold: enough to find an object's default model.
## The real INI parser will be the SAGE INI port in the engine; this is not it.
class_name RetailIni
extends RefCounted


## Returns the Model of `object_name`'s DefaultModelConditionState, or "" if absent.
static func find_default_model(ini_text: String, object_name: String) -> String:
	var in_object := false
	var in_default := false
	for raw_line in ini_text.split("\n"):
		var line := raw_line.replace("\t", " ").strip_edges()
		for marker in [";", "//"]:
			var at := line.find(marker)
			if at >= 0:
				line = line.substr(0, at).strip_edges()
		if line.is_empty():
			continue
		var words := line.split(" ", false)
		if words[0] == "Object":
			in_object = words.size() > 1 and words[1] == object_name
			in_default = false
			continue
		if not in_object:
			continue
		if words[0] == "DefaultModelConditionState":
			in_default = true
		elif in_default and words[0] == "End":
			in_default = false
		elif in_default and words[0] == "Model":
			var eq := line.find("=")
			if eq >= 0:
				return line.substr(eq + 1).strip_edges()
	return ""

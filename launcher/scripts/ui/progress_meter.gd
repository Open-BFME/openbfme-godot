## Lane UI-3: the one progress bar's numbers: MB done of total, MB/s and time left. The rate is an exponential average over half-second
## samples (a resumed download jumping ahead, or a new file starting at 0, restarts it), so "time left" does not jump with every chunk.
extends RefCounted

const L10n := preload("res://scripts/l10n/l10n.gd")
const SAMPLE_MS := 500
const SMOOTH := 0.3  # weight of the newest sample

var rate := 0.0       # bytes per second, 0 until the first sample
var _t := -1
var _b := 0


func reset() -> void:
	rate = 0.0
	_t = -1
	_b = 0


## feed a progress report taken at `now_ms`
func feed(done: int, now_ms: int) -> void:
	if _t < 0 or done < _b:
		_t = now_ms
		_b = done
		return
	if now_ms - _t < SAMPLE_MS:
		return
	var sample := (done - _b) * 1000.0 / (now_ms - _t)
	rate = sample if rate <= 0.0 else rate + SMOOTH * (sample - rate)
	_t = now_ms
	_b = done


## "312.4 MB of 1.00 GB · 8.2 MB/s · 1 min 30 s left" (the rate and time once known)
func text(done: int, total: int) -> String:
	var parts := PackedStringArray([L10n.t("launcher.progress_meter.done_of", {"done": mb(done), "total": mb(total)})])
	if rate > 0.0:
		parts.append(L10n.t("launcher.progress_meter.rate", {"rate": rate / 1048576.0}))
		parts.append(L10n.t("launcher.progress_meter.left", {"time": time_left(int(ceil((total - done) / rate)))}))
	return " · ".join(parts)


static func mb(bytes: int) -> String:
	if bytes >= 1024 * 1048576:
		return L10n.t("launcher.progress_meter.gb", {"size": bytes / 1073741824.0})
	return L10n.t("launcher.progress_meter.mb", {"size": bytes / 1048576.0})


static func time_left(secs: int) -> String:
	secs = maxi(secs, 0)
	if secs < 60:
		return L10n.t("launcher.progress_meter.seconds", {"secs": secs})
	if secs < 3600:
		return L10n.t("launcher.progress_meter.minutes_seconds", {"min": secs / 60, "secs": secs % 60}) if secs < 600 else L10n.t("launcher.progress_meter.minutes", {"min": ceili(secs / 60.0)})
	return L10n.t("launcher.progress_meter.hours_minutes", {"hours": secs / 3600, "min": (secs % 3600) / 60})

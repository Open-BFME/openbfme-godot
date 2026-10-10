## Lane UI-3: the one progress bar's numbers: MB done of total, MB/s and time left. The rate is an exponential average over half-second
## samples (a resumed download jumping ahead, or a new file starting at 0, restarts it), so "time left" does not jump with every chunk.
extends RefCounted

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
	var parts := PackedStringArray(["%s of %s" % [mb(done), mb(total)]])
	if rate > 0.0:
		parts.append("%.1f MB/s" % (rate / 1048576.0))
		parts.append(time_left(int(ceil((total - done) / rate))) + " left")
	return " · ".join(parts)


static func mb(bytes: int) -> String:
	if bytes >= 1024 * 1048576:
		return "%.2f GB" % (bytes / 1073741824.0)
	return "%.1f MB" % (bytes / 1048576.0)


static func time_left(secs: int) -> String:
	secs = maxi(secs, 0)
	if secs < 60:
		return "%d s" % secs
	if secs < 3600:
		return "%d min %d s" % [secs / 60, secs % 60] if secs < 600 else "%d min" % ceili(secs / 60.0)
	return "%d h %d min" % [secs / 3600, (secs % 3600) / 60]

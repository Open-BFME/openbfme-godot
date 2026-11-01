extends Control

signal repair_confirmed
signal cancel_requested

const W := preload("res://scripts/ui/widgets.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

var _summary: Label
var _notice: Label
var _files: RichTextLabel
var _progress: ProgressBar
var _progress_text: Label
var _repair: Button
var _close: Button
var _running := false
var _confirming := false
var _report: Dictionary = {}


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var modal := W.modal(L10n.t("launcher.verify_files_panel.title"), 760)
	add_child(modal.root)
	modal.close.pressed.connect(_on_close)
	_summary = W.wrap("")
	modal.body.add_child(_summary)
	_notice = W.wrap("", "Muted")
	modal.body.add_child(_notice)
	_files = RichTextLabel.new()
	_files.custom_minimum_size = Vector2(700, 240)
	_files.bbcode_enabled = false
	_files.selection_enabled = true
	modal.body.add_child(_files)
	_progress_text = W.trim_label("", "Muted")
	modal.body.add_child(_progress_text)
	_progress = W.progress_bar()
	modal.body.add_child(_progress)
	var row := W.hbox()
	row.add_child(W.spacer())
	_close = W.button(L10n.t("common.close"))
	_close.pressed.connect(_on_close)
	row.add_child(_close)
	_repair = W.button(L10n.t("launcher.verify_files_panel.repair"), "download", "GoldButton")
	_repair.pressed.connect(_on_repair)
	row.add_child(_repair)
	modal.body.add_child(row)
	visible = false


static func summary(report: Dictionary) -> String:
	return L10n.t("launcher.verify_files_panel.summary", {"good": report.good.size(), "missing": report.missing.size(), "changed": report.changed.size(), "unchecked": report.unchecked.size()})


func begin(repairing := false) -> void:
	_running = true
	_confirming = false
	_summary.text = L10n.t("launcher.verify_files_panel.repairing") if repairing else L10n.t("launcher.verify_files_panel.checking")
	_notice.text = L10n.t("launcher.verify_files_panel.close_game_notice") if repairing else L10n.t("launcher.verify_files_panel.check_notice")
	_files.text = ""
	_progress.value = 0
	_progress.visible = true
	_progress_text.text = L10n.t("launcher.verify_files_panel.preparing")
	_progress_text.visible = true
	_repair.visible = false
	_close.text = L10n.t("common.cancel")
	_close.disabled = false
	visible = true


func set_progress(text: String, done: int, total: int) -> void:
	_progress_text.text = Paths.scrub(text)
	_progress.max_value = maxi(total, 1)
	_progress.value = done


func show_report(report: Dictionary) -> void:
	_report = report
	_running = false
	_confirming = false
	_summary.text = "%s\n%s" % [report.label, summary(report)]
	if report.has("repaired"):
		_summary.text += L10n.t("launcher.verify_files_panel.repaired", {"count": report.repaired})
	_notice.text = report.warning if report.warning != "" else L10n.t("launcher.verify_files_panel.default_notice")
	var lines := PackedStringArray()
	for group in [[L10n.t("launcher.verify_files_panel.group_missing"), report.missing], [L10n.t("launcher.verify_files_panel.group_changed"), report.changed], [L10n.t("launcher.verify_files_panel.group_unchecked"), report.unchecked], [L10n.t("launcher.verify_files_panel.group_ok"), report.good]]:
		if group[1].is_empty():
			continue
		lines.append(L10n.t("launcher.verify_files_panel.group", {"name": group[0], "total": group[1].size()}))
		for path in group[1]:
			lines.append("  " + String(path))
		lines.append("")
	_files.text = "\n".join(lines)
	_files.scroll_to_line(0)
	_progress.visible = false
	_progress_text.visible = false
	_repair.text = L10n.t("launcher.verify_files_panel.repair")
	_repair.visible = report.repairable and not (report.missing.is_empty() and report.changed.is_empty())
	_close.text = L10n.t("common.close")
	_close.disabled = false
	visible = true


func _on_repair() -> void:
	if _running or not _repair.visible:
		return
	if _confirming:
		repair_confirmed.emit()
		return
	_confirming = true
	_notice.text = L10n.t("launcher.verify_files_panel.confirm_notice", {"missing": _report.missing.size(), "changed": _report.changed.size()})
	_files.text = L10n.t("launcher.verify_files_panel.files_to_replace") + "\n".join(_report.missing + _report.changed)
	_files.scroll_to_line(0)
	_repair.text = L10n.t("launcher.verify_files_panel.confirm_repair")
	_close.text = L10n.t("launcher.verify_files_panel.back")


func _on_close() -> void:
	if _running:
		cancel_requested.emit()
		_close.disabled = true
		_progress_text.text = L10n.t("launcher.verify_files_panel.cancelling")
	elif _confirming:
		show_report(_report)
	else:
		visible = false

from __future__ import annotations

import unittest
from unittest.mock import patch

try:
    from openbfme_importer import sage_string
    from openbfme_importer.sage_string import parse_string_catalog
except ModuleNotFoundError:  # Supports the repository-root acceptance command.
    from importer.openbfme_importer import sage_string
    from importer.openbfme_importer.sage_string import parse_string_catalog


class SageStringTests(unittest.TestCase):
    def test_parse_orders_records_and_resolves_identifiers_case_insensitively(self) -> None:
        source = b"""
// repository-authored fixture
UI:Zulu
  "Second"
END
; another comment
UI:Alpha // comment before value
  "First\\nLine" // comment before terminator
eNd
"""
        catalog = parse_string_catalog(source)

        self.assertEqual(
            [record.identifier for record in catalog.records],
            ["UI:Alpha", "UI:Zulu"],
        )
        self.assertEqual(catalog.resolve("ui:ALPHA"), "First\nLine")
        self.assertEqual(catalog.resolve("UI:Missing"), None)
        self.assertEqual(catalog.record("ui:zulu").category, "UI")

    def test_decodes_supported_escapes_bare_values_and_physical_multiline(self) -> None:
        source = (
            b'UI:Escaped "A\\nB\\rC\\tD\\vE\\\\F\\\'G\\\"H\\/I" End\r\n'
            b'UI:Bare Enabled End\r\n'
            b'UI:Multiline "one\r\ntwo" End\r\n'
        )
        catalog = parse_string_catalog(source)

        self.assertEqual(
            catalog.resolve("UI:Escaped"),
            "A\nB\rC\tD\vE\\F'G\"H/I",
        )
        self.assertEqual(catalog.resolve("UI:Bare"), "Enabled")
        self.assertEqual(catalog.resolve("UI:Multiline"), "one\r\ntwo")

    def test_accepts_and_reports_known_duplicate_value_terminator_form(self) -> None:
        catalog = parse_string_catalog(b'UI:Typo "value"" End')

        self.assertEqual(catalog.resolve("UI:Typo"), "value")
        self.assertEqual(catalog.duplicate_value_terminators, 1)
        self.assertEqual(catalog.neutral_summary()["duplicateValueTerminatorCount"], 1)

    def test_neutral_summary_is_order_independent_and_contains_no_text(self) -> None:
        first = parse_string_catalog(b'UI:Beta "private two" End\nUI:Alpha "private one" End')
        second = parse_string_catalog(b'UI:Alpha "private one" End\nUI:Beta "private two" End')

        self.assertEqual(first.neutral_summary(), second.neutral_summary())
        rendered = repr(first.neutral_summary()).casefold()
        self.assertNotIn("private", rendered)
        self.assertNotIn("ui:alpha", rendered)
        self.assertNotIn("\\", rendered)

    def test_rejects_duplicate_case_insensitive_identifiers(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate case-insensitive"):
            parse_string_catalog(b'UI:Name "one" End\nui:nAME "two" End')

    def test_first_wins_is_source_ordered_and_accounts_for_duplicate_conflicts(self) -> None:
        source = (
            b'UI:Same "unchanged" End\n'
            b'ui:sAME "unchanged" End\n'
            b'UI:Conflict "first" End\n'
            b'ui:conflict "second" End\n'
            b'UI:CONFLICT "third" End'
        )
        catalog = parse_string_catalog(source, duplicate_policy="first-wins")

        self.assertEqual(catalog.resolve("ui:same"), "unchanged")
        self.assertEqual(catalog.resolve("UI:CONFLICT"), "first")
        self.assertEqual(catalog.diagnostics.source_record_count, 5)
        self.assertEqual(catalog.diagnostics.duplicate_record_count, 3)
        self.assertEqual(catalog.diagnostics.conflicting_duplicate_record_count, 2)
        self.assertEqual(catalog.diagnostics.duplicate_identifier_count, 2)
        self.assertEqual(catalog.diagnostics.conflicting_identifier_count, 1)
        self.assertEqual(
            catalog.diagnostics.duplicate_identifiers,
            ("UI:Conflict", "UI:Same"),
        )
        self.assertEqual(
            catalog.diagnostics.conflicting_identifiers,
            ("UI:Conflict",),
        )
        self.assertNotIn("first", repr(catalog.diagnostics.to_report()))
        self.assertNotIn("second", repr(catalog.diagnostics.to_report()))
        self.assertNotIn("third", repr(catalog.diagnostics.to_report()))

    def test_first_wins_keeps_unique_resolution_independent_of_other_duplicates(self) -> None:
        source = (
            b'UI:Duplicate "first" End\n'
            b'ui:duplicate "ignored" End\n'
            b'UI:Requested "resolved" End'
        )
        catalog = parse_string_catalog(source, duplicate_policy="first-wins")

        self.assertEqual(catalog.resolve("ui:requested"), "resolved")
        self.assertEqual(catalog.record("UI:Requested").identifier, "UI:Requested")
        self.assertEqual(len(catalog), 2)

    def test_duplicate_diagnostics_are_deterministic_and_bounded(self) -> None:
        source = (
            b'UI:Zulu "one" End\nui:zulu "two" End\n'
            b'UI:Alpha "one" End\nui:alpha "two" End'
        )
        with patch.object(sage_string, "MAX_DIAGNOSTIC_IDENTIFIERS", 1):
            first = parse_string_catalog(source, duplicate_policy="first-wins")
            second = parse_string_catalog(source, duplicate_policy="first-wins")

        self.assertEqual(first.diagnostics, second.diagnostics)
        self.assertEqual(first.diagnostics.duplicate_identifier_count, 2)
        self.assertEqual(first.diagnostics.duplicate_identifiers, ("UI:Alpha",))
        self.assertEqual(first.diagnostics.conflicting_identifiers, ("UI:Alpha",))
        self.assertTrue(first.diagnostics.identifiers_truncated)

    def test_rejects_unknown_duplicate_policy(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsupported duplicate policy"):
            parse_string_catalog(b"", duplicate_policy="last-wins")  # type: ignore[arg-type]

    def test_rejects_malformed_and_truncated_records(self) -> None:
        malformed = (
            (b'UI:Name "value"', "missing END"),
            (b'UI:Name "value', "unterminated quoted"),
            (b'UI:Name "bad\\q" End', "unsupported string escape"),
            (b'UIOnly "value" End', "missing category separator"),
            (b'_UI:Name "value" End', "invalid category"),
            (b'UI:Name~ "value" End', "invalid label"),
            (b'UI:Name value! End', "missing END"),
            (b'UI:Name "value" EndStuff', "unexpected characters after END"),
            (b'UI:Name "value""" End', "missing END"),
        )
        for source, message in malformed:
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    parse_string_catalog(source)

    def test_rejects_nul_and_undefined_cp1252_bytes(self) -> None:
        with self.assertRaisesRegex(ValueError, "NUL"):
            parse_string_catalog(b'UI:Name "bad\0value" End')
        with self.assertRaisesRegex(ValueError, "unsupported encoding"):
            parse_string_catalog(b'UI:Name "bad\x81value" End')

    def test_enforces_source_record_identifier_value_and_anomaly_bounds(self) -> None:
        source = b'UI:One "1" End\nUI:Two "2" End'
        with patch.object(sage_string, "MAX_STRING_BYTES", len(source) - 1):
            with self.assertRaisesRegex(ValueError, "byte limit"):
                parse_string_catalog(source)
        with patch.object(sage_string, "MAX_STRING_RECORDS", 1):
            with self.assertRaisesRegex(ValueError, "record count"):
                parse_string_catalog(source)
        with patch.object(sage_string, "MAX_IDENTIFIER_CHARS", 4):
            with self.assertRaisesRegex(ValueError, "identifier exceeds"):
                parse_string_catalog(b'UI:Long "value" End')
        with patch.object(sage_string, "MAX_VALUE_CHARS", 3):
            with self.assertRaisesRegex(ValueError, "value exceeds"):
                parse_string_catalog(b'UI:Name "four" End')
        with patch.object(sage_string, "MAX_DUPLICATE_VALUE_TERMINATORS", 0):
            with self.assertRaisesRegex(ValueError, "terminator count"):
                parse_string_catalog(b'UI:Name "value"" End')


if __name__ == "__main__":
    unittest.main()

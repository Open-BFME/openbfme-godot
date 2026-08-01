from __future__ import annotations

import math

import pytest

from openbfme_importer.retail_hud_live_text_oracle import (
    HudLiveTextOracleError,
    format_command_points,
    format_resource_multiplier,
    format_resources,
)


def test_resources_exact_negative_fallback() -> None:
    assert format_resources(1200) == "1200"
    assert format_resources(0) == "0"
    assert format_resources(-1) == " "


def test_command_points_exact_two_one_and_blank_branches() -> None:
    assert format_command_points(60, 200) == "60/200"
    assert format_command_points(60, -1) == "60"
    assert format_command_points(-1, 200) == " "


def test_multiplier_exact_one_is_hidden_and_other_values_use_percent_g() -> None:
    assert format_resource_multiplier(1.0) == " "
    assert format_resource_multiplier(1.5) == "x1.5"
    assert format_resource_multiplier(2.0) == "x2"
    with pytest.raises(HudLiveTextOracleError):
        format_resource_multiplier(math.nan)

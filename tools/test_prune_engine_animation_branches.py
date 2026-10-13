"""Check geometry isolation and persistent-state guards for engine pruning."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "blender"))
from obj8_preview import parse_obj8
from prune_engine_animation_branches import prune_text


HEADER = """I
800
OBJ
POINT_COUNTS 3 0 0 3
VT 1 0 0 0 0 1 0 0
VT 0 1 0 0 0 1 1 0
VT 0 0 0 0 0 1 0 1
IDX 0
IDX 1
IDX 2
"""


class PruneEngineAnimationsTests(unittest.TestCase):
    def test_unused_sibling_refs_removed_live_parent_and_fan_unchanged(self):
        source = HEADER + """ANIM_begin
ANIM_trans 0 0 0 0 1 0 0 1 wing_flex
ANIM_begin
ANIM_rotate 0 0 1 0 90 0 1 unused_aileron
ANIM_begin
ANIM_hide 0 1 unused_visibility
ANIM_end
ANIM_end
ANIM_begin
ANIM_rotate 0 0 1 0 360 0 360 fan_angle
TRIS 0 3
ANIM_end
ANIM_end
"""
        pruned, report = prune_text(source, "a380x_fans")
        self.assertNotIn("unused_", pruned)
        self.assertEqual(report["empty_animation_branches_removed"], 1)
        for flex, angle in ((0, 0), (.5, 90), (1, 270), (.2, 360)):
            values = {"wing_flex": flex, "fan_angle": angle}
            old, new = (parse_obj8(text.splitlines(), values) for text in (source, pruned))
            self.assertEqual(old.positions, new.positions)
            self.assertEqual(old.uv, new.uv)
            self.assertEqual(old.indices, new.indices)
            self.assertEqual(old.bake(), new.bake())
            self.assertEqual(old.draws[0].transform_datarefs, new.draws[0].transform_datarefs)
        self.assertEqual(prune_text(pruned, "a380x_fans")[0], pruned)
        self.assertEqual(prune_text(pruned, "a380x_fans")[1]["animation_lines_removed"], 0)

    def test_persistent_draw_state_and_its_ancestors_retained(self):
        source = HEADER + """ANIM_begin
ANIM_begin
ATTR_no_cull
ANIM_end
ANIM_end
TRIS 0 3
"""
        pruned, report = prune_text(source, "a380x_eng_lh")
        self.assertEqual(pruned, source)
        self.assertEqual(report["animation_lines_removed"], 0)
        self.assertFalse(parse_obj8(pruned.splitlines()).draws[0].cull)

    def test_unknown_or_draw_commands_prevent_removal(self):
        for command in ("ANIM_future_command 1", "LIGHT_NAMED example 0 0 0", "LINES 0 2", "ATTR_draw_disable", "TRIS 0 0"):
            source = f"ANIM_begin\n{command}\nANIM_end\n"
            self.assertEqual(prune_text(source, "a380x_eng_rh")[0], source)

    def test_nonengine_variants_untouched(self):
        source = "ANIM_begin\nANIM_hide 0 1 unused\nANIM_end\n"
        self.assertEqual(prune_text(source, "a380x_wing1")[0], source)

    def test_unbalanced_scopes_rejected(self):
        for source in ("ANIM_begin\n", "ANIM_end\n", "ANIM_begin extra\n"):
            with self.assertRaises(ValueError):
                prune_text(source, "a380x_fans")


if __name__ == "__main__":
    unittest.main()

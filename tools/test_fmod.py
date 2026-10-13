import json
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

from build_development_aircraft import validate_fmod_inputs
from build_fmod_snd import load_event_map, render_snd
from inventory_fbw_sounds import inventory
from inventory_wwise_media import probe_batch
from extract_wwise_bnk_media import extract_bnk_media, parse_bnk_media
from extract_wwise_pck import extract_pck, parse_pck
from stage_fbw_engine_audio import collect_events, parse_txtp
from validate_aircraft import validate_fmod_package


class FmodTests(unittest.TestCase):
    def test_wwise_inventory_uses_metadata_only_mode(self):
        metadata = {
            "sampleRate": 48000,
            "channels": 1,
            "numberOfSamples": 48000,
            "encoding": "test",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            media = root / "123.wem"
            media.write_bytes(b"test")
            completed = subprocess.CompletedProcess([], 0, json.dumps(metadata) + "\n", "")
            with patch("inventory_wwise_media.subprocess.run", return_value=completed) as run:
                entries = probe_batch([media], root, "vgmstream-cli")
            self.assertEqual(run.call_args.args[0][:3], ["vgmstream-cli", "-m", "-I"])
            self.assertEqual(entries[0]["duration_seconds"], 1.0)

    def test_engine_audio_selection_deduplicates_txtp_state_variants(self):
        content = (
            "# PATH\n#   CAkEvent[450] 270236557\n#     Source 468009919\n"
            "# GAMEVARS\n"
            "# - RTPCID: {2750389324=0.0,200.0} <[Volume]: -96.3,1.7>\n"
            "# - RTPCID: {2919001772=0.0,1000.0} <[Volume]: 0.0,-96.3>\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "base.txtp").write_text(content)
            (root / "state-variant.txtp").write_text(content.replace("468009919", "867510340"))
            parsed = parse_txtp(root / "base.txtp")
            self.assertEqual(parsed["event_id"], 270236557)
            events = collect_events(root)
            self.assertEqual(len(events), 1)
            self.assertEqual(events[0]["classification"], "n1_spatial_layer")
            self.assertEqual(events[0]["source_ids"], [468009919, 867510340])

    def test_wwise_bnk_extractor_names_media_by_wwise_id(self):
        media = b"RIFF-test-wem"
        didx = struct.pack("<III", 987654321, 0, len(media))
        bank = b"BKHD" + struct.pack("<I", 0) + b"DIDX" + struct.pack("<I", len(didx)) + didx
        bank += b"DATA" + struct.pack("<I", len(media)) + media
        self.assertEqual(parse_bnk_media(bank), [{"id": 987654321, "offset": 36, "size": len(media)}])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.bnk"
            source.write_bytes(bank)
            extract_bnk_media(source, root / "media")
            self.assertEqual((root / "media/987654321.wem").read_bytes(), media)

    def test_wwise_pck_extractor_preserves_ids_and_payloads(self):
        bank = b"BKHD-test-bank"
        header_size = 76
        language = struct.pack("<I", 1) + struct.pack("<II", 12, 0) + b"sfx\0"
        bank_offset = header_size
        bank_table = struct.pack("<I5I", 1, 12345, 1, len(bank), bank_offset, 0)
        sound_table = struct.pack("<I", 0)
        external_table = struct.pack("<I", 0)
        header = (
            b"AKPK"
            + struct.pack("<6I", header_size, 1, len(language), len(bank_table), len(sound_table), len(external_table))
            + language
            + bank_table
            + sound_table
            + external_table
        )
        self.assertEqual(len(header), header_size)
        parsed = parse_pck(header + bank)
        self.assertEqual(parsed["entries"][0]["id"], 12345)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.pck"
            source.write_bytes(header + bank)
            result = extract_pck(source, root / "out")
            self.assertTrue(result["source_untouched"])
            self.assertEqual((root / "out/12345.bnk").read_bytes(), bank)

    def test_starter_project_has_xplane_12_version_and_required_bus_guids(self):
        root = Path(__file__).resolve().parents[1] / "fmod/project"
        project = (root / "A380X-XP12.fspro").read_text(encoding="utf-8")
        self.assertIn('serializationModel="Studio.02.02.00"', project)
        required = {
            "06db5351-0b2f-4648-b97a-a33b40a00e11",
            "751f5066-4651-4afb-a410-4cb9db0894c9",
            "77575f3b-b9cd-4bff-9056-637cff744a14",
            "7f7c05a4-e05a-4d44-94fe-d4691147a9b7",
            "8dbbc9d7-a954-4ed0-b1f1-f8111b03cbd1",
            "a5a85a81-afd5-4425-9dc2-6e50f7776e6a",
            "b6c156f1-9861-405c-a622-92b2ab8010a7",
            "d8526059-b27c-4eae-9b14-a1ee1cb3dfa1",
            "e3492bec-f854-4fbb-b2bc-23586269032e",
        }
        actual = {path.stem.strip("{}") for path in (root / "Metadata/Group").glob("*.xml")}
        self.assertEqual(actual, required)
        self.assertIn(
            "{9f08114d-36ec-44cc-b8bf-b2babda09f9e}",
            (root / "Metadata/Group/{7f7c05a4-e05a-4d44-94fe-d4691147a9b7}.xml").read_text(),
        )

    def test_initial_event_map_expands_four_engines_and_five_tires(self):
        root = Path(__file__).resolve().parents[1]
        rendered = render_snd(load_event_map(root / "config/fmod_event_map.json"))
        self.assertEqual(rendered.count("BEGIN_SOUND_ATTACHMENT"), 11)
        self.assertIn("VEH_PART engine 3", rendered)
        self.assertIn("PARAM_DREF_IDX 4", rendered)
        self.assertIn("sim/flightmodel2/engines/N1_percent[3] > 1", rendered)

    def test_event_map_rejects_unknown_catalog_dataref(self):
        event_map = {
            "events": [{
                "event": "event:/Test",
                "part": "cockpit",
                "indices": [0],
                "start_conditions": [["sim/not/real", ">", 0]],
            }]
        }
        with self.assertRaisesRegex(ValueError, "unknown X-Plane dataref"):
            render_snd(event_map, {"sim/known"})

    def test_inventory_reads_metadata_without_audio(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sound.xml"
            path.write_text(
                '<SoundInfo><SimVarSounds><Sound WwiseEvent="engine"/>'
                '<Sound WwiseEvent="engine"/></SimVarSounds></SoundInfo>',
                encoding="utf-8",
            )
            result = inventory([path])
            self.assertFalse(result["audio_extracted"])
            self.assertEqual(result["total_unique_events"], 1)
            self.assertEqual(result["files"][0]["event_references"], 2)

    def test_load_event_map_requires_supported_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "events.json"
            path.write_text(json.dumps({"schema_version": 9, "acf_stem": "test", "events": []}))
            with self.assertRaisesRegex(ValueError, "schema_version"):
                load_event_map(path)

    def test_packaging_rejects_guid_file_without_attached_event(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snd = root / "A380X-XP12.snd"
            snd.write_text(
                "A\n1000\nACF_SOUNDS\nBEGIN_SOUND_ATTACHMENT\n"
                "EVENT_NAME event:/A380X/Missing\nVEH_PART cockpit 0\n"
                "EVENT_START_COND sim/test > 0\nEND_SOUND_ATTACHMENT\n"
            )
            (root / "Master Bank.bank").write_bytes(b"bank")
            (root / "GUIDs.txt").write_text("{00000000-0000-0000-0000-000000000000} bank:/Master Bank\n")
            with self.assertRaisesRegex(ValueError, "lacks attached objects"):
                validate_fmod_inputs(snd, root)

    def test_package_validator_accepts_complete_minimal_fmod(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fmod = root / "fmod"
            fmod.mkdir()
            (fmod / "A380X-XP12.snd").write_text(
                "A\n1000\nACF_SOUNDS\nBEGIN_SOUND_ATTACHMENT\n"
                "EVENT_NAME event:/A380X/Test\nVEH_PART cockpit 0\n"
                "EVENT_START_COND sim/test > 0\nEND_SOUND_ATTACHMENT\n"
            )
            (fmod / "Master Bank.bank").write_bytes(b"bank")
            (fmod / "GUIDs.txt").write_text(
                "{00000000-0000-0000-0000-000000000000} event:/A380X/Test\n"
            )
            self.assertEqual(validate_fmod_package(root), ([], []))


if __name__ == "__main__":
    unittest.main()

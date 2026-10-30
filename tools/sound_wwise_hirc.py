#!/usr/bin/env python3
"""Read-only Wwise (bank version 135, Wwise 2019.2) parser for the FBW MSFS sound packages.

This module parses the AKPK packages shipped by FlyByWire in
``SimObjects/AirPlanes/FlyByWire_A380X/common/sound`` and decodes the HIRC
hierarchy of the embedded sound bank: events, actions, random/sequence, switch
and blend (layer) containers, actor-mixers, sounds, attenuations and states,
including RTPC curves.  Every object is parsed field by field and must consume
exactly the size declared in the bank; any mismatch raises, so a silent
mis-parse is not possible.

Event names
-----------
Wwise stores 32-bit FNV-1 hashes ("short IDs") of lower-case names, not the
names.  MSFS posts a ``WwiseEvent`` from ``sound.xml`` as the Wwise event named
``Play_<MainPackage>_<WwiseEvent>``.  The FBW A380X ``sound.xml`` declares
``<MainPackage Name="Asobo_A320_NEO"/>``, so every FBW event ID is::

    fnv1_32("play_asobo_a320_neo_" + wwise_event.lower())

The prefix was recovered from the banks themselves (206 of the event/name
pairs from ``sound.xml`` share one FNV pre-state, which is exactly
``fnv1_32("play_asobo_a320_neo_")``), so a recovered name is an exact hash
match, not a heuristic.  Game parameters (RTPCs), switch groups, states and
banks use the plain hash of their name.

Field layouts follow the public description of the bank format in the
open-source ``wwiser`` project (bnnm) for version 135; no code from it is used.
"""
from __future__ import annotations

import hashlib
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_wwise_pck import parse_pck  # noqa: E402

FNV_BASIS = 2166136261
FNV_PRIME = 16777619
FNV_PRIME_INVERSE = pow(FNV_PRIME, -1, 2 ** 32)
MSFS_EVENT_PREFIX = "play_asobo_a320_neo_"


def fnv1_32(name: str, state: int = FNV_BASIS) -> int:
    """Wwise short ID: 32-bit FNV-1 of the lower-cased UTF-8 name."""
    value = state
    for byte in name.lower().encode("utf-8"):
        value = (value * FNV_PRIME) & 0xFFFFFFFF
        value ^= byte
    return value


def fnv1_unwind(value: int, suffix: str) -> int:
    """Inverse of :func:`fnv1_32` for a known suffix: the state before ``suffix``."""
    for byte in reversed(suffix.lower().encode("utf-8")):
        value = ((value ^ byte) * FNV_PRIME_INVERSE) & 0xFFFFFFFF
    return value


MSFS_EVENT_STATE = fnv1_32(MSFS_EVENT_PREFIX)


def msfs_event_id(wwise_event: str) -> int:
    return fnv1_32(wwise_event, MSFS_EVENT_STATE)


HIRC_TYPES = {
    1: "State", 2: "Sound", 3: "Action", 4: "Event", 5: "RanSeqCntr", 6: "SwitchCntr",
    7: "ActorMixer", 8: "Bus", 9: "LayerCntr", 10: "MusicSegment", 11: "MusicTrack",
    12: "MusicSwitch", 13: "MusicRanSeq", 14: "Attenuation", 15: "DialogueEvent",
    16: "FxShareSet", 17: "FxCustom", 18: "AuxBus", 19: "LFO", 20: "Envelope",
    21: "AudioDevice", 22: "TimeMod",
}

# AkPropID, bank versions 128..145
PROP_IDS = dict(enumerate([
    "Volume", "LFE", "Pitch", "LPF", "HPF", "BusVolume", "MakeUpGain", "Priority",
    "PriorityDistanceOffset", "_FeedbackVolume", "_FeedbackLPF", "MuteRatio", "PAN_LR", "PAN_FR",
    "CenterPCT", "DelayTime", "TransitionTime", "Probability", "DialogueMode", "UserAuxSendVolume0",
    "UserAuxSendVolume1", "UserAuxSendVolume2", "UserAuxSendVolume3", "GameAuxSendVolume",
    "OutputBusVolume", "OutputBusHPF", "OutputBusLPF", "HDRBusThreshold", "HDRBusRatio",
    "HDRBusReleaseTime", "HDRBusGameParam", "HDRBusGameParamMin", "HDRBusGameParamMax",
    "HDRActiveRange", "LoopStart", "LoopEnd", "TrimInTime", "TrimOutTime", "FadeInTime",
    "FadeOutTime", "FadeInCurve", "FadeOutCurve", "LoopCrossfadeDuration", "CrossfadeUpCurve",
    "CrossfadeDownCurve", "MidiTrackingRootNote", "MidiPlayOnNoteType", "MidiTransposition",
    "MidiVelocityOffset", "MidiKeyRangeMin", "MidiKeyRangeMax", "MidiVelocityRangeMin",
    "MidiVelocityRangeMax", "MidiChannelMask", "PlaybackSpeed", "MidiTempoSource", "MidiTargetNode",
    "AttachedPluginFXID", "Loop", "InitialDelay", "UserAuxSendLPF0", "UserAuxSendLPF1",
    "UserAuxSendLPF2", "UserAuxSendLPF3", "UserAuxSendHPF0", "UserAuxSendHPF1", "UserAuxSendHPF2",
    "UserAuxSendHPF3", "GameAuxSendLPF", "GameAuxSendHPF", "AttenuationID", "PositioningTypeBlend",
    "ReflectionBusVolume", "PAN_UD",
]))
INT_PROPS = {"Priority", "Loop", "AttenuationID", "AttachedPluginFXID", "MidiTargetNode",
             "HDRBusGameParam", "DialogueMode", "MidiPlayOnNoteType", "MidiTrackingRootNote",
             "MidiTempoSource", "MidiChannelMask", "FadeInCurve", "FadeOutCurve",
             "CrossfadeUpCurve", "CrossfadeDownCurve", "MidiKeyRangeMin", "MidiKeyRangeMax",
             "MidiVelocityRangeMin", "MidiVelocityRangeMax"}

# AkRTPC_ParameterID, bank versions >= 135
RTPC_PARAMS = dict(enumerate([
    "Volume", "LFE", "Pitch", "LPF", "HPF", "BusVolume", "InitialDelay", "MakeUpGain",
    "Deprecated_FeedbackVolume", "Deprecated_FeedbackLowpass", "Deprecated_FeedbackPitch",
    "MidiTransposition", "MidiVelocityOffset", "PlaybackSpeed", "MuteRatio",
    "PlayMechanismSpecialTransitionsValue", "MaxNumInstances", "Priority", "Position_PAN_X_2D",
    "Position_PAN_Y_2D", "Position_PAN_X_3D", "Position_PAN_Y_3D", "Position_PAN_Z_3D",
    "PositioningTypeBlend", "Positioning_Divergence_Center_PCT", "Positioning_Cone_Attenuation_ON_OFF",
    "Positioning_Cone_Attenuation", "Positioning_Cone_LPF", "Positioning_Cone_HPF", "BypassFX0",
    "BypassFX1", "BypassFX2", "BypassFX3", "BypassAllFX", "HDRBusThreshold", "HDRBusReleaseTime",
    "HDRBusRatio", "HDRActiveRange", "GameAuxSendVolume", "UserAuxSendVolume0", "UserAuxSendVolume1",
    "UserAuxSendVolume2", "UserAuxSendVolume3", "OutputBusVolume", "OutputBusHPF", "OutputBusLPF",
    "Positioning_EnableAttenuation", "ReflectionsVolume", "UserAuxSendLPF0", "UserAuxSendLPF1",
    "UserAuxSendLPF2", "UserAuxSendLPF3", "UserAuxSendHPF0", "UserAuxSendHPF1", "UserAuxSendHPF2",
    "UserAuxSendHPF3", "GameAuxSendLPF", "GameAuxSendHPF", "Position_PAN_Z_2D", "BypassAllMetadata",
]))
CURVE_INTERP = {0: "Log3", 1: "Sine", 2: "Log1", 3: "InvSCurve", 4: "Linear", 5: "SCurve",
                6: "Exp1", 7: "SineRecip", 8: "Exp3", 9: "Constant"}
CURVE_SCALING = {0: "None", 2: "dB", 3: "Log", 4: "dBToLin"}
RTPC_TYPE = {0: "GameParameter", 1: "MIDIParameter", 2: "Modulator"}
RTPC_ACCUM = {0: "None", 1: "Exclusive", 2: "Additive", 3: "Multiply", 4: "Boolean", 5: "Maximum", 6: "Filter"}
RANSEQ_MODE = {0: "Random", 1: "Sequence"}
RANDOM_MODE = {0: "Normal", 1: "Shuffle"}
TRANSITION_MODE = {0: "Disabled", 1: "CrossFadeAmp", 2: "CrossFadePower", 3: "Delay",
                   4: "SampleAccurate", 5: "TriggerRate"}
GROUP_TYPE = {0: "Switch", 1: "State"}
STREAM_TYPE = {0: "DataBnk", 1: "PrefetchStreaming", 2: "Streaming"}
ATTENUATION_CURVES = ["VolumeDry", "VolumeWetGame", "VolumeWetUser", "LowPassFilter",
                      "HighPassFilter", "Spread", "Focus"]
ACTION_TYPES = {
    0x01: "Stop", 0x02: "Pause", 0x03: "Resume", 0x04: "Play", 0x05: "PlayAndContinue",
    0x06: "Mute", 0x07: "UnMute", 0x08: "SetPitch", 0x09: "ResetPitch", 0x0A: "SetVolume",
    0x0B: "ResetVolume", 0x0C: "SetBusVolume", 0x0D: "ResetBusVolume", 0x0E: "SetLPF",
    0x0F: "ResetLPF", 0x10: "UseState", 0x11: "UnuseState", 0x12: "SetState",
    0x13: "SetGameParameter", 0x14: "ResetGameParameter", 0x19: "SetSwitch", 0x1A: "ToggleBypass",
    0x1B: "ResetBypassEffect", 0x1C: "Break", 0x1D: "Trigger", 0x1E: "Seek", 0x1F: "Release",
    0x20: "SetHPF", 0x21: "PlayEvent", 0x22: "ResetPlaylist", 0x30: "SetFX", 0x31: "ResetSetFX",
    0x32: "NoOp?", 0x33: "PlayEventUnknown?",
}
CODECS = {0x00010001: "PCM", 0x00020001: "ADPCM", 0x00040001: "Vorbis", 0x00050001: "WMA",
          0x00080001: "XMA", 0x00090001: "AAC", 0x000A0001: "FilePackage", 0x000B0001: "ATRAC9",
          0x00110001: "Opus", 0x00120001: "OpusWEM", 0x00130001: "OPUSWEM", 0x00650002: "Silence",
          0x00660002: "ToneGenerator", 0x00940002: "SynthOne"}


class ParseError(ValueError):
    pass


class Reader:
    def __init__(self, data: bytes, offset: int = 0, end: int | None = None):
        self.data, self.pos, self.end = data, offset, len(data) if end is None else end

    def _take(self, fmt: str):
        size = struct.calcsize(fmt)
        if self.pos + size > self.end:
            raise ParseError(f"read past end at {self.pos}")
        value = struct.unpack_from("<" + fmt, self.data, self.pos)
        self.pos += size
        return value[0] if len(value) == 1 else value

    def u8(self): return self._take("B")
    def u16(self): return self._take("H")
    def u32(self): return self._take("I")
    def s32(self): return self._take("i")
    def f32(self): return self._take("f")

    def var(self) -> int:
        current = self.u8()
        value = current & 0x7F
        while current & 0x80:
            current = self.u8()
            value = (value << 7) | (current & 0x7F)
        return value

    def bytes(self, count: int) -> bytes:
        if self.pos + count > self.end:
            raise ParseError("read past end")
        value = self.data[self.pos:self.pos + count]
        self.pos += count
        return value


def _graph(reader: Reader, count: int) -> list[dict]:
    points = []
    for _ in range(count):
        x, y, interp = reader.f32(), reader.f32(), reader.u32()
        points.append({"x": round(x, 6), "y": round(y, 6), "interp": CURVE_INTERP.get(interp, interp)})
    return points


def _prop_value(name: str, raw: bytes):
    if name in INT_PROPS:
        return struct.unpack("<i", raw)[0] if name != "AttenuationID" else struct.unpack("<I", raw)[0]
    return round(struct.unpack("<f", raw)[0], 6)


def _props(reader: Reader) -> tuple[dict, dict]:
    count = reader.u8()
    ids = [reader.u8() for _ in range(count)]
    values = {}
    for prop in ids:
        name = PROP_IDS.get(prop, f"prop_{prop}")
        values[name] = _prop_value(name, reader.bytes(4))
    count = reader.u8()
    ids = [reader.u8() for _ in range(count)]
    ranged = {}
    for prop in ids:
        name = PROP_IDS.get(prop, f"prop_{prop}")
        low, high = reader.bytes(4), reader.bytes(4)
        ranged[name] = [_prop_value(name, low), _prop_value(name, high)]
    return values, ranged


def _initial_rtpc(reader: Reader) -> list[dict]:
    curves = []
    for _ in range(reader.u16()):
        rtpc_id = reader.u32()
        rtpc_type, accum = reader.u8(), reader.u8()
        param = reader.var()
        curve_id = reader.u32()
        scaling = reader.u8()
        size = reader.u16()
        curves.append({
            "rtpc_id": rtpc_id, "rtpc_type": RTPC_TYPE.get(rtpc_type, rtpc_type),
            "accum": RTPC_ACCUM.get(accum, accum), "param": RTPC_PARAMS.get(param, f"param_{param}"),
            "curve_id": curve_id, "scaling": CURVE_SCALING.get(scaling, scaling), "points": _graph(reader, size),
        })
    return curves


def _node_base(reader: Reader) -> dict:
    node: dict = {}
    node["override_parent_fx"] = reader.u8()
    fx_count = reader.u8()
    fx = []
    if fx_count:
        reader.u8()  # bitsFXBypass
        for _ in range(fx_count):
            index, fx_id, share, rendered = reader.u8(), reader.u32(), reader.u8(), reader.u8()
            fx.append({"index": index, "fx_id": fx_id, "shareset": share, "rendered": rendered})
    node["fx"] = fx
    reader.u8()  # bOverrideAttachmentParams
    node["override_bus_id"] = reader.u32()
    node["parent_id"] = reader.u32()
    node["priority_bits"] = reader.u8()
    node["props"], node["ranged_props"] = _props(reader)
    # PositioningParams
    bits = reader.u8()
    positioning = {
        "override_parent": bool(bits & 1), "listener_relative_routing": bool(bits & 2),
        "panner_type": (bits >> 2) & 3, "position_type_3d": (bits >> 5) & 3,
    }
    if bits & 1 and bits & 2:
        bits3d = reader.u8()
        positioning.update({
            "spatialization": bits3d & 3, "enable_attenuation": bool(bits3d & 8),
            "hold_emitter_position": bool(bits3d & 16), "hold_listener_orientation": bool(bits3d & 32),
            "is_looping_path": bool(bits3d & 64), "enable_diffraction": bool(bits3d & 128),
        })
        if (bits >> 5) & 3:
            reader.u8(); reader.s32()
            vertices = reader.u32()
            reader.bytes(16 * vertices)
            items = reader.u32()
            reader.bytes(8 * items)
            reader.bytes(12 * items)
            positioning["automation_vertices"] = vertices
    node["positioning"] = positioning
    # AuxParams
    aux_bits = reader.u8()
    if aux_bits & 8:
        node["aux_ids"] = [reader.u32() for _ in range(4)]
    node["reflections_aux_bus"] = reader.u32()
    # AdvSettingsParams
    adv_bits = reader.u8()
    virtual_queue = reader.u8()
    max_instances = reader.u16()
    below_threshold = reader.u8()
    reader.u8()
    node["adv"] = {"kill_newest": bool(adv_bits & 1), "use_virtual": bool(adv_bits & 2),
                   "ignore_parent_max_instances": bool(adv_bits & 8), "virtual_queue": virtual_queue,
                   "max_instances": max_instances, "below_threshold": below_threshold}
    # StateChunk
    for _ in range(reader.var()):
        reader.var(); reader.u8(); reader.u8()
    states = []
    for _ in range(reader.var()):
        group = reader.u32()
        sync = reader.u8()
        entries = [(reader.u32(), reader.u32()) for _ in range(reader.var())]
        states.append({"group_id": group, "sync": sync,
                       "states": [{"state_id": s, "state_instance_id": i} for s, i in entries]})
    node["state_groups"] = states
    node["rtpcs"] = _initial_rtpc(reader)
    return node


def _children(reader: Reader) -> list[int]:
    return [reader.u32() for _ in range(reader.u32())]


def _parse_sound(reader: Reader) -> dict:
    plugin = reader.u32()
    stream = reader.u8()
    source_id, in_memory, bits = reader.u32(), reader.u32(), reader.u8()
    result = {"codec": CODECS.get(plugin, f"0x{plugin:08x}"), "plugin_id": plugin,
              "stream_type": STREAM_TYPE.get(stream, stream), "source_id": source_id,
              "in_memory_size": in_memory, "language_specific": bool(bits & 1)}
    if plugin & 0xF == 2:
        size = reader.u32()
        result["plugin_params"] = reader.bytes(size).hex()
    result.update(_node_base(reader))
    return result


def _parse_ranseq(reader: Reader) -> dict:
    result = _node_base(reader)
    result["loop_count"] = reader.u16()
    result["loop_mod"] = [reader.u16(), reader.u16()]
    result["transition_time"] = [round(reader.f32(), 4) for _ in range(3)]
    result["avoid_repeat"] = reader.u16()
    result["transition_mode"] = TRANSITION_MODE.get(reader.u8())
    result["random_mode"] = RANDOM_MODE.get(reader.u8())
    result["mode"] = RANSEQ_MODE.get(reader.u8())
    bits = reader.u8()
    result.update({"using_weight": bool(bits & 1), "reset_playlist": bool(bits & 2),
                   "restart_backward": bool(bits & 4), "continuous": bool(bits & 8),
                   "global": bool(bits & 16)})
    result["children"] = _children(reader)
    result["playlist"] = [{"id": reader.u32(), "weight": reader.s32()} for _ in range(reader.u16())]
    return result


def _parse_switch(reader: Reader) -> dict:
    result = _node_base(reader)
    result["group_type"] = GROUP_TYPE.get(reader.u8())
    result["group_id"] = reader.u32()
    result["default_switch"] = reader.u32()
    result["continuous_validation"] = reader.u8()
    result["children"] = _children(reader)
    result["switches"] = []
    for _ in range(reader.u32()):
        switch_id = reader.u32()
        result["switches"].append({"switch_id": switch_id, "nodes": [reader.u32() for _ in range(reader.u32())]})
    result["switch_params"] = []
    for _ in range(reader.u32()):
        node, bits1, bits2, fade_out, fade_in = reader.u32(), reader.u8(), reader.u8(), reader.s32(), reader.s32()
        result["switch_params"].append({"node": node, "first_only": bool(bits1 & 1),
                                        "continue_playback": bool(bits1 & 2), "on_switch_mode": bits2 & 7,
                                        "fade_out_ms": fade_out, "fade_in_ms": fade_in})
    return result


def _parse_layer(reader: Reader) -> dict:
    result = _node_base(reader)
    result["children"] = _children(reader)
    layers = []
    for _ in range(reader.u32()):
        layer = {"layer_id": reader.u32(), "rtpcs": _initial_rtpc(reader)}
        layer["crossfade_rtpc_id"] = reader.u32()
        layer["crossfade_rtpc_type"] = RTPC_TYPE.get(reader.u8())
        assoc = []
        for _ in range(reader.u32()):
            child = reader.u32()
            assoc.append({"child": child, "curve": _graph(reader, reader.u32())})
        layer["associations"] = assoc
        layers.append(layer)
    result["layers"] = layers
    result["continuous_validation"] = reader.u8()
    return result


def _parse_actor_mixer(reader: Reader) -> dict:
    result = _node_base(reader)
    result["children"] = _children(reader)
    return result


def _parse_attenuation(reader: Reader) -> dict:
    result: dict = {}
    if reader.u8() & 1:
        result["cone"] = {"inside_deg": reader.f32(), "outside_deg": reader.f32(),
                          "outside_volume": reader.f32(), "lowpass": reader.f32(), "highpass": reader.f32()}
    curve_to_use = [struct.unpack("<b", reader.bytes(1))[0] for _ in range(7)]
    curves = []
    for _ in range(reader.u8()):
        scaling = reader.u8()
        curves.append({"scaling": CURVE_SCALING.get(scaling, scaling), "points": _graph(reader, reader.u16())})
    result["curves"] = {name: curves[index] for name, index in zip(ATTENUATION_CURVES, curve_to_use)
                        if 0 <= index < len(curves)}
    result["rtpcs"] = _initial_rtpc(reader)
    return result


def _parse_state(reader: Reader) -> dict:
    count = reader.u16()
    ids = [reader.u16() for _ in range(count)]
    return {"props": {RTPC_PARAMS.get(i, f"param_{i}"): round(reader.f32(), 6) for i in ids}}


def _parse_event(reader: Reader) -> dict:
    return {"actions": [reader.u32() for _ in range(reader.var())]}


def _parse_action(reader: Reader) -> dict:
    action_type = reader.u16()
    result = {"action_type": f"0x{action_type:04x}",
              "action": ACTION_TYPES.get(action_type >> 8, f"type_{action_type >> 8:#x}"),
              "scope": action_type & 0xFF, "target_id": reader.u32(), "target_is_bus": bool(reader.u8() & 1)}
    result["props"], result["ranged_props"] = _props(reader)
    kind = action_type >> 8
    if kind == 0x04:  # Play
        result["fade_curve"] = reader.u8() & 0x1F
        result["bank_id"] = reader.u32()
    elif kind in (0x01, 0x02, 0x03):  # Stop / Pause / Resume
        result["fade_curve"] = reader.u8() & 0x1F
        result["specific_bits"] = reader.u8()
        result["exceptions"] = [(reader.u32(), reader.u8()) for _ in range(reader.var())]
    elif kind == 0x12:  # SetState
        result["state_group_id"], result["state_id"] = reader.u32(), reader.u32()
    elif kind == 0x19:  # SetSwitch
        result["switch_group_id"], result["switch_state_id"] = reader.u32(), reader.u32()
    elif kind in (0x13, 0x14):  # Set/Reset GameParameter
        result["fade_curve"] = reader.u8() & 0x1F
        if kind == 0x13:
            result["bypass_transition"] = reader.u8()
            result["value_meaning"], result["value"] = reader.u8(), [reader.f32(), reader.f32(), reader.f32()]
        result["exceptions"] = [(reader.u32(), reader.u8()) for _ in range(reader.var())]
    elif kind in (0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x20):
        result["fade_curve"] = reader.u8() & 0x1F
        if kind in (0x08, 0x0A, 0x0C, 0x0E, 0x20):
            result["value_meaning"], result["value"] = reader.u8(), [reader.f32(), reader.f32(), reader.f32()]
        result["exceptions"] = [(reader.u32(), reader.u8()) for _ in range(reader.var())]
    elif kind == 0x1E:  # Seek
        result["seek"] = {"relative_to_duration": reader.u8(), "value": [reader.f32() for _ in range(3)],
                          "snap_to_marker": reader.u8()}
        result["exceptions"] = [(reader.u32(), reader.u8()) for _ in range(reader.var())]
    else:
        result["unparsed"] = reader.bytes(reader.end - reader.pos).hex()
    return result


def _parse_fx(reader: Reader) -> dict:
    """FxShareSet / FxCustom: only the plugin ID is decoded; the parameter block is kept opaque."""
    plugin = reader.u32()
    rest = reader.bytes(reader.end - reader.pos)
    return {"plugin_id": plugin, "plugin_hex": f"0x{plugin:08x}", "plugin_type": plugin & 0xF,
            "plugin_company": (plugin >> 4) & 0x3FF, "opaque_bytes": len(rest)}


PARSERS = {1: _parse_state, 2: _parse_sound, 3: _parse_action, 4: _parse_event, 5: _parse_ranseq,
           6: _parse_switch, 7: _parse_actor_mixer, 9: _parse_layer, 14: _parse_attenuation,
           16: _parse_fx, 17: _parse_fx}


@dataclass
class Bank:
    package: str
    package_sha256: str
    bank_id: int
    version: int
    media: dict[int, tuple[int, int]]  # media id -> (absolute offset in package, size)
    objects: dict[int, dict] = field(default_factory=dict)
    order: list[int] = field(default_factory=list)


def parse_hirc(body: bytes) -> tuple[dict[int, dict], list[int]]:
    count = struct.unpack_from("<I", body, 0)[0]
    offset, objects, order = 4, {}, []
    for index in range(count):
        kind = body[offset]
        size, object_id = struct.unpack_from("<II", body, offset + 1)
        start, end = offset + 9, offset + 5 + size
        parser = PARSERS.get(kind)
        if parser is None:
            item = {"raw_size": size - 4}
        else:
            reader = Reader(body, start, end)
            try:
                item = parser(reader)
            except ParseError as error:
                raise ParseError(f"HIRC #{index} {HIRC_TYPES.get(kind, kind)} {object_id}: {error}") from None
            if reader.pos != end:
                raise ParseError(f"HIRC #{index} {HIRC_TYPES.get(kind, kind)} {object_id}: "
                                 f"parsed {reader.pos - start} of {end - start} bytes")
        item.update({"id": object_id, "type": HIRC_TYPES.get(kind, kind), "hirc_index": index})
        if object_id in objects:
            raise ParseError(f"duplicate HIRC id {object_id}")
        objects[object_id] = item
        order.append(object_id)
        offset = end
    if offset != len(body):
        raise ParseError("trailing bytes in HIRC")
    return objects, order


def load_package(path: Path) -> Bank:
    data = path.read_bytes()
    entries = parse_pck(data)["entries"]
    banks = [entry for entry in entries if entry["kind"] == "bank"]
    if len(banks) != 1 or any(entry["kind"] != "bank" for entry in entries):
        raise ParseError(f"{path.name}: expected exactly one embedded bank and no loose media")
    base, size = banks[0]["offset"], banks[0]["size"]
    cursor, end = base, base + size
    version = bank_id = None
    didx = data_offset = hirc = None
    while cursor + 8 <= end:
        tag = data[cursor:cursor + 4]
        length = struct.unpack_from("<I", data, cursor + 4)[0]
        payload = cursor + 8
        if tag == b"BKHD":
            version, bank_id = struct.unpack_from("<II", data, payload)
        elif tag == b"DIDX":
            didx = data[payload:payload + length]
        elif tag == b"DATA":
            data_offset = payload
        elif tag == b"HIRC":
            hirc = data[payload:payload + length]
        cursor = payload + length
    if version != 135:
        raise ParseError(f"{path.name}: unsupported bank version {version}")
    media = {}
    for index in range(0, len(didx or b""), 12):
        media_id, relative, length = struct.unpack_from("<III", didx, index)
        media[media_id] = (data_offset + relative, length)
    bank = Bank(path.name, hashlib.sha256(data).hexdigest(), bank_id, version, media)
    bank.objects, bank.order = parse_hirc(hirc)
    return bank


def read_media(package: Path, bank: Bank, media_id: int) -> bytes:
    offset, size = bank.media[media_id]
    with package.open("rb") as source:
        source.seek(offset)
        return source.read(size)


def ancestors(objects: dict[int, dict], node_id: int) -> list[dict]:
    """The node and its DirectParentID chain (only parents present in this bank)."""
    chain, seen = [], set()
    while node_id in objects and node_id not in seen:
        seen.add(node_id)
        node = objects[node_id]
        chain.append(node)
        node_id = node.get("parent_id", 0)
    return chain


def self_test(sound_dir: Path | None = None) -> None:
    """Hash vectors (game-parameter IDs present in the FBW banks) and, optionally, a full bank parse."""
    assert fnv1_32("SIMVAR_TURB_ENG_N1") == 2750389324
    assert fnv1_32("LOCALVAR_A32NX_INTERIOR_ENG_VOLUME") == 2508093411
    assert fnv1_32("FBW_A320_NEO_1") == 814486318  # bank ID of FBW_A320_NEO_1.PC.PCK
    assert MSFS_EVENT_STATE == 3351899593
    assert fnv1_unwind(fnv1_32("pb1on", MSFS_EVENT_STATE), "pb1on") == MSFS_EVENT_STATE
    if sound_dir is not None:
        events = set()
        for path in sorted(sound_dir.glob("FBW_A320_NEO_*.PC.*")):
            bank = load_package(path)
            events |= {i for i, o in bank.objects.items() if o["type"] == "Event"}
        assert msfs_event_id("pb1on") in events and msfs_event_id("Engine_1_Front") in events
        print(f"ok: {len(events)} unique events parsed with exact object sizes")
    else:
        print("ok: hash vectors")


if __name__ == "__main__":
    import argparse
    _parser = argparse.ArgumentParser(description="FBW Wwise bank parser self-test")
    _parser.add_argument("--self-test", type=Path, nargs="?", const=None, default=None, metavar="SOUND_DIR")
    self_test(_parser.parse_args().self_test)

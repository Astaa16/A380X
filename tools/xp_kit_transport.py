"""Protocol v5 shared-memory layout, matching a380x_xpkit_shared.h exactly."""
import ctypes as C

W, H, CAP, VERSION = 384, 512, 262144, 5
ND_W, ND_H = 768, 1024


class Rect(C.Structure):
    _fields_ = [(key, C.c_float) for key in ('x', 'y', 'w', 'h')]


class Point(C.Structure):
    _fields_ = [('x', C.c_float), ('y', C.c_float)]


class Command(C.Structure):
    _fields_ = [(key, C.c_uint8) for key in ('kind', 'layer', 'color', 'font', 'anchor', 'closed')] + [
        ('clip', C.c_uint16), ('a', C.c_float * 4), ('width', C.c_float), ('size', C.c_float),
        ('first', C.c_uint32), ('count', C.c_uint32)]


class DrawList(C.Structure):
    _fields_ = [('cmds', Command * 4096), ('points', Point * 16384), ('text', C.c_char * 32768),
                ('clips', Rect * 256), ('cmd_count', C.c_uint32), ('point_count', C.c_uint32),
                ('text_len', C.c_uint32), ('clip_count', C.c_uint32), ('clip_stack', C.c_uint16 * 16),
                ('clip_depth', C.c_int), ('layer', C.c_uint8), ('overflowed', C.c_int)]


class MfdFrame(C.Structure):
    _fields_ = [('sequence', C.c_uint32), ('powered', C.c_uint32), ('uri', C.c_char * 64), ('list', DrawList)]


class SdFrame(C.Structure):
    _fields_ = [('sequence', C.c_uint32), ('size', C.c_uint32), ('json', C.c_char * 65536)]


class NdProfile(C.Structure):
    _fields_ = [(key, C.c_uint64) for key in ('snapshot_count', 'snapshot_ns', 'apply_count', 'apply_ns',
                'skipped_count', 'upload_count', 'upload_bytes', 'draw_count')]


class Shared(C.Structure):
    _fields_ = [('magic', C.c_uint32), ('version', C.c_uint32),
                ('state_seq', C.c_uint32), ('state_size', C.c_uint32),
                ('state_time', C.c_double), ('state', C.c_char * CAP),
                ('ready', C.c_uint32 * 7), ('frame_seq', C.c_uint32 * 7),
                ('errors', C.c_uint32), ('frame_time', C.c_double * 7),
                ('zoom_delta', C.c_int * 2), ('nd_status', (C.c_uint32 * 8) * 2),
                ('error', C.c_char * 512), ('pixels', (C.c_ubyte * (W * H * 4)) * 2),
                ('nd_pixels', (C.c_ubyte * (ND_W * ND_H * 4)) * 2),
                ('mfd_pixels', (C.c_ubyte * (768 * 1024 * 4)) * 3), ('mfd', MfdFrame * 2), ('sd', SdFrame),
                ('nd_profile', NdProfile * 2)]


def view_dimensions(view):
    return (W, H) if view < 2 else (ND_W, ND_H) if view < 4 else (768, 1024)


def view_buffer(view):
    width, height = view_dimensions(view)
    length = width * height * 4
    field, index = ('pixels', view) if view < 2 else ('nd_pixels', view - 2) if view < 4 else ('mfd_pixels', view - 4)
    return getattr(Shared, field).offset + length * index, length

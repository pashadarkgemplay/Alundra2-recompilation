import ctypes, os, sys, subprocess, numpy as np
_here = os.path.dirname(os.path.abspath(__file__)); _root = os.path.dirname(_here)
_lib = os.path.join(_here, 'hdcodec.dll' if os.name == 'nt' else 'hdcodec.so')
_src = [os.path.join(_here, 'hdcodec_dll.c'), os.path.join(_root, 'rt', 'a2hdcodec.h')]
if not os.path.exists(_lib) or os.path.getmtime(_lib) < max(os.path.getmtime(f) for f in _src):
    zig = os.path.join(_root, 'tools', 'zig', 'ziglang', 'zig.exe')
    cc = [zig, 'cc'] if os.path.exists(zig) else [sys.executable, '-m', 'ziglang', 'cc']
    subprocess.run(cc + ['-shared', '-O2', '-I' + os.path.join(_root, 'rt'), '-o', _lib, _src[0]], check=True)
_d = ctypes.CDLL(_lib)
_d.hd_enc.restype = ctypes.c_size_t; _d.hd_rowhash.restype = ctypes.c_uint64; _d.hd_cellrow.restype = ctypes.c_uint64
_d.hd_enc.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
_d.hd_dec.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
_d.hd_crypt.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint64]
_d.hd_rowhash.argtypes = [ctypes.c_char_p, ctypes.c_size_t]; _d.hd_cellrow.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
def encode(lr, bpp, W, H, K, i0, i1, wq):
    hd = np.stack([i0, i1, wq, np.zeros_like(i0)], -1).astype(np.uint8).tobytes()
    cap = len(hd) + 1024; out = ctypes.create_string_buffer(cap)
    n = _d.hd_enc(lr, bpp, W, H, K, hd, out, cap); return out.raw[:n]
def decode(lr, bpp, W, H, K, blob):
    out = ctypes.create_string_buffer(W * K * H * K * 4); _d.hd_dec(lr, bpp, W, H, K, blob, len(blob), out)
    return np.frombuffer(out.raw, np.uint8).reshape(H * K, W * K, 4)
def crypt(b, nonce):
    buf = ctypes.create_string_buffer(bytes(b), len(b)); _d.hd_crypt(buf, len(b), nonce); return buf.raw[:len(b)]
def rowhash(b): return _d.hd_rowhash(b, len(b))
def cellrow(b): return _d.hd_cellrow(b, len(b))

"""Compare the actual ES 3 visibility shader to CPU masks using Mesa surfaceless EGL."""
from build_viewer import visibility_source

import argparse
import csv
import ctypes as ct
import json
import os
import re
from pathlib import Path

U, I, F, P, S = ct.c_uint, ct.c_int, ct.c_float, ct.c_void_p, ct.c_char_p
METHODS = ("constant", "slope", "receiver", "numeric", "footprint", "full", "reference")
PARAMETERS = (0.0001, 0.5, 0.000002, 2.0, 2.0, 2.0, 0.0)
INPUTS = [
    ("positions", "positions", 0x8814, 0x1908, 0x1406, False),
    ("gradients", "gradients", 0x8814, 0x1908, 0x1406, False),
    ("inputError", "input_error", 0x822E, 0x1903, 0x1406, False),
    ("depthSlope", "depth_slope", 0x8814, 0x1908, 0x1406, True),
    ("casterPlanes", "caster_planes", 0x8236, 0x8D94, 0x1405, True),
]
TEXTURE_2D, FRAMEBUFFER = 0x0DE1, 0x8D40


def bind(lib, name, out, *args):
    f = getattr(lib, name)
    f.restype, f.argtypes = out, args
    return f


def egl_context():
    egl = ct.CDLL("libEGL.so.1")
    display = bind(egl, "eglGetPlatformDisplay", P, U, P, P)(0x31DD, None, None)
    initialized = bind(egl, "eglInitialize", U, P, P, P)(display, None, None)
    selected_api = bind(egl, "eglBindAPI", U, U)(0x30A0)
    attributes = (I * 5)(0x3033, 1, 0x3040, 0x0040, 0x3038)
    config, count = P(), I()
    choose = bind(egl, "eglChooseConfig", U, P, P, P, I, P)
    if not initialized or not selected_api or not choose(display, attributes, ct.byref(config), 1, ct.byref(count)) or count.value != 1:
        raise RuntimeError("Mesa surfaceless EGL with an ES 3 context is required")
    attrs = (I * 3)(0x3098, 3, 0x3038)
    context = bind(egl, "eglCreateContext", P, P, P, P, P)(display, config, None, attrs)
    if not context or not bind(egl, "eglMakeCurrent", U, P, P, P, P)(display, None, None, context):
        raise RuntimeError("cannot activate a surfaceless ES 3 context")
    address = bind(egl, "eglGetProcAddress", P, S)

    def gl(name, result, *arg_types):
        ptr = address(("gl" + name).encode("ascii"))
        if not ptr:
            raise RuntimeError(f"OpenGL function unavailable: {name}")
        return ct.CFUNCTYPE(result, *arg_types)(ptr)

    return gl


class Driver:
    def __init__(self):
        gl = egl_context()
        self.get_string = gl("GetString", S, U)
        self.get_error = gl("GetError", U)
        self.disable = gl("Disable", None, U)
        self.create_shader = gl("CreateShader", U, U)
        self.shader_source = gl("ShaderSource", None, U, I, P, P)
        self.compile_shader = gl("CompileShader", None, U)
        self.shader_iv = gl("GetShaderiv", None, U, U, P)
        self.shader_log = gl("GetShaderInfoLog", None, U, I, P, P)
        self.create_program = gl("CreateProgram", U)
        self.attach = gl("AttachShader", None, U, U)
        self.link = gl("LinkProgram", None, U)
        self.program_iv = gl("GetProgramiv", None, U, U, P)
        self.program_log = gl("GetProgramInfoLog", None, U, I, P, P)
        self.use = gl("UseProgram", None, U)
        self.location = gl("GetUniformLocation", I, U, S)
        self.uniform1i = gl("Uniform1i", None, I, I)
        self.uniform1f = gl("Uniform1f", None, I, F)
        self.gen_textures = gl("GenTextures", None, I, P)
        self.delete_textures = gl("DeleteTextures", None, I, P)
        self.active = gl("ActiveTexture", None, U)
        self.bind_texture = gl("BindTexture", None, U, U)
        self.tex_parameter = gl("TexParameteri", None, U, U, I)
        self.tex_image = gl("TexImage2D", None, U, I, I, I, I, I, U, U, P)
        self.gen_fb = gl("GenFramebuffers", None, I, P)
        self.bind_fb = gl("BindFramebuffer", None, U, U)
        self.attach_texture = gl("FramebufferTexture2D", None, U, U, U, U, I)
        self.check_fb = gl("CheckFramebufferStatus", U, U)
        self.gen_vao = gl("GenVertexArrays", None, I, P)
        self.bind_vao = gl("BindVertexArray", None, U)
        self.viewport = gl("Viewport", None, I, I, I, I)
        self.draw = gl("DrawArrays", None, U, I, I)
        self.read = gl("ReadPixels", None, I, I, I, I, U, U, P)
        vao, fb = U(), U()
        self.gen_vao(1, ct.byref(vao))
        self.bind_vao(vao)
        self.gen_fb(1, ct.byref(fb))
        self.bind_fb(FRAMEBUFFER, fb)
        self.disable(0x0BD0)

    def assert_ok(self):
        result = self.get_error()
        if result:
            raise RuntimeError(f"OpenGL error {result:#x}")

    @staticmethod
    def require(p, query, ty, info):
        ok = I()
        query(p, ty, ct.byref(ok))
        if not ok.value:
            log = ct.create_string_buffer(8192)
            info(p, len(log), None, log)
            raise RuntimeError(log.value.decode())

    def shader(self, stage, source):
        shader = self.create_shader(stage)
        ptr = S(source.encode())
        self.shader_source(shader, 1, ct.byref(ptr), None)
        self.compile_shader(shader)
        self.require(shader, self.shader_iv, 0x8B81, self.shader_log)
        return shader

    def program(self, vertex_shader, source):
        p = self.create_program()
        self.attach(p, vertex_shader)
        self.attach(p, self.shader(0x8B30, source))
        self.link(p)
        self.require(p, self.program_iv, 0x8B82, self.program_log)
        return p

    def texture(self, unit, width, height, internal, fmt, ty, data=None):
        out = U()
        self.gen_textures(1, ct.byref(out))
        self.active(0x84C0 + unit)
        self.bind_texture(TEXTURE_2D, out)
        for parameter in (0x2801, 0x2800):
            self.tex_parameter(TEXTURE_2D, parameter, 0x2600)
        self.tex_image(TEXTURE_2D, 0, internal, width, height, 0, fmt, ty, data)
        self.assert_ok()
        return out


def run(case, driver, shaders, renderer):
    config = json.loads(case.read_text())
    w, h, n = (config[k] for k in ("width", "height", "size"))
    driver.viewport(0, 0, w, h)
    textures = []
    for i, (_, file, internal, fmt, ty, shadow_map) in enumerate(INPUTS):
        width, height = (n, n) if shadow_map else (w, h)
        textures.append(driver.texture(i, width, height, internal, fmt, ty, (case.parent / f"{file}.bin").read_bytes()))
    mask = driver.texture(len(INPUTS), w, h, 0x8058, 0x1908, 0x1401)
    textures.append(mask)
    driver.attach_texture(FRAMEBUFFER, 0x8CE0, TEXTURE_2D, mask, 0)
    if driver.check_fb(FRAMEBUFFER) != 0x8CD5:
        raise RuntimeError("incomplete mask framebuffer")
    buffer = (ct.c_ubyte * (w * h * 4))()
    for method, parameter, shader in zip(METHODS, PARAMETERS, shaders):
        driver.use(shader)
        for i, (name, *_) in enumerate(INPUTS):
            driver.uniform1i(driver.location(shader, name.encode()), i)
        driver.uniform1i(driver.location(shader, b"mapSize"), n)
        driver.uniform1i(driver.location(shader, b"radius"), 0)
        driver.uniform1f(driver.location(shader, b"alpha"), config["warp"])
        driver.uniform1f(driver.location(shader, b"parameter"), parameter)
        driver.draw(4, 0, 3)
        driver.read(0, 0, w, h, 0x1908, 0x1401, buffer)
        driver.assert_ok()
        cpu = (case.parent / f"hard_{method}.bin").read_bytes()
        gpu = bytes(buffer)[::4]
        mismatch = sum(a != b for a, b in zip(cpu, gpu, strict=True))
        yield [config["id"], method, w * h, mismatch, renderer]
    for texture in textures:
        driver.delete_textures(1, ct.byref(texture))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--fixtures", type=Path, default=Path("viewer/cases"))
    p.add_argument("--out", type=Path, required=True)
    args = p.parse_args()
    os.environ.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    driver = Driver()
    renderer = driver.get_string(0x1F01).decode()
    viewer = Path(__file__).resolve().parents[1] / "viewer"
    js = (viewer / "lab.js").read_text(encoding="utf-8")
    frag = visibility_source(viewer.parent)
    vertex = re.search(r"const vertexSource = `(.*?)`;", js, re.S)[1]
    paint = re.search(r"const paintSource = `(.*?)`;", js, re.S)[1]
    vertex_shader = driver.shader(0x8B31, vertex)
    driver.program(vertex_shader, paint)
    programs = []
    for i in range(len(METHODS)):
        for diagnostics in (0, 1):
            source = frag.replace("#version 300 es", f"#version 300 es\n#define METHOD {i}\n#define DIAGNOSTICS {diagnostics}")
            shader = driver.program(vertex_shader, source)
            if diagnostics == 0:
                programs.append(shader)
    cases = sorted(args.fixtures.glob("*/fixture.json"))
    if not cases:
        raise RuntimeError("no fixture cases were found")
    with args.out.open("w", newline="", encoding="utf-8") as output:
        w = csv.writer(output)
        w.writerow(["fixture", "method", "pixels", "different", "renderer"])
        for case in cases:
            w.writerows(run(case, driver, programs, renderer))
    print(f"Checked {len(cases)} fixtures with {renderer}.")


if __name__ == "__main__":
    main()

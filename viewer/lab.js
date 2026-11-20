"use strict";

const $ = id => document.getElementById(id);
const sides = ["A", "B"];
const methodNames = ["Constant depth", "Slope scale", "Receiver plane", "Numerics only",
                     "Footprint", "Auto", "Ray reference"];
const methodIds = ["constant", "slope", "receiver", "numeric", "footprint", "full", "reference"];
const defaultValues = [0.0001, 0.5, 0.000002, 2, 2, 2, 0];
const labels = ["Depth bias", "Slope multiplier", "Depth epsilon", "Bias scale", "Bias scale", "Bias scale", "Parameter"];
const inputKeys = ["positions", "gradients", "input_error", "base", "depth_slope", "caster_planes"];
const textureUniforms = ["positions", "gradients", "inputError", "base", "depthSlope", "casterPlanes"];
const paintUniforms = ["mask", "positions", "base", "view"];
const compareUniforms = [...textureUniforms, "mapSize", "alpha", "parameter", "radius"];
const sceneNames = { ramp: "Planar ramp", hills: "Rolling hills", terraces: "Terraces", ridge: "Mountain ridge", custom: "Custom terrain" };

const vertexSource = `#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2),float(gl_VertexID & 2));
    gl_Position = vec4(p*2.0-1.0,0.0,1.0);
}`;

const paintSource = `#version 300 es
precision highp float;
precision highp int;
uniform sampler2D mask, positions, base;
uniform int view;
out vec4 color;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 data = texelFetch(mask,p,0).rg;
    vec3 rgb = texelFetch(base,p,0).rgb;
    float kind = texelFetch(positions,p,0).w;
    vec3 result = rgb*(1.0-0.69*data.r);
    if (view == 1) {
        result = vec3(dot(rgb,vec3(0.083)));
        if (kind == 2.0 && data.r >= 0.5) result = vec3(1.0,0.4118,0.2824);
        if (kind >= 3.0 && data.r < 0.5) result = vec3(0.3373,0.8275,0.9333);
    }
    if (view == 2) result = vec3(1.0-data.r)*step(1.0,kind);
    if (view == 3) {
        vec3 cold = mix(vec3(0.076,0.11,0.18),vec3(0.20,0.72,0.70),smoothstep(0.0,0.56,data.g));
        result = mix(cold,vec3(1.0,0.68,0.31),smoothstep(0.56,1.0,data.g));
        if (kind < 2.0) result = vec3(0.055,0.079,0.085);
    }
    color = vec4(result,1.0);
}`;

let gl, timer, renderer, paint, vao, mask, target, asset, textures = {}, programs = new Map();
let configurations, shaderSource, lastRows = [], epoch = 0, running = false, cancelled = false;

function status(message, error = false) {
    $("status").textContent = message;
    $("status").dataset.error = String(error);
}

function compile(type, source) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        const message = gl.getShaderInfoLog(shader);
        gl.deleteShader(shader);
        throw Error(message);
    }
    return shader;
}

function program(fragment, uniforms) {
    const vertex = compile(gl.VERTEX_SHADER, vertexSource);
    const pixel = compile(gl.FRAGMENT_SHADER, fragment);
    const handle = gl.createProgram();
    gl.attachShader(handle, vertex);
    gl.attachShader(handle, pixel);
    gl.linkProgram(handle);
    gl.deleteShader(vertex);
    gl.deleteShader(pixel);
    if (!gl.getProgramParameter(handle, gl.LINK_STATUS)) {
        const message = gl.getProgramInfoLog(handle);
        gl.deleteProgram(handle);
        throw Error(message);
    }
    const places = Object.fromEntries(uniforms.map(name => [name,gl.getUniformLocation(handle,name)]));
    return { handle, places };
}

function comparisonProgram(method, diagnostic) {
    const key = method * 2 + Number(diagnostic);
    if (!programs.has(key)) {
        const define = `#version 300 es\n#define METHOD ${method}\n#define DIAGNOSTICS ${Number(diagnostic)}`;
        programs.set(key, program(shaderSource.replace("#version 300 es",define), compareUniforms));
    }
    return programs.get(key);
}

async function decompress(value) {
    const binary = Uint8Array.from(atob(value), ch => ch.charCodeAt(0));
    const stream = new Blob([binary]).stream().pipeThrough(new DecompressionStream("gzip"));
    return new Response(stream).arrayBuffer();
}

function texture(internal, format, type, width, height, source) {
    const t = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, t);
    for (const filter of [gl.TEXTURE_MIN_FILTER,gl.TEXTURE_MAG_FILTER]) {
        gl.texParameteri(gl.TEXTURE_2D, filter, gl.NEAREST);
    }
    for (const axis of [gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T]) {
        gl.texParameteri(gl.TEXTURE_2D, axis, gl.CLAMP_TO_EDGE);
    }
    gl.texImage2D(gl.TEXTURE_2D, 0, internal, width, height, 0, format, type, source);
    const error = gl.getError();
    if (error !== gl.NO_ERROR) {
        gl.deleteTexture(t);
        throw Error(`WebGL texture upload failed (0x${error.toString(16)}).`);
    }
    return t;
}

async function loadScene() {
    const index = ++epoch;
    const selection = configurations[Number($("scene").value)];
    if (!selection) throw Error("No fixture selected.");
    status("Loading comparison geometry…");
    $("controls").disabled = true;
    const buffers = await Promise.all(inputKeys.map(key => decompress(selection.files[key])));
    if (index !== epoch) return;
    asset = selection;
    const {width,height,size} = asset;
    const c = $("engine");
    c.width = width;
    c.height = height;
    for (const t of Object.values(textures)) gl.deleteTexture(t);
    if (mask) gl.deleteTexture(mask);
    if (target) gl.deleteFramebuffer(target);
    const f = (buffer,w=width,h=height) => texture(gl.RGBA32F,gl.RGBA,gl.FLOAT,w,h,new Float32Array(buffer));
    textures = {
        positions: f(buffers[0]),
        gradients: f(buffers[1]),
        inputError: texture(gl.R32F,gl.RED,gl.FLOAT,width,height,new Float32Array(buffers[2])),
        base: texture(gl.RGBA8,gl.RGBA,gl.UNSIGNED_BYTE,width,height,new Uint8Array(buffers[3])),
        depthSlope: f(buffers[4],size,size),
        casterPlanes: texture(gl.R32UI,gl.RED_INTEGER,gl.UNSIGNED_INT,size,size,new Uint32Array(buffers[5]))
    };
    asset.points = new Float32Array(buffers[0]);
    mask = texture(gl.RGBA8,gl.RGBA,gl.UNSIGNED_BYTE,width,height,null);
    target = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER,target);
    gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,mask,0);
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE)
        throw Error("Could not create the shadow mask framebuffer.");
    $("metadata").textContent = `${sceneNames[asset.scene]} · ${asset.view} · sun ${asset.sun}° / ${asset.azimuth}° · ${size}² map · NDC warp ${asset.warp}`;
    $("controls").disabled = false;
    status(timer ? "Ready for GPU timing." : "GPU timestamps are unavailable. Image comparison and export are ready.");
    render();
}

function bind(p, uniforms, sources) {
    gl.useProgram(p.handle);
    for (let i = 0; i < uniforms.length; ++i) {
        const where = p.places[uniforms[i]];
        if (where === null) continue;
        gl.activeTexture(gl.TEXTURE0+i);
        gl.bindTexture(gl.TEXTURE_2D,sources[i]);
        gl.uniform1i(where,i);
    }
}

function prepare(method, parameter, radius, diagnostic) {
    const p = comparisonProgram(method,diagnostic);
    gl.bindFramebuffer(gl.FRAMEBUFFER,target);
    gl.viewport(0,0,asset.width,asset.height);
    bind(p,textureUniforms,textureUniforms.map(name => textures[name]));
    gl.uniform1i(p.places.mapSize,asset.size);
    gl.uniform1f(p.places.alpha,asset.warp);
    gl.uniform1f(p.places.parameter,parameter);
    gl.uniform1i(p.places.radius,radius);
}

function draw() { gl.drawArrays(gl.TRIANGLES,0,3); }

function pixels() {
    const raw = new Uint8Array(asset.width*asset.height*4);
    gl.readPixels(0,0,asset.width,asset.height,gl.RGBA,gl.UNSIGNED_BYTE,raw);
    return raw;
}

function score(raw) {
    const counts = {lit:0,shadow:0,false_shadow:0,missed_shadow:0,contact:0,missed_contact:0};
    for (let i = 0; i < raw.length; i += 4) {
        const kind = asset.points[i+3], shadow = raw[i] >= 128;
        if (kind === 2) {
            ++counts.lit;
            if (shadow) ++counts.false_shadow;
        } else if (kind >= 3) {
            ++counts.shadow;
            if (!shadow) ++counts.missed_shadow;
        }
        if (kind === 4) {
            ++counts.contact;
            if (!shadow) ++counts.missed_contact;
        }
    }
    return counts;
}

function shade(canvas, view) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    bind(paint,["mask","positions","base"],[mask,textures.positions,textures.base]);
    gl.uniform1i(paint.places.view,view);
    draw();
    canvas.width = asset.width;
    canvas.height = asset.height;
    canvas.getContext("2d",{alpha:false}).drawImage($("engine"),0,0);
}

function selections() {
    return sides.map(side => {
        const method = Number($("method"+side).value);
        const input = $("value"+side);
        const typed = input.valueAsNumber;
        const value = Number.isFinite(typed) && typed >= 0 ? typed : defaultValues[method];
        return {method,value};
    });
}

function formatRate(n, d) { return d ? `${(100*n/d).toFixed(2)}%` : "·"; }

function render() {
    if (!asset || running) return;
    const radius = Number($("filter").value), view = Number($("display").value);
    lastRows = selections().map(({method,value},i) => {
        prepare(method,value,radius,view === 3);
        draw();
        const counts = score(pixels());
        shade($("image"+sides[i]),view);
        $("caption"+sides[i]).textContent = methodNames[method];
        return {method:methodIds[method],parameter:value,...counts};
    });
    const metrics = [
        ["False shadow", "false_shadow", "lit"],
        ["Missed shadow", "missed_shadow", "shadow"],
        ["Missed contact", "missed_contact", "contact"]
    ];
    $("scores").replaceChildren(...metrics.map(([name,n,d]) => {
        const tr = document.createElement("tr");
        const th = document.createElement("th");
        th.scope = "row";
        th.textContent = name;
        tr.append(th,...lastRows.map(row => {
            const td = document.createElement("td");
            td.textContent = formatRate(row[n],row[d]);
            td.title = `${row[n]} / ${row[d]}`;
            return td;
        }));
        return tr;
    }));
}

function download(blob, name) {
    const link = document.createElement("a");
    const url = URL.createObjectURL(blob);
    link.href = url;
    link.download = name;
    link.click();
    setTimeout(() => URL.revokeObjectURL(url),1000);
}

function exportPng() {
    const gap = 8, bar = 58, canvas = document.createElement("canvas");
    canvas.width = 2*asset.width+gap;
    canvas.height = asset.height+bar;
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = "#182023";
    ctx.fillRect(0,0,canvas.width,canvas.height);
    ctx.fillStyle = "#e6edeb";
    ctx.font = "15px system-ui";
    ctx.fillText($("metadata").textContent,10,20);
    ctx.font = "600 14px system-ui";
    sides.forEach((side,i) => {
        const left = i*(asset.width+gap);
        ctx.fillText($("caption"+side).textContent+` · ${lastRows[i].parameter}`,left+10,45);
        ctx.drawImage($("image"+side),left,bar);
    });
    canvas.toBlob(b => download(b,asset.id+"-comparison.png"),"image/png");
}

function exportCsv(rows,name) {
    const keys = Object.keys(rows[0]);
    const value = cell => '"' + String(cell).replaceAll('"','""') + '"';
    const csv = [keys.join(","),...rows.map(row => keys.map(k => value(row[k])).join(","))].join("\n")+"\n";
    download(new Blob([csv],{type:"text/csv"}), name);
}

const waitFrame = () => new Promise(resolve => setTimeout(resolve,8));

async function assertNotDisjoint() {
    if (gl.isContextLost() || gl.getParameter(timer.GPU_DISJOINT_EXT))
        throw Error("GPU timestamps were invalidated. Run the trial again.");
}

async function timedDraws(trial, radius, batch, timestamp = true) {
    prepare(trial.method,trial.value,radius,false);
    if (!timestamp) {
        for (let i=0;i<batch;++i) draw();
        gl.finish();
        return 0;
    }
    const query = gl.createQuery();
    try {
        gl.beginQuery(timer.TIME_ELAPSED_EXT,query);
        for (let i = 0; i < batch; ++i) draw();
        gl.endQuery(timer.TIME_ELAPSED_EXT);
        gl.flush();
        const start = performance.now();
        while (!gl.getQueryParameter(query,gl.QUERY_RESULT_AVAILABLE)) {
            if (cancelled) throw Error("Timing stopped.");
            await assertNotDisjoint();
            if (performance.now()-start > 20000) throw Error("GPU timestamp query timed out.");
            await waitFrame();
        }
        await assertNotDisjoint();
        const ns = gl.getQueryParameter(query,gl.QUERY_RESULT);
        if (!Number.isFinite(ns) || ns <= 0) throw Error("The driver returned an invalid timestamp.");
        return ns/batch;
    } finally {
        gl.deleteQuery(query);
    }
}

async function benchmark() {
    if (!timer || running) return;
    const trials = selections();
    if (trials.some(t => t.method === 6)) throw Error("Choose two shadow-map methods for timing.");
    const radius = Number($("filter").value);
    const repeats = 24, batch = 20, warmup = 40, rows = [];
    running = true;
    cancelled = false;
    $("controls").disabled = true;
    const stop = $("stop");
    stop.hidden = false;
    $("controls").parentElement.appendChild(stop);
    try {
        await assertNotDisjoint();
        for (const trial of trials) await timedDraws(trial,radius,warmup,false);
        for (let i = 0; i < repeats; ++i) {
            const order = i%2 ? [1,0] : [0,1];
            for (const j of order) {
                if (cancelled) throw Error("Timing stopped.");
                const ns = await timedDraws(trials[j],radius,batch);
                rows.push({version:"0.1.1",fixture:asset.id,width:asset.width,height:asset.height,
                    map_size:asset.size,warp:asset.warp,pcf_radius:radius,pair:i+1,order:i%2?"BA":"AB",
                    arm:j === 0?"A":"B",method:methodIds[trials[j].method],parameter:trials[j].value,
                    warmup_draws:warmup,batch_draws:batch,gpu_ns_per_draw:ns,renderer,browser:navigator.userAgent});
            }
            status(`GPU timing · pair ${i+1} / ${repeats}`);
            await waitFrame();
        }
        const sorted = rows.filter(r => r.arm === "B").map(b => b.gpu_ns_per_draw
            -rows.find(a => a.arm === "A" && a.pair === b.pair).gpu_ns_per_draw).sort((a,b)=>a-b);
        const median = (sorted[repeats/2-1]+sorted[repeats/2])*0.5;
        status(`Timing saved · median right − left ${(median/1000).toFixed(2)} μs.`);
        exportCsv(rows,asset.id+"-gpu-timing.csv");
    } finally {
        running = false;
        stop.hidden = true;
        $("controls").disabled = false;
        render();
    }
}

function updateMethod(side, initial) {
    const method = Number($("method"+side).value);
    const input = $("value"+side), label = $("label"+side);
    label.firstChild.nodeValue = labels[method];
    input.value = defaultValues[method];
    label.hidden = method === 6;
    if (!initial) render();
}

async function compareCpuMasks() {
    const differences = {};
    for (let i = 0; i < methodIds.length; ++i) {
        const reference = new Uint8Array(await decompress(asset.files["hard_"+methodIds[i]]));
        prepare(i,defaultValues[i],0,false);
        draw();
        const gpu = pixels();
        let errors = 0;
        for (let j = 0; j < reference.length; ++j) errors += gpu[j*4] !== reference[j] ? 1 : 0;
        differences[methodIds[i]] = errors;
    }
    return differences;
}

async function initialize() {
    configurations = JSON.parse($("fixtures").textContent);
    shaderSource = $("visibility-shader").textContent.trimStart();
    for (const side of sides) {
        $("method"+side).replaceChildren(...methodNames.map((name,i)=>new Option(name,i)));
        $("method"+side).value = side === "A" ? "0" : "5";
        updateMethod(side,true);
        $("method"+side).addEventListener("change",()=>updateMethod(side,false));
        $("value"+side).addEventListener("change",render);
    }
    $("scene").replaceChildren(...configurations.map((s,i)=>new Option(
        `${sceneNames[s.scene]} · ${s.view} · ${s.size}² · warp ${s.warp}`,i)));
    const first = configurations.findIndex(s=>s.scene === "ridge" && s.view === "overview" && s.size === 512 && s.warp === 8);
    $("scene").value = String(first >= 0 ? first : 0);
    gl = $("engine").getContext("webgl2",{alpha:false,antialias:false,depth:false,
                                         stencil:false,preserveDrawingBuffer:true});
    if (!gl) throw Error("WebGL 2 is required. Enable hardware acceleration in your browser.");
    const debug = gl.getExtension("WEBGL_debug_renderer_info");
    renderer = debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER);
    $("hardware").textContent = renderer;
    timer = gl.getExtension("EXT_disjoint_timer_query_webgl2");
    $("timing").disabled = !timer;
    gl.disable(gl.DITHER);
    vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    paint = program(paintSource,paintUniforms);
    $("engine").addEventListener("webglcontextlost",() => status("Graphics context lost. Reload the page.",true));
    $("scene").addEventListener("change",()=>loadScene().catch(error=>status(error.message,true)));
    $("filter").addEventListener("change",render);
    $("display").addEventListener("change",render);
    $("capture").addEventListener("click",exportPng);
    $("quality").addEventListener("click",() => exportCsv(lastRows.map(row =>
        ({fixture:asset.id,pcf_radius:Number($("filter").value),...row})),asset.id+"-quality.csv"));
    $("timing").addEventListener("click",()=>benchmark().catch(error=>status(error.message,true)));
    $("stop").addEventListener("click",()=>{cancelled=true;});
    await loadScene();
    window.shadowAutoBiasLab = { compareCpuMasks };
}

initialize().catch(error=>status(error.message,true));

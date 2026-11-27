// Aporia engine shaders for the Direct3D backends.
//
// The source is compiled at runtime by D3DCompile, so the same file serves
// every Direct3D version: D3D12 uses the shader model 5.1 profile.
//
// Register layout (matches the backend root signature):
//   b0 scene constants, b1 object constants
//   t0 albedo, t1 shadow depth
//   s0 linear sampler, s1 clamp sampler
//
// The engine matrices are column-major and arrive in the constant buffer
// exactly as the host built them, so they are uploaded as column_major
// float4x4. HLSL's mul(vector, matrix) treats the vector as a row vector and
// would compute the transpose, so every transform below uses mul(matrix,
// vector) instead.

cbuffer SceneConstants : register(b0)
{
    float4x4 viewProjection;
    float4x4 sunViewProjection;
    float4   sunDirection;    // xyz: direction to the sun, w: intensity
    float4   cameraPosition;  // xyz: world position, w: unused
    float4   shadingParams;   // x: ambient, y: shadow strength, z: light count, w: shadow texel size
    float4   screenParams;    // x: width, y: height, z: exposure, w: shadow map size
    float4   fogColor;        // rgb: fog colour, a: density
    float4   fogParams;       // x: height falloff (1/m), y: fog base height
    float4   cameraRight;
    float4   cameraUp;
    float4   cameraForward;
    float4   lightPosition[16];  // xyz position, w type (0 dir, 1 point, 2 spot)
    float4   lightDirection[16]; // xyz toward the light, w unused
    float4   lightColor[16];    // rgb colour, a intensity
    float4   lightParams[16];   // x radius, y softness, z inner cos, w outer cos
    float4   uiParams;          // x text mode (0 ttf, 1 msdf), y distance range, z blur mode (0/1/2), w blur intensity
    float4   jitter;            // xy current sub-pixel offset in pixels, zw previous offset
    float4   aaParams;          // x history weight, y strength, z tan(half fov), w unused
    float4   shadowParams;      // x kernel radius in texels, y filter (0 pcf, 1 pcss), z light size
};

cbuffer ObjectConstants : register(b1)
{
    float4x4 model;
    float4   color;     // rgb tint, a alpha
    float4   material;  // x emissive
};

// UI-only constants: the rect pass reads the glass mode from here so the
// renderer can switch it between the UI phases (backdrop blur draws the top
// layer plain) without rebuilding the scene block.
cbuffer UiConstants : register(b2)
{
    float4 uiBlur;      // x glass mode (0 solid, 1 glass), y intensity, z radius, w unused
};

// Full screen blur: xy direction in texels, z radius in texels.
cbuffer BlurConstants : register(b3)
{
    float4 blurParams;
};

Texture2D<float4> texAlbedo : register(t0);
Texture2D<float>  texShadow : register(t1);
Texture2D<float4> texHistory : register(t2);

SamplerState sampLinear : register(s0);
SamplerState sampClamp  : register(s1);

// ---------------------------------------------------------------------------
// Scene geometry
// ---------------------------------------------------------------------------

struct MeshInput
{
    float3 position : ATTRIBUTE0;
    float3 normal   : ATTRIBUTE1;
    float2 uv       : ATTRIBUTE2;
};

struct MeshOutput
{
    float4 position   : SV_POSITION;
    float3 normal     : TEXCOORD0;
    float2 uv         : TEXCOORD1;
    float4 lightSpace : TEXCOORD2;
    float3 world      : TEXCOORD3;
};

MeshOutput SceneVS(MeshInput input)
{
    MeshOutput output;
    const float4 world = mul(model, float4(input.position, 1.0));
    const float4 clip = mul(viewProjection, world);
    output.position = clip;
    // The temporal pass moves the scene by a fraction of a pixel every frame,
    // so the resolve can average samples that land between two pixels. The
    // offset is in screen pixels: x grows right, y grows down.
    const float2 jitterNdc = float2(jitter.x * 2.0 / max(screenParams.x, 1.0),
                                    -jitter.y * 2.0 / max(screenParams.y, 1.0));
    output.position.xy += jitterNdc * output.position.w;
    output.world = world.xyz;
    output.normal = normalize(mul((float3x3)model, input.normal));
    output.uv = input.uv;
    output.lightSpace = mul(sunViewProjection, world);
    return output;
}

struct ShadowOutput
{
    float4 position : SV_POSITION;
    float  depth    : TEXCOORD0;
};

// The shadow map is a plain R32F colour target: the depth of the fragment
// that survived the depth test is written as colour, so the main pass can
// sample it with an ordinary shader resource view (a depth buffer cannot be
// sampled that way without a typeless resource).
ShadowOutput ShadowVS(MeshInput input)
{
    ShadowOutput output;
    const float4 world = mul(model, float4(input.position, 1.0));
    const float4 clip = mul(sunViewProjection, world);
    output.position = clip;
    output.depth = clip.z / max(clip.w, 1e-6);
    return output;
}

float ShadowPS(ShadowOutput input) : SV_TARGET
{
    return input.depth;
}

// ---------------------------------------------------------------------------
// Shadows
// ---------------------------------------------------------------------------

// Direct3D framebuffer coordinates: x grows right, v grows down.
float2 ShadowUv(float3 projected)
{
    return float2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
}

float ShadowSample(float2 uv, float depth)
{
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }
    const float mapDepth = texShadow.Sample(sampClamp, uv).r;
    return depth <= mapDepth ? 1.0 : 0.0;
}

float ShadowMapDepth(float2 uv)
{
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }
    return texShadow.Sample(sampClamp, uv).r;
}

// A 16 point Poisson disk: the same sample count regardless of the kernel
// radius, so a wide soft shadow costs the same as a narrow one. The disk is
// rotated per pixel from a hash of the screen position, which turns the
// leftover pattern into noise instead of visible bands.
static const float2 kShadowDisk[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790),
};

float2 ShadowDiskRotation(float2 uv)
{
    const float angle = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    return float2(cos(angle), sin(angle));
}

float2 RotateOffset(float2 offset, float2 rotation)
{
    return float2(offset.x * rotation.x - offset.y * rotation.y,
                  offset.x * rotation.y + offset.y * rotation.x);
}

// Filters the shadow map over a disk of `radius` texels. A radius of zero
// falls back to the single bilinear tap.
float PcfShadowFiltered(float2 uv, float depth, float radius, float2 rotation, float texel)
{
    if (radius <= 0.5) {
        return ShadowSample(uv, depth);
    }
    float lit = 0.0;
    [unroll]
    for (int index = 0; index < 16; ++index) {
        const float2 offset = RotateOffset(kShadowDisk[index], rotation) * texel * radius;
        lit += ShadowSample(uv + offset, depth);
    }
    return lit / 16.0;
}

// Percentage closer soft shadows: the distance between the receiver and the
// average blocker drives the penumbra width, so a contact shadow stays sharp
// while a shadow cast from far away turns soft.
float PcssShadowFiltered(float2 uv, float depth, float texel, float2 rotation)
{
    const float searchRadius = max(shadowParams.x, 1.0);
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    [unroll]
    for (int index = 0; index < 16; ++index) {
        const float2 offset = RotateOffset(kShadowDisk[index], rotation) * texel * searchRadius;
        const float mapDepth = ShadowMapDepth(uv + offset);
        if (mapDepth < depth) {
            blockerSum += mapDepth;
            blockerCount += 1.0;
        }
    }
    if (blockerCount < 0.5) {
        return 1.0;
    }
    const float averageBlocker = blockerSum / blockerCount;
    // Penumbra width in light space grows with the receiver-to-blocker gap.
    // shadowParams.z scales the gap into texels for the current sun extent.
    const float penumbra = max(depth - averageBlocker, 0.0) / max(shadowParams.z, 1e-4);
    const float radius = clamp(penumbra, 1.0, max(shadowParams.x * 3.0, 1.0));
    return PcfShadowFiltered(uv, depth, radius, rotation, texel);
}

float PcfShadow(float4 lightSpace, float3 normal)
{
    if (lightSpace.w <= 0.0) {
        return 1.0;
    }
    const float3 projected = lightSpace.xyz / lightSpace.w;
    const float2 uv = ShadowUv(projected);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }
    const float bias = 0.00006 + 0.00030 * (1.0 - saturate(dot(normal, sunDirection.xyz)));
    const float depth = projected.z - bias;
    const float texel = shadingParams.w;
    const float2 rotation = ShadowDiskRotation(uv);

    const float lit = shadowParams.y > 0.5
        ? PcssShadowFiltered(uv, depth, texel, rotation)
        : PcfShadowFiltered(uv, depth, max(shadowParams.x, 0.0), rotation, texel);
    return lerp(shadingParams.y, 1.0, lit);
}

float4 ScenePS(MeshOutput input) : SV_TARGET
{
    const float4 albedoSample = texAlbedo.Sample(sampLinear, input.uv);
    float3 albedo = albedoSample.rgb * color.rgb;
    const float texAlpha = albedoSample.a;

    const float luminance = dot(albedo, float3(0.299, 0.587, 0.114));
    albedo = lerp(albedo, luminance.xxx, 0.18);

    const float3 normal = normalize(input.normal);
    const float3 sunDir = normalize(sunDirection.xyz);

    const float diffuse = max(dot(normal, sunDir), 0.0);
    const float shadow = PcfShadow(input.lightSpace, normal);
    // Hemisphere ambient: sky above, warm bounce below.
    const float upness = saturate(normal.z * 0.5 + 0.5);
    const float3 skyAmbient = lerp(float3(0.20, 0.20, 0.19), float3(0.50, 0.60, 0.80), upness);
    const float3 sunlight = float3(1.0, 0.96, 0.86) * diffuse * shadow * sunDirection.w;
    float3 lit = albedo * (skyAmbient * shadingParams.x + sunlight);

    // Dynamic lights: inverse-square falloff, spot cone, specular highlight.
    const int lightCount = min(int(shadingParams.z), 16);
    for (int index = 0; index < lightCount; ++index) {
        float3 L;
        float attenuation = 1.0;
        if (lightPosition[index].w < 0.5) {
            L = normalize(lightDirection[index].xyz);
        } else {
            const float3 toLight = lightPosition[index].xyz - input.world;
            const float distance = length(toLight);
            L = toLight / max(distance, 1e-4);
            const float radius = max(lightParams[index].x, 1e-4);
            const float inverseSquare = 1.0 / max(distance * distance, 1e-6);
            attenuation = inverseSquare * radius * radius;
            const float t = saturate(1.0 - distance / radius);
            attenuation *= t * t * (3.0 - 2.0 * t);
            attenuation = pow(max(attenuation, 0.0), max(lightParams[index].y, 0.01));
            if (lightPosition[index].w > 1.5) {
                const float cosAngle = dot(normalize(-L), normalize(lightDirection[index].xyz));
                attenuation *= smoothstep(lightParams[index].w, lightParams[index].z, cosAngle);
            }
        }

        const float ndl = max(dot(normal, L), 0.0);
        float3 contribution = albedo * lightColor[index].rgb * lightColor[index].a * ndl * attenuation;
        if (ndl > 0.001 && attenuation > 0.01) {
            const float3 view = normalize(cameraPosition.xyz - input.world);
            const float3 halfVector = normalize(L + view);
            const float nh = max(dot(normal, halfVector), 0.0);
            const float exponent = lerp(8.0, 128.0, luminance);
            const float specular = pow(nh, exponent) * attenuation * lightColor[index].a;
            contribution += float3(0.9, 0.92, 1.0) * specular * 0.35;
        }
        lit += contribution;
    }

    if (material.x > 0.001) {
        lit += albedo * material.x;
    }

    if (fogColor.a > 0.0) {
        const float viewDistance = length(cameraPosition.xyz - input.world);
        float density = fogColor.a;
        if (fogParams.x > 0.0) {
            const float height = max(input.world.z - fogParams.y, 0.0);
            density *= exp(-height * fogParams.x);
        }
        const float amount = saturate(1.0 - exp(-viewDistance * density));
        lit = lerp(lit, fogColor.rgb, amount);
    }

    return float4(lit, color.a * texAlpha);
}

// ---------------------------------------------------------------------------
// Procedural sky
// ---------------------------------------------------------------------------

struct FullscreenOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

FullscreenOutput FullscreenVS(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };
    FullscreenOutput output;
    const float2 corner = positions[vertexId];
    output.position = float4(corner, 0.0, 1.0);
    output.uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
    return output;
}

float4 SkyPS(FullscreenOutput input) : SV_TARGET
{
    const float aspect = screenParams.x / max(screenParams.y, 1.0);
    const float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    // The ray has to match the projection the world was drawn with, otherwise
    // the sky rotates at a different rate than the geometry in front of it.
    const float tanHalfFov = aaParams.z > 1.0e-6 ? aaParams.z : 1.0;
    const float3 direction = normalize(
        cameraForward.xyz +
        cameraRight.xyz * (ndc.x * aspect * tanHalfFov) +
        cameraUp.xyz * (ndc.y * tanHalfFov));

    const float elevation = clamp(direction.z, -1.0, 1.0);
    const float elevationDeg = elevation * 57.29578;
    const float daylight = saturate(sunDirection.w);
    const float3 zenith = lerp(float3(0.006, 0.010, 0.025), float3(0.030, 0.070, 0.180), daylight);
    const float3 mid = lerp(float3(0.010, 0.016, 0.040), float3(0.220, 0.380, 0.620), daylight);
    const float3 horizon = lerp(float3(0.020, 0.026, 0.048), float3(0.620, 0.720, 0.900), daylight);

    float3 sky;
    if (elevation >= 0.0) {
        const float3 lower = lerp(horizon, mid, saturate(elevationDeg / 30.0));
        sky = lerp(lower, zenith, saturate((elevationDeg - 30.0) / 60.0));
    } else {
        sky = lerp(horizon, horizon * 0.6, saturate(-elevationDeg / 25.0));
    }

    const float3 sunDir = normalize(sunDirection.xyz);
    const float cosAngle = dot(direction, sunDir);
    const float disk = smoothstep(0.9995, 0.9998, cosAngle);
    const float glow = pow(max(cosAngle, 0.0), 400.0) * 0.5 + pow(max(cosAngle, 0.0), 96.0) * 0.06;
    sky += float3(1.0, 0.92, 0.78) * (disk + glow) * sunDirection.w;

    if (fogColor.a > 0.0) {
        const float horizonWeight = 0.35 + 0.65 * (1.0 - saturate(elevation));
        sky = lerp(sky, fogColor.rgb, saturate(fogColor.a * horizonWeight));
    }
    return float4(sky, 1.0);
}

// ---------------------------------------------------------------------------
// Tone mapping
// ---------------------------------------------------------------------------

float3 AcesTonemap(float3 value)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((value * (a * value + b)) / (value * (c * value + d) + e));
}

float4 TonemapPS(FullscreenOutput input) : SV_TARGET
{
    const float3 hdr = texAlbedo.Sample(sampLinear, input.uv).rgb;
    const float3 mapped = AcesTonemap(hdr * screenParams.z);
    // The swapchain is a plain UNORM target, so the display encode is explicit.
    const float3 display = pow(max(mapped, 0.0), 1.0 / 2.2);
    return float4(display, 1.0);
}

float4 CopyPS(FullscreenOutput input) : SV_TARGET
{
    return texAlbedo.Sample(sampLinear, input.uv);
}

// ---------------------------------------------------------------------------
// Anti-aliasing
// ---------------------------------------------------------------------------

float LumaOf(float3 value)
{
    return dot(value, float3(0.299, 0.587, 0.114));
}

// Edge directed anti-aliasing: the local contrast gives the direction of the
// edge, and the two pixels along that direction are blended into it. Flat
// areas are left untouched, so only the silhouettes are softened.
float4 AaPs(FullscreenOutput input) : SV_TARGET
{
    const float2 texel = 1.0 / max(screenParams.xy, float2(1.0, 1.0));
    const float3 center = texAlbedo.Sample(sampLinear, input.uv).rgb;

    const float lumaNorthWest = LumaOf(texAlbedo.Sample(sampLinear, input.uv + float2(-1.0, -1.0) * texel).rgb);
    const float lumaNorthEast = LumaOf(texAlbedo.Sample(sampLinear, input.uv + float2(1.0, -1.0) * texel).rgb);
    const float lumaSouthWest = LumaOf(texAlbedo.Sample(sampLinear, input.uv + float2(-1.0, 1.0) * texel).rgb);
    const float lumaSouthEast = LumaOf(texAlbedo.Sample(sampLinear, input.uv + float2(1.0, 1.0) * texel).rgb);
    const float luma = LumaOf(center);

    const float lumaMin = min(luma, min(min(lumaNorthWest, lumaNorthEast), min(lumaSouthWest, lumaSouthEast)));
    const float lumaMax = max(luma, max(max(lumaNorthWest, lumaNorthEast), max(lumaSouthWest, lumaSouthEast)));
    const float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(0.0312, lumaMax * 0.125)) {
        return float4(center, 1.0);
    }

    // The edge runs along the diagonal with the smaller gradient.
    float2 direction = float2(-((lumaNorthWest + lumaNorthEast) - (lumaSouthWest + lumaSouthEast)),
                              ((lumaNorthWest + lumaSouthWest) - (lumaNorthEast + lumaSouthEast)));
    const float reduce = max((lumaNorthWest + lumaNorthEast + lumaSouthWest + lumaSouthEast) * 0.25 * 0.125,
                              0.0078125);
    const float inverseMinimum = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * inverseMinimum, -8.0, 8.0) * texel;

    const float3 rgbA = 0.5 * (texAlbedo.Sample(sampLinear, input.uv + direction * (1.0 / 3.0 - 0.5)).rgb +
                                texAlbedo.Sample(sampLinear, input.uv + direction * (2.0 / 3.0 - 0.5)).rgb);
    const float3 rgbB = rgbA * 0.5 + 0.25 * (texAlbedo.Sample(sampLinear, input.uv - direction * 0.5).rgb +
                                            texAlbedo.Sample(sampLinear, input.uv + direction * 0.5).rgb);
    const float lumaBlended = LumaOf(rgbB);
    const float3 resolved = (lumaBlended < lumaMin || lumaBlended > lumaMax) ? rgbA : rgbB;
    return float4(resolved, 1.0);
}

// Temporal resolve: the current frame is blended with the history image of
// the previous frame. The neighbourhood of the pixel limits how far the
// history may differ from the current frame, so a moving object leaves a
// short trail instead of a long smear.
float4 TaaPs(FullscreenOutput input) : SV_TARGET
{
    const float2 texel = 1.0 / max(screenParams.xy, float2(1.0, 1.0));
    const float3 current = texAlbedo.Sample(sampLinear, input.uv).rgb;

    // The scene was moved by the jitter, so the history is read where the
    // same world point stood in the previous frame.
    const float2 historyShift = float2(jitter.z - jitter.x, jitter.w - jitter.y);
    const float2 historyUv = input.uv + historyShift * texel;

    float3 minimum = current;
    float3 maximum = current;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float3 neighbour =
                texAlbedo.Sample(sampLinear, input.uv + float2(float(x), float(y)) * texel).rgb;
            minimum = min(minimum, neighbour);
            maximum = max(maximum, neighbour);
        }
    }

    const float3 history = texHistory.Sample(sampLinear, historyUv).rgb;
    const float3 clamped = clamp(history, minimum, maximum);
    const float weight = saturate(aaParams.x);
    return float4(lerp(current, clamped, weight), 1.0);
}

// ---------------------------------------------------------------------------
// User interface
// ---------------------------------------------------------------------------

struct UiInput
{
    float2 position : ATTRIBUTE0;
    float2 local    : ATTRIBUTE1;
    float4 color    : ATTRIBUTE2;
    float4 shape    : ATTRIBUTE3;  // x: half width, y: half height, z: corner radius
};

struct UiOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : TEXCOORD1;
    float2 local    : TEXCOORD2;
    float4 shape    : TEXCOORD3;
};

UiOutput UiVS(UiInput input)
{
    UiOutput output;
    const float2 ndc = float2(
        input.position.x / max(screenParams.x, 1.0) * 2.0 - 1.0,
        1.0 - input.position.y / max(screenParams.y, 1.0) * 2.0);
    output.position = float4(ndc, 0.0, 1.0);
    output.uv = input.local;
    output.color = input.color;
    output.local = input.local;
    output.shape = input.shape;
    return output;
}

float4 UiRectPS(UiOutput input) : SV_TARGET
{
    const float2 halfSize = input.shape.xy;
    const float radius = min(input.shape.z, min(halfSize.x, halfSize.y));
    const float2 delta = abs(input.local) - (halfSize - radius);
    const float distance = length(max(delta, 0.0)) + min(max(delta.x, delta.y), 0.0) - radius;
    const float coverage = saturate(0.5 - distance);
    if (coverage <= 0.0) {
        discard;
    }

    // Glass: the rect samples the already composited frame behind it (the
    // renderer binds the display image to t0 for this pass) and mixes the
    // scene with the panel colour. This is what makes a HUD panel look like
    // frosted glass instead of a flat sticker.
    if (uiBlur.x > 0.5) {
        const float2 uv = input.position.xy / max(screenParams.xy, float2(1.0, 1.0));
        const float3 backdrop = texAlbedo.Sample(sampLinear, uv).rgb;
        const float3 tinted = lerp(backdrop, input.color.rgb, saturate(input.color.a * uiBlur.y));
        return float4(tinted, coverage);
    }
    return float4(input.color.rgb, input.color.a * coverage);
}

// Separable Gaussian blur: one direction per pass, five bilinear taps stand in
// for a nine tap kernel. The UI backdrop runs it twice (horizontal, vertical)
// and repeats it `BlurConfig::iterations` times.
float4 BlurPS(FullscreenOutput input) : SV_TARGET
{
    const float2 texel = 1.0 / max(screenParams.xy, float2(1.0, 1.0));
    const float2 step = blurParams.xy * texel * max(blurParams.z, 0.0);
    float3 sum = texAlbedo.Sample(sampLinear, input.uv).rgb * 0.2270270270;
    sum += texAlbedo.Sample(sampLinear, input.uv + step * 1.3846153846).rgb * 0.3162162162;
    sum += texAlbedo.Sample(sampLinear, input.uv - step * 1.3846153846).rgb * 0.3162162162;
    sum += texAlbedo.Sample(sampLinear, input.uv + step * 3.2307692308).rgb * 0.0702702703;
    sum += texAlbedo.Sample(sampLinear, input.uv - step * 3.2307692308).rgb * 0.0702702703;
    return float4(sum, 1.0);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

struct UiTextInput
{
    float2 position : ATTRIBUTE0;
    float2 uv       : ATTRIBUTE1;
    float4 color    : ATTRIBUTE2;
};

struct UiTextOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

UiTextOutput UiTextVS(UiTextInput input)
{
    UiTextOutput output;
    const float2 ndc = float2(
        input.position.x / max(screenParams.x, 1.0) * 2.0 - 1.0,
        1.0 - input.position.y / max(screenParams.y, 1.0) * 2.0);
    output.position = float4(ndc, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 UiTextPS(UiTextOutput input) : SV_TARGET
{
    const float4 texel = texAlbedo.Sample(sampLinear, input.uv);
    float coverage = texel.a;
    if (uiParams.x > 0.5) {
        // MSDF: the median of the three channels is the signed distance to
        // the glyph edge, mapped to a coverage value.
        const float median = min(max(texel.r, texel.g), max(min(texel.r, texel.g), texel.b));
        const float range = max(uiParams.y, 0.0001);
        coverage = saturate(median / range + 0.5);
    }
    return float4(input.color.rgb, coverage * input.color.a);
}

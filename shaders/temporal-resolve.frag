#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uCurrent;
uniform sampler2D uDepth;
uniform sampler2D uHistory;
uniform mat4 uInverseViewProjection;
uniform mat4 uPreviousViewProjection;
uniform int uHistoryValid;
uniform float uHistoryWeight;
uniform float uNearPlane;
uniform float uFarPlane;

float linearDepth(float deviceDepth) {
    float z = deviceDepth * 2.0 - 1.0;
    return (2.0 * uNearPlane * uFarPlane) /
           max(uFarPlane + uNearPlane - z * (uFarPlane - uNearPlane), 0.00001);
}

void main() {
    vec3 current = texture(uCurrent, vUV).rgb;
    float deviceDepth = texture(uDepth, vUV).r;
    // History alpha stores linear camera distance / far plane, not gl_FragDepth.
    // 16-bit float alpha then has useful depth precision even far from camera.
    float depth = deviceDepth < 0.99999
        ? linearDepth(deviceDepth) / uFarPlane : 1.0;
    FragColor = vec4(current, depth);

    if (uHistoryValid == 0 || deviceDepth >= 0.99999) return;

    vec4 world = uInverseViewProjection *
                 vec4(vUV * 2.0 - 1.0, deviceDepth * 2.0 - 1.0, 1.0);
    if (abs(world.w) < 0.000001) return;
    world /= world.w;
    vec4 previous = uPreviousViewProjection * world;
    if (previous.w <= 0.0) return;
    vec2 previousUV = (previous.xy / previous.w) * 0.5 + 0.5;
    // Reject samples outside the previous frame instead of smearing edges.
    if (any(lessThan(previousUV, vec2(0.0))) ||
        any(greaterThan(previousUV, vec2(1.0)))) return;

    vec4 history = texture(uHistory, previousUV);
    float previousLinearDepth = previous.w / uFarPlane;
    // Disocclusion rejection: history must belong to the same world surface.
    float depthTolerance = max(0.08 / uFarPlane, previousLinearDepth * 0.04);
    if (abs(history.a - previousLinearDepth) > depthTolerance) return;

    // Neighborhood clamp suppresses trails from animated lights/geometry.
    vec2 texel = 1.0 / vec2(textureSize(uCurrent, 0));
    vec3 minimumColor = vec3(1.0e10);
    vec3 maximumColor = vec3(-1.0e10);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 uv = clamp(vUV + vec2(x, y) * texel, texel * 0.5,
                            vec2(1.0) - texel * 0.5);
            vec3 neighboring = texture(uCurrent, uv).rgb;
            minimumColor = min(minimumColor, neighboring);
            maximumColor = max(maximumColor, neighboring);
        }
    }
    vec3 clampedHistory = clamp(history.rgb, minimumColor, maximumColor);

    float movementPixels = length((previousUV - vUV) / texel);
    float motionWeight = clamp(1.0 - movementPixels / 24.0, 0.08, 1.0);
    float difference = length(current - clampedHistory);
    float contrastWeight = 1.0 / (1.0 + difference * 2.5);
    float weight = clamp(uHistoryWeight * motionWeight * contrastWeight, 0.0, 0.98);
    FragColor.rgb = mix(current, clampedHistory, weight);
}

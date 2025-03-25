// Enhanced sky fragment shader
varying vec3 vPosition;
varying vec3 vWorldPos;
varying vec2 vUV;

// Procedural sky parameters
uniform float time;            // Time for animation and day cycle
uniform float sunHeight;       // Height of sun (0.0 to 1.0)
uniform float cloudiness;      // Cloud coverage (0.0 to 1.0)

// Advanced noise functions
float hash(float n) {
    return fract(sin(n) * 43758.5453);
}

// Simplex-like noise function
float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    
    // Cubic interpolation
    f = f*f*(3.0-2.0*f);
    
    float n = i.x + i.y*57.0 + i.z*113.0;
    
    float a = hash(n);
    float b = hash(n+1.0);
    float c = hash(n+57.0);
    float d = hash(n+58.0);
    float e = hash(n+113.0);
    float f1 = hash(n+114.0);
    float g = hash(n+170.0);
    float h = hash(n+171.0);
    
    float result = mix(mix(mix(a, b, f.x),
                           mix(c, d, f.x), f.y),
                       mix(mix(e, f1, f.x),
                           mix(g, h, f.x), f.y), f.z);
    
    return result;
}

// Fractal Brownian Motion
float fbm(vec3 p) {
    float f = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    for(int i = 0; i < 5; i++) {
        f += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    
    return f;
}

// Atmospheric scattering approximation
vec3 atmosphere(vec3 rayDir, vec3 sunDir) {
    // Rayleigh scattering coefficients
    const vec3 rayleighCoeff = vec3(5.8e-6, 13.5e-6, 33.1e-6);
    
    // Amount of Rayleigh scattering
    float rayleighFactor = 1.0 + pow(1.0 - max(0.0, dot(rayDir, sunDir)), 5.0) * 2.0;
    vec3 rayleigh = rayleighCoeff * rayleighFactor;
    
    // Mie scattering approximation
    float mieFactor = pow(max(0.0, dot(rayDir, sunDir)), 8.0) * 0.5;
    vec3 mie = vec3(mieFactor);
    
    // Handle both upper and lower hemisphere
    // For the lower hemisphere, we use a ground color or reflection
    float absZenith = abs(rayDir.y);
    float signZenith = sign(rayDir.y);
    float atmosphericThickness = (1.0 - absZenith);
    
    // Colors for upper hemisphere (sky)
    vec3 day_zenith = vec3(0.0, 0.1, 0.6);
    vec3 day_horizon = vec3(0.3, 0.5, 0.9);
    vec3 night_zenith = vec3(0.0, 0.0, 0.1);
    vec3 night_horizon = vec3(0.05, 0.05, 0.1);
    
    // Colors for lower hemisphere (ground reflection or solid color)
    vec3 ground_color = vec3(0.1, 0.1, 0.15); // Dark grayish-blue for below horizon
    
    vec3 baseColor;
    
//    if (signZenith >= 0.0) {
        // Upper hemisphere (sky)
        // Day-night interpolation
        vec3 zenithColor = mix(night_zenith, day_zenith, sunHeight);
        vec3 horizonColor = mix(night_horizon, day_horizon, sunHeight);
        
        // Base sky color
        //baseColor = mix(zenithColor, horizonColor, atmosphericThickness);
        baseColor = horizonColor;
        
        // Add Rayleigh and Mie scattering
        //baseColor += rayleigh * sunHeight;
        //baseColor += mie * sunHeight;
    //} else {
    //    // Lower hemisphere (ground)
      //  // Darken the ground color based on time of day
        //baseColor = ground_color * max(0.2, sunHeight);
        
        // Add a subtle gradient toward horizon
        //baseColor = mix(baseColor, mix(night_horizon, day_horizon, sunHeight), pow(1.0 - absZenith, 8.0));
//    }
    
    return baseColor;
}

// Cloud shape and structure
float cloudShape(vec3 p, float coverage) {
    float base = fbm(p * 0.3);
    float detail = fbm(p * 1.2 + vec3(time * 0.05, 0.0, time * 0.03));
    
    // Adjust cloud coverage with parameter
    float cloudCoverage = 0.4 + coverage * 0.4;
    
    // Shape the clouds
    float clouds = smoothstep(cloudCoverage, cloudCoverage + 0.2, base + detail * 0.2);
    
    return clouds;
}

// Cloud lighting
vec3 cloudLighting(float cloudDensity, vec3 cloudPos, vec3 sunDir) {
    // Base cloud color
    vec3 cloudColor = vec3(0.9, 0.9, 0.95);
    
    // Directional lighting
    float sunLight = max(0.0, dot(normalize(vec3(0.0, 1.0, 0.0)), sunDir));
    float directLight = pow(sunLight, 4.0) * 0.5 + 0.5;
    
    // Cloud self-shadowing
    float selfShadow = 1.0 - cloudDensity * 0.5;
    
    // Sunset coloring
    float sunset = pow(max(0.0, dot(normalize(vec3(0.0, 0.3, 1.0)), sunDir)), 2.0);
    vec3 sunsetColor = mix(vec3(1.0, 0.8, 0.5), vec3(1.0, 0.4, 0.2), sunset);
    
    // Apply lighting
    cloudColor = cloudColor * mix(vec3(0.5, 0.5, 0.65), sunsetColor, directLight) * selfShadow;
    
    // Darken clouds at night
    cloudColor = mix(cloudColor * 0.2, cloudColor, sunHeight);
    
    return cloudColor;
}

void main()
{
    // Normalize direction for skybox
    vec3 dir = normalize(vPosition);
    
    // Calculate sun direction
    float sunTheta = 3.14159 * (0.5 - sunHeight);
    vec3 sunDir = normalize(vec3(0.0, cos(sunTheta), sin(sunTheta)));
    
    // Atmospheric scattering
    vec3 skyColor = atmosphere(dir, sunDir);
    
    // Sun disk with realistic shape
    float sunDot = max(0.0, dot(dir, sunDir));
    float sunDisk = pow(sunDot, 256.0);
    float sunGlow = pow(sunDot, 8.0) * 0.3;
    
    // Sun color varies with height (redder at horizon)
    vec3 sunColor = mix(vec3(1.0, 0.6, 0.3), vec3(1.0, 0.9, 0.7), sunHeight);
    
    // Add sun to sky
    skyColor += sunDisk * sunColor;
    skyColor += sunGlow * vec3(1.0, 0.4, 0.2) * sunHeight;
    
    // Clouds - Only render clouds in the upper hemisphere
    float clouds = 0.0;
    if (dir.y > 0.05) {
        // Project clouds onto a dome
        float cloudHeight = 200.0;
        vec3 cloudPos = dir * (cloudHeight / dir.y);
        
        // Add time-based movement
        cloudPos += vec3(time * 2.0, 0.0, time * 1.0);
        
        // Get cloud density
        clouds = cloudShape(cloudPos * 0.01, cloudiness);
        
        // Fade clouds at horizon
        clouds *= smoothstep(0.05, 0.1, dir.y);
        
        // If we have clouds, apply cloud lighting
        if (clouds > 0.01) {
            vec3 cloudColor = cloudLighting(clouds, cloudPos, sunDir);
            skyColor = mix(skyColor, cloudColor, clouds * 0.9);
        }
    }
    
    // Stars at night - only in upper hemisphere
    if (sunHeight < 0.2 && dir.y > 0.0) {
        // Make stars depend on position
        float starPattern = noise(dir * 100.0);
        starPattern = pow(starPattern, 20.0);
        
        // Star brightness - fade near horizon
        float horizonFade = smoothstep(0.0, 0.4, dir.y);
        float starBrightness = (1.0 - sunHeight * 5.0) * 0.3 * horizonFade;
        starBrightness = max(0.0, starBrightness);
        
        // Different star colors
        vec3 starColor = mix(vec3(0.8, 0.9, 1.0), vec3(1.0, 0.9, 0.8), noise(dir * 10.0));
        
        // Add stars to sky
        skyColor += starPattern * starBrightness * starColor;
    }
    
    // Final color
    gl_FragColor = vec4(skyColor, 1.0);
}
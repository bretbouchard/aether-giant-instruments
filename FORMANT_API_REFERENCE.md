# Giant Voice Formant API Reference

## Quick Reference for New Features

### 1. Vowel Shapes (Extended)

```cpp
enum class VowelShape {
    Ah,     // Wide open, lowest F1 (default)
    Eh,     // Mid openness
    Ee,     // High F2, "beet" sound
    Oh,     // Rounded, low F2
    Oo,     // Closed, lowest F1 and F2
    Uh,     // Mid-rounded
    Ih,     // High F2, "bit" sound
    Custom  // User-defined frequencies
};
```

**Usage:**
```cpp
FormantStack::Parameters params;
params.vowelShape = FormantStack::VowelShape::Ee;  // "Ee" vowel
params.openness = 0.5f;
params.giantScale = 0.6f;  // 0.6 = giant, 1.0 = human
formants.setParameters(params);
```

### 2. GiantFormantFilter Methods

**New Methods:**
```cpp
// Set bandwidth in Hz (more intuitive for speech synthesis)
void setBandwidthHz(float bwHz);

// Set bandwidth using Q factor (Q = frequency / bandwidth)
void setQ(float q);

// Existing method (bandwidth in octaves)
void setBandwidth(float bw);
```

**Examples:**
```cpp
GiantFormantFilter formant;
formant.setFrequency(1000.0f);

// Method 1: Set bandwidth in Hz (recommended for speech)
formant.setBandwidthHz(100.0f);  // 100 Hz bandwidth

// Method 2: Set using Q factor
formant.setQ(10.0f);  // Q = 10

// Method 3: Set bandwidth in octaves (existing)
formant.setBandwidth(0.5f);  // 0.5 octaves
```

### 3. FormantStack Parameters

**New Parameter:**
```cpp
struct Parameters {
    VowelShape vowelShape = VowelShape::Ah;
    float formantDrift = 0.1f;
    float openness = 0.5f;
    float giantScale = 0.6f;  // NEW: Scale factor (1.0 = human, 0.6 = giant)

    // Custom formant frequencies (when vowelShape = Custom)
    float f1 = 600.0f;
    float f2 = 1200.0f;
    float f3 = 2500.0f;
    float f4 = 3500.0f;
    float f5 = 4500.0f;
};
```

**Usage:**
```cpp
FormantStack::Parameters params;
params.vowelShape = FormantStack::VowelShape::Oo;
params.openness = 0.3f;        // More closed
params.formantDrift = 0.15f;   // Slower drift
params.giantScale = 0.7f;      // Slightly smaller giant
formants.setParameters(params);
```

### 4. Utility Functions

**Calculate Formant Q:**
```cpp
float calculateFormantQ(float formantFreq, float bandwidthHz);
```
Returns Q factor for a given formant frequency and bandwidth.

**Example:**
```cpp
float q = calculateFormantQ(1000.0f, 100.0f);  // Q = 10.0
```

**Bandwidth Conversion:**
```cpp
float bandwidthHzToOctaves(float bandwidthHz, float centerFreq);
```
Converts bandwidth in Hz to bandwidth in octaves.

**Example:**
```cpp
float bwOctaves = bandwidthHzToOctaves(100.0f, 1000.0f);  // ~0.144 octaves
```

**Get Vowel Formants:**
```cpp
VowelFormants getVowelFormants(int vowelIndex, float scale = 0.6f);
```
Returns formant frequencies and bandwidths for a vowel.

**Vowel Indices:**
- 0 = Ah
- 1 = Eh
- 2 = Ee
- 3 = Oh
- 4 = Oo
- 5 = Uh
- 6 = Ih

**Example:**
```cpp
VowelFormants ahVowel = getVowelFormants(0, 0.6f);  // "Ah" at giant scale
std::cout << "F1: " << ahVowel.f1 << " Hz, B1: " << ahVowel.b1 << " Hz" << std::endl;
```

### 5. VowelFormants Structure

```cpp
struct VowelFormants {
    const char* name;  // Vowel name
    float f1, f2, f3, f4;  // Formant frequencies (Hz)
    float b1, b2, b3, b4;  // Formant bandwidths (Hz)
};
```

## Usage Examples

### Example 1: Basic Vowel Selection

```cpp
// Create formant stack
FormantStack formants;
formants.prepare(48000.0);

// Set to "Ee" vowel (high F2, "beet" sound)
FormantStack::Parameters params;
params.vowelShape = FormantStack::VowelShape::Ee;
params.openness = 0.5f;
params.giantScale = 0.6f;
formants.setParameters(params);

// Process audio
float output = formants.processSample(input);
```

### Example 2: Custom Formants with Q-Based Bandwidth

```cpp
// Set custom formant frequencies
FormantStack::Parameters params;
params.vowelShape = FormantStack::VowelShape::Custom;
params.f1 = 500.0f;
params.f2 = 1500.0f;
params.f3 = 2500.0f;
params.f4 = 3500.0f;
formants.setParameters(params);

// Or set formant directly with Q
formants[0].setFrequency(500.0f);
formants[0].setQ(8.0f);  // Q = 8
formants[0].setAmplitude(1.0f);
```

### Example 3: Scale Interpolation

```cpp
// Interpolate between giant and human scales
for (float scale = 0.6f; scale <= 1.0f; scale += 0.1f) {
    VowelFormants vowel = getVowelFormants(0, scale);  // "Ah"
    std::cout << "Scale " << scale << ": F1 = " << vowel.f1 << " Hz" << std::endl;
}

// Output:
// Scale 0.6: F1 = 440 Hz (giant)
// Scale 0.7: F1 = 512 Hz
// Scale 0.8: F1 = 585 Hz
// Scale 0.9: F1 = 658 Hz
// Scale 1.0: F1 = 730 Hz (human)
```

### Example 4: Vowel Transition

```cpp
// Smooth vowel transition (Ah -> Ee)
FormantStack::Parameters params;
float transitionTime = 1.0f;  // 1 second
float sampleRate = 48000.0f;
int totalSamples = static_cast<int>(transitionTime * sampleRate);

for (int i = 0; i < totalSamples; i++) {
    float t = static_cast<float>(i) / totalSamples;

    // Interpolate between vowels
    if (t < 0.5f) {
        params.vowelShape = FormantStack::VowelShape::Ah;
    } else {
        params.vowelShape = FormantStack::VowelShape::Ee;
    }

    // Update formants (smooth transition handled internally)
    formants.setParameters(params);

    // Process sample
    float output = formants.processSample(input);
}
```

## Formant Frequency Ranges

### Standard Human Voice (Adult Male)
```
Vowel  F1 Range   F2 Range   F3 Range   F4 Range
Ah     600-800    1000-1200  2400-2600  3300-3500
Eh     400-600    1700-1900  2400-2600  3300-3400
Ee     250-300    2200-2400  2900-3100  3300-3400
Oh     500-600    800-900    2300-2500  3300-3400
Oo     250-350    800-900    2200-2300  3300-3400
Uh     600-700    1100-1300  2300-2500  3300-3400
Ih     350-450    2200-2400  2900-3000  3300-3400
```

### Giant-Scaled Voice (0.6x Scale)
```
Vowel  F1 Range   F2 Range   F3 Range   F4 Range
Ah     360-480    600-720    1440-1560  1980-2040
Eh     240-360    1020-1140  1470-1590  1980-2040
Ee     150-180    1320-1440  1800-1920  1980-2040
Oh     300-360    480-540    1410-1530  1980-2040
Oo     150-210    480-540    1340-1400  1980-2040
Uh     360-420    660-780    1380-1500  1980-2040
Ih     210-270    1320-1440  1740-1860  1980-2040
```

## Bandwidth Guidelines

### Typical Bandwidth Values (Hz)
```
Formant  Human      Giant
F1       50-80      70-120
F2       70-100     90-150
F3       100-120    150-180
F4       120-130    180-195
```

### Q Factor Ranges
```
Formant  Q Range (Human)    Q Range (Giant)
F1       5-15               4-8
F2       10-25              8-12
F3       20-30              12-18
F4       25-35              15-20
```

## Tips for Best Vocal Quality

1. **Use lookup tables for vowels** - More realistic than manual frequency setting
2. **Set appropriate bandwidth** - 50-150 Hz for human, 70-195 Hz for giant
3. **Use Q factor for control** - Higher Q = more resonant, lower Q = more breathy
4. **Adjust openness carefully** - Small changes (0.3-0.7) have big effects
5. **Use formant drift sparingly** - 0.05-0.15 for subtle movement
6. **Match scale to bandwidth** - Giant scale needs wider bandwidths

## Troubleshooting

### Problem: Vowels sound too similar
**Solution:** Check that bandwidths are set correctly using `setBandwidthHz()`. Each vowel should have distinct F1/F2 ratios.

### Problem: Formants too resonant/peaky
**Solution:** Reduce Q factor or increase bandwidth. Try `setBandwidthHz(150.0f)` for wider formants.

### Problem: Formants too muffled
**Solution:** Increase Q factor or decrease bandwidth. Try `setBandwidthHz(80.0f)` for narrower formants.

### Problem: Transitions sound abrupt
**Solution:** Reduce `formantDrift` parameter or use slower parameter changes.

### Problem: Giant voice sounds too human
**Solution:** Decrease `giantScale` parameter (try 0.5-0.6) and increase bandwidths.

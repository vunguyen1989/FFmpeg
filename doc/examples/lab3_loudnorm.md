# Loudnorm Filter Analysis

## Filter Command
```
-af loudnorm=I=-14:TP=-1:LRA=7
```

## Does loudnorm internally upsample?

**No, loudnorm does NOT internally upsample.**

The `input_srate[] = {192000, -1}` array in `query_formats()` only declares that the filter *accepts* input up to 192kHz - it does NOT mean it upsamples.

Key points:
- No resampling code exists in the filter (no `swr_*` calls)
- Output sample rate = input sample rate (inherited)
- For accurate true peak detection, resample to 192kHz *before* loudnorm:
  ```
  afresample=192000,loudnorm=I=-14:TP=-1:LRA=7
  ```

## Flow Chart

```
+-----------------------------------------------------------------+
|                    INPUT AUDIO STREAM                            |
|              (sample_rate, channels, format)                     |
+-----------------------------+-----------------------------------+
                              |
                              v
+-----------------------------------------------------------------+
|                    FRAME TYPE DETECTION                          |
|         (based on audio length & linear parameters)              |
+-----------------------------+-----------------------------------+
                              |
             +----------------+----------------+
             |                                 |
             v                                 v
    +-------------------+           +-------------------+
    |   FIRST_FRAME     |           |  LINEAR_MODE      |
    |  (audio > 3s)     |           | (short audio      |
    +--------+----------+           |  or measured_*    |
             |                     |  provided)        |
             |                     +--------+----------+
             v                              |
             |                              |
             v                              v
+-----------------------------------------------------------------+
|              EBU R128 LOUDNESS MEASUREMENT                       |
|  +-----------------------------------------------------------+  |
|  |  - Integrated Loudness (I)     <- -14 LUFS (target)        |  |
|  |  - True Peak (TP)              <- -1 dBTP (limit)          |  |
|  |  - Loudness Range (LRA)        <- 7 LU (dynamic range)     |  |
|  |  - Short-term Loudness                                   |  |
|  |  - Momentary Loudness                                    |  |
|  +-----------------------------------------------------------+  |
+-----------------------------+-----------------------------------+
                              |
                              v
+-----------------------------------------------------------------+
|                    GAIN CALCULATION                              |
|                                                                  |
|   offset_gain = target_I - measured_I                            |
|                = -14 - measured_LUFS                             |
|                                                                  |
|   true_peak_check = measured_TP + offset_gain                    |
|                     <= -1 dBTP ?                                 |
|                                                                  |
|   delta[n] = pow(10, (env_global + env_shortterm) / 20)         |
|              where env_shortterm = target_I - shortterm          |
+-----------------------------+-----------------------------------+
                              |
                              v
+-----------------------------------------------------------------+
|              GAUSSIAN SMOOTHING (3-second window)                |
|                                                                  |
|   +---------------------------------------------------------+   |
|   |  weights[21] = Gaussian distribution (sigma=3.5)          |   |
|   |                                                          |   |
|   |  30-frame circular buffer of delta values                |   |
|   |  +---+---+---+---+       +---+---+                       |   |
|   |  | 0 | 1 | 2 |...|  ...  |28 |29 |  (3s / 100ms = 30)    |   |
|   |  +---+---+---+---+       +---+---+                       |   |
|   |       ^                                                  |   |
|   |    current                                               |   |
|   +---------------------------------------------------------+   |
+-----------------------------+-----------------------------------+
                              |
                              v
+-----------------------------------------------------------------+
|                 TRUE PEAK LIMITER                                |
|                                                                  |
|   +---------------------------------------------------------+   |
|   |  limiter_buf: 210ms circular buffer                      |   |
|   |                                                          |   |
|   |  Peak Detection:                                         |   |
|   |  1. Find samples > -1 dBTP (0.891 dB linear)             |   |
|   |  2. Look 2-12 samples ahead to confirm peak              |   |
|   |                                                          |   |
|   |  Limiter States:                                         |   |
|   |  +----------+                                           |   |
|   |  |   OUT    | <- No peaks, pass through                 |   |
|   |  +----------+                                           |   |
|   |  |  ATTACK  | <- Gain reduction ramp up                 |   |
|   |  +----------+                                           |   |
|   |  | SUSTAIN  | <- Hold gain reduction                    |   |
|   |  +----------+                                           |   |
|   |  | RELEASE  | <- Gain reduction ramp down               |   |
|   |  +----------+                                           |   |
|   |                                                          |   |
|   |  attack_length =  10ms worth of samples                 |   |
|   |  release_length = 100ms worth of samples                |   |
|   +---------------------------------------------------------+   |
+-----------------------------+-----------------------------------+
                              |
                              v
+-----------------------------------------------------------------+
|                 APPLY FINAL GAIN                                |
|                                                                  |
|   output_sample = input_sample x delta x offset x limiter_env   |
|                                                                  |
|   output = clamp(output, -1 dBTP)  <- Hard ceiling at TP=-1     |
+-----------------------------+-----------------------------------+
                              |
                              v
+-----------------------------------------------------------------+
|                 OUTPUT AUDIO STREAM                              |
|              (same sample_rate as input)                         |
|                                                                  |
|   Achieves:                                                     |
|   [check] Integrated Loudness = -14 LUFS (+/-0.5 tolerance)      |
|   [check] True Peak <= -1 dBTP                                   |
|   [check] Loudness Range ~= 7 LU (preserved dynamic contrast)    |
+-----------------------------------------------------------------+
```

## Parameter Summary

| Parameter | Value | Effect |
|-----------|-------|--------|
| `I=-14` | Target integrated loudness | Normalizes so average loudness = -14 LUFS |
| `TP=-1` | Maximum true peak | Hard limit - audio won't exceed -1 dBTP |
| `LRA=7` | Target loudness range | Preserves ~7 LU dynamic range (not compressed to death) |

## Two-Pass Usage

In practice, loudnorm is typically run twice:
1. **Pass 1:** Measure without target -> get measured values
2. **Pass 2:** Apply normalization with measured values -> achieve target

Example:
```bash
# Pass 1: Measure
ffmpeg -i input.mp4 -af loudnorm=print_format=json -f null -

# Pass 2: Apply (using measured values from pass 1)
ffmpeg -i input.mp4 -af loudnorm=I=-14:TP=-1:LRA=7:measured_I=-23.5:measured_TP=-2.1:measured_LRA=8.2:measured_thresh=-33.5 output.mp4
```
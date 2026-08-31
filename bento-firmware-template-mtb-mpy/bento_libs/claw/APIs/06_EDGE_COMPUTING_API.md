# 06 -- Edge Computing API

BENTO PSoC Edge E84 Firmware SDK -- DSP Filters, IMU Fusion, and Sensor Analytics

---

## Architecture Overview

The `dsp` module provides on-device signal processing for real-time sensor data analysis. All computation runs on CM33_NS within the MicroPython runtime. The module is structured across three source files:

| File | Contents |
|------|----------|
| `moddsp.c` | Module shell, stateless environment/IMU functions |
| `moddsp_filters.c` | Filter classes: EMA, SMA, LPF, HPF, Median, Kalman1D |
| `moddsp_imu.c` | IMU algorithm classes: Madgwick AHRS, Pedometer |

All filter and IMU classes follow a consistent API:
```python
obj = dsp.FilterType(params...)
result = obj.update(sample)    # Feed new sample, return processed value
obj.value()                    # Read last output without feeding new data
obj.reset()                    # Reset internal state
```

---

## 1. Digital Filter Classes

**Source:** `common/mpy/moddsp_filters.c`

### dsp.EMA -- Exponential Moving Average

Implements first-order IIR smoothing: `y = alpha * x + (1 - alpha) * y_prev`

Good for: general-purpose noise reduction with minimal memory footprint.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `alpha` | float (keyword) | 0.1 | Smoothing factor. Range 0.0-1.0. Lower = more smoothing. |

| Method | Signature | Returns | Description |
|--------|-----------|---------|-------------|
| `update(sample)` | `update(sample)` | `float` | Feed new sample, return filtered value. First sample initializes output directly. |
| `value()` | `value()` | `float` | Return last filtered output. |
| `reset()` | `reset()` | `None` | Reset state. Next `update()` reinitializes. |

**Choosing alpha:**
- 0.01-0.05: Heavy smoothing, large lag. Good for slow-changing sensors (temperature, pressure).
- 0.1-0.3: Moderate smoothing. Good for accelerometer data at 100Hz.
- 0.5-0.9: Light smoothing, fast response. Good for real-time control.

```python
import dsp, sensors, time

sensors.init()
f = dsp.EMA(alpha=0.1)

while True:
    data = sensors.read()
    smooth = f.update(data['ax'])
    print(f"Raw: {data['ax']:.2f}  Filtered: {smooth:.2f}")
    time.sleep_ms(10)
```

### dsp.SMA -- Simple Moving Average

Circular buffer averaging over the last N samples. Equal weight to all samples in the window.

Good for: removing periodic noise, computing rolling averages for dashboards.

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `window` | int (keyword) | 10 | 2-64 | Window size (number of samples). |

| Method | Signature | Returns | Description |
|--------|-----------|---------|-------------|
| `update(sample)` | `update(sample)` | `float` | Feed sample, return current average. Partial average until buffer fills. |
| `value()` | `value()` | `float` | Return current average (0.0 if no samples). |
| `reset()` | `reset()` | `None` | Clear buffer, reset count and sum. |

**Memory usage:** Allocates `window * 4` bytes for the float buffer.

```python
import dsp

avg = dsp.SMA(window=20)
for val in [1.0, 2.0, 3.0, 4.0, 5.0]:
    print(avg.update(val))  # Partial average until 20 samples
```

### dsp.LPF -- First-Order Low-Pass IIR Filter

Attenuates frequencies above the cutoff. Computed as `alpha = dt / (RC + dt)` where `RC = 1 / (2 * pi * cutoff)` and `dt = 1 / fs`.

Good for: removing high-frequency sensor noise while preserving slow trends.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cutoff` | float (keyword) | 5.0 | Cutoff frequency in Hz. |
| `fs` | float (keyword) | 100.0 | Sampling frequency in Hz. |

| Method | Signature | Returns | Description |
|--------|-----------|---------|-------------|
| `update(sample)` | `update(sample)` | `float` | Feed sample, return filtered value. |
| `value()` | `value()` | `float` | Return last output. |
| `reset()` | `reset()` | `None` | Reset filter state. |

```python
import dsp

# BMI270 accelerometer at 100Hz, remove vibration above 5Hz
lpf = dsp.LPF(cutoff=5.0, fs=100.0)
smooth_accel = lpf.update(raw_accel)
```

### dsp.HPF -- First-Order High-Pass IIR Filter

Attenuates frequencies below the cutoff. Computed as `alpha = RC / (RC + dt)`. Output: `y = alpha * (y_prev + x - x_prev)`.

Good for: removing DC offset, isolating dynamic motion from gravity, detecting sudden changes.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cutoff` | float (keyword) | 0.5 | Cutoff frequency in Hz. |
| `fs` | float (keyword) | 100.0 | Sampling frequency in Hz. |

| Method | Signature | Returns | Description |
|--------|-----------|---------|-------------|
| `update(sample)` | `update(sample)` | `float` | Feed sample, return filtered value. |
| `value()` | `value()` | `float` | Return last output. |
| `reset()` | `reset()` | `None` | Reset filter state. |

```python
import dsp

# Remove gravity (DC component) from accelerometer
hpf = dsp.HPF(cutoff=0.5, fs=100.0)
dynamic_accel = hpf.update(raw_accel)  # Only motion component
```

### dsp.Median -- Median Filter

Non-linear filter that outputs the median of the last N samples. Uses insertion sort on a circular buffer.

Good for: removing impulse noise (spikes), sensor glitches, and outlier rejection.

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `window` | int (keyword) | 5 | 3-15 | Window size (forced to odd if even). |

| Method | Signature | Returns | Description |
|--------|-----------|---------|-------------|
| `update(sample)` | `update(sample)` | `float` | Feed sample, return median of current window. |
| `value()` | `value()` | `float` | Return last computed median. |
| `reset()` | `reset()` | `None` | Clear buffer, reset state. |

```python
import dsp

# Remove occasional radar distance spikes
med = dsp.Median(window=7)
clean_distance = med.update(raw_distance)
```

### dsp.Kalman1D -- Scalar Kalman Filter

Optimal estimator for noisy scalar measurements. Uses predict-update cycle:
- Predict: `p += q`
- Update: `k = p / (p + r)`, `x += k * (z - x)`, `p *= (1 - k)`

Good for: high-quality estimation of slowly changing values (altitude, temperature), sensor fusion of single-axis data.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `q` | float (keyword) | 0.01 | Process noise covariance. Higher = expect faster changes. |
| `r` | float (keyword) | 0.1 | Measurement noise covariance. Higher = trust measurements less. |

| Method | Signature | Returns | Description |
|--------|-----------|---------|-------------|
| `update(measurement)` | `update(measurement)` | `float` | Feed measurement, return estimated state. |
| `value()` | `value()` | `float` | Return current state estimate. |
| `reset()` | `reset()` | `None` | Reset state (x=0, p=1, uninitialized). |

**Tuning guide:**
- `q/r` ratio determines responsiveness vs. smoothness
- `q = 0.001, r = 1.0`: Very smooth, slow response (barometric altitude)
- `q = 0.1, r = 0.1`: Fast tracking, moderate noise (accelerometer)
- `q = 1.0, r = 0.01`: Nearly unfiltered (trust measurements)

```python
import dsp, sensors, time

sensors.init()
kf = dsp.Kalman1D(q=0.001, r=0.5)

while True:
    data = sensors.read()
    altitude = dsp.altitude(data['pressure'])
    smooth_alt = kf.update(altitude)
    print(f"Altitude: {smooth_alt:.1f} m")
    time.sleep_ms(100)
```

---

## 2. IMU Algorithm Classes

**Source:** `common/mpy/moddsp_imu.c`

### dsp.Madgwick -- 6-DOF AHRS (Attitude and Heading Reference System)

Implements the Madgwick gradient descent AHRS algorithm for fusing accelerometer and gyroscope data into orientation quaternions. Converts to Euler angles (roll, pitch, yaw) automatically.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `beta` | float (keyword) | 0.1 | Algorithm gain. Higher = more accelerometer influence, lower = more gyroscope trust. |
| `fs` | float (keyword) | 100.0 | Sample rate in Hz. Determines integration step (sample period = 1/fs). |

| Method | Signature | Returns | Description |
|--------|-----------|---------|-------------|
| `update(ax, ay, az, gx, gy, gz)` | 7 args (self + 6 floats) | `(roll, pitch, yaw)` | Feed accelerometer (g) and gyroscope (rad/s) data. Returns Euler angles in degrees. |
| `quaternion()` | `quaternion()` | `(w, x, y, z)` | Read current orientation quaternion. |
| `reset()` | `reset()` | `None` | Reset to identity quaternion (1, 0, 0, 0). |

**Important:** Gyroscope input must be in **radians per second**, not degrees per second. BMI270 outputs deg/s by default -- multiply by `pi/180`.

**Beta tuning:**
- 0.01-0.04: Minimal accelerometer correction. Good for fast rotations where gyro drift is acceptable.
- 0.1: Default balance. Suitable for most applications.
- 0.5-1.0: Heavy accelerometer correction. Good for slow/static orientations but noisy during fast motion.

```python
import dsp, sensors, time

sensors.init()
ahrs = dsp.Madgwick(beta=0.1, fs=100.0)

while True:
    d = sensors.read()
    # Convert gyro from deg/s to rad/s
    gx = d['gx'] * 0.01745329
    gy = d['gy'] * 0.01745329
    gz = d['gz'] * 0.01745329

    roll, pitch, yaw = ahrs.update(d['ax'], d['ay'], d['az'], gx, gy, gz)
    print(f"Roll:{roll:.1f} Pitch:{pitch:.1f} Yaw:{yaw:.1f}")
    time.sleep_ms(10)
```

### dsp.Pedometer -- Step Counter

Detects walking steps from accelerometer magnitude using threshold crossing with hysteresis and minimum interval debounce. Uses `mp_hal_ticks_ms()` for timing.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `threshold` | float (keyword) | 1.5 | Acceleration magnitude threshold (g) to detect step peak. |
| `min_interval` | int (keyword) | 300 | Minimum time between steps in milliseconds (debounce). |

Hysteresis is automatically set to 80% of threshold.

| Method | Signature | Returns | Description |
|--------|-----------|---------|-------------|
| `update(ax, ay, az)` | 4 args (self + 3 floats) | `(steps, active)` | Feed accelerometer data. Returns cumulative step count and whether a step was detected on this sample. |
| `reset()` | `reset()` | `None` | Reset step count and timing state. |

**Detection algorithm:**
1. Compute magnitude: `sqrt(ax^2 + ay^2 + az^2)`
2. Rising edge: magnitude crosses above threshold
3. Debounce: at least `min_interval` ms since last step
4. Falling edge: magnitude drops below `threshold * 0.8` (hysteresis)

```python
import dsp, sensors, time

sensors.init()
ped = dsp.Pedometer(threshold=1.3, min_interval=250)

while True:
    d = sensors.read()
    steps, active = ped.update(d['ax'], d['ay'], d['az'])
    if active:
        print(f"Step detected! Total: {steps}")
    time.sleep_ms(10)
```

---

## 3. Stateless Functions

**Source:** `common/mpy/moddsp.c`

### IMU Functions

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `dsp.tilt(ax, ay, az)` | `tilt(ax, ay, az)` | `(roll, pitch)` | Compute tilt angles from accelerometer. Uses `atan2`. Roll = rotation around X, Pitch = rotation around Y. Output in degrees. |
| `dsp.compass(mx, my, mz)` | `compass(mx, my, mz)` | `float` | Compute heading from magnetometer. 0-360 degrees (0=North). `mz` reserved for future tilt compensation. |

### Environment Functions

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `dsp.altitude(pressure_hpa, sea_level=1013.25)` | `altitude(pressure, sea_level=1013.25)` | `float` | Barometric altitude in meters using hypsometric formula: `44330 * (1 - (p/p0)^0.190295)`. |
| `dsp.dew_point(temp_c, rh_percent)` | `dew_point(temp, rh)` | `float` | Dew point temperature in degrees C using Magnus-Tetens formula. |
| `dsp.heat_index(temp_c, rh_percent)` | `heat_index(temp, rh)` | `float` | Apparent temperature in degrees C using Rothfusz regression. Accounts for humidity effect on perceived temperature. |
| `dsp.comfort_zone(temp_c, rh_percent)` | `comfort_zone(temp, rh)` | `str` | Classify conditions into one of: `"cold"` (<18C), `"hot"` (>27C), `"dry"` (<30% RH), `"humid"` (>70% RH), `"comfortable"` (20-25C, 30-60% RH), `"acceptable"`. |

### Formulas

**Altitude (hypsometric):**
```
h = 44330 * (1 - (P / P0) ^ 0.190295)
```

**Dew Point (Magnus-Tetens):**
```
gamma = (17.27 * T) / (237.7 + T) + ln(RH / 100)
Td = (237.7 * gamma) / (17.27 - gamma)
```

**Heat Index (Rothfusz regression, when >= 80F):**
```
HI = -42.379 + 2.049*Tf + 10.143*RH - 0.225*Tf*RH
     - 0.00684*Tf^2 - 0.0548*RH^2 + 0.00123*Tf^2*RH
     + 0.000853*Tf*RH^2 - 0.00000199*Tf^2*RH^2
```

---

## 4. Use Case Reference

| Use Case | Recommended Filter/Function | Parameters |
|----------|----------------------------|------------|
| Smooth noisy temperature readings | `dsp.EMA(alpha=0.05)` or `dsp.Kalman1D(q=0.001, r=0.5)` | Low alpha / high r for stability |
| Remove accelerometer vibration | `dsp.LPF(cutoff=5.0, fs=100.0)` | Cutoff below vibration frequency |
| Detect sudden impacts | `dsp.HPF(cutoff=2.0, fs=100.0)` | Removes gravity, passes shocks |
| Clean radar distance readings | `dsp.Median(window=5)` | Removes spike noise |
| Rolling average for dashboard | `dsp.SMA(window=30)` | 30-sample window at 100Hz = 300ms |
| Stable barometric altitude | `dsp.Kalman1D(q=0.001, r=1.0)` | Very smooth altitude estimate |
| 3D orientation tracking | `dsp.Madgwick(beta=0.1, fs=100.0)` | Requires both accel + gyro |
| Step counting (fitness) | `dsp.Pedometer(threshold=1.3)` | Adjust threshold per user gait |
| Compass heading | `dsp.compass(mx, my, mz)` | Requires BMM350 magnetometer |
| Weather station dew point | `dsp.dew_point(temp, rh)` | Requires DPS368 + SHT40 |
| HVAC comfort assessment | `dsp.comfort_zone(temp, rh)` | Returns category string |

---

## 5. Board Support

| Feature | AI Kit | Eva Kit | Game Console |
|---------|--------|---------|--------------|
| `dsp` module | Yes | Yes | Yes |
| All filter classes | Yes | Yes | Yes |
| Madgwick AHRS | Yes | Yes | Yes |
| Pedometer | Yes | Yes | Yes |
| `tilt()`, `compass()` | Yes | Yes | Yes |
| `altitude()` | Yes (DPS368) | No DPS368 | No DPS368 |
| `dew_point()`, `heat_index()` | Yes (SHT40) | No SHT40 | No SHT40 |
| `comfort_zone()` | Yes (SHT40) | No SHT40 | No SHT40 |

Environment functions (`altitude`, `dew_point`, `heat_index`, `comfort_zone`) are available on all boards but require appropriate sensor data as input. Only the AI Kit has both DPS368 (pressure) and SHT40 (temperature/humidity) sensors on-board.

---

## 6. Complete Example: Environmental Monitor

```python
import dsp, sensors, time, ui

sensors.init()
ui.screen(792, 398)
ui.clear()

# Create filters for stable readings
temp_filter = dsp.Kalman1D(q=0.01, r=0.5)
pres_filter = dsp.EMA(alpha=0.05)
hum_filter  = dsp.SMA(window=20)

# UI widgets
lbl_temp = ui.Label("Temp: --", x=20, y=20)
lbl_alt  = ui.Label("Alt: --",  x=20, y=60)
lbl_dew  = ui.Label("Dew: --",  x=20, y=100)
lbl_zone = ui.Label("Zone: --", x=20, y=140)

while True:
    d = sensors.read()

    temp = temp_filter.update(d['temperature'])
    pres = pres_filter.update(d['pressure'])
    hum  = hum_filter.update(d['humidity'])

    alt  = dsp.altitude(pres)
    dew  = dsp.dew_point(temp, hum)
    zone = dsp.comfort_zone(temp, hum)

    lbl_temp.set_text(f"Temp: {temp:.1f} C")
    lbl_alt.set_text(f"Alt: {alt:.0f} m")
    lbl_dew.set_text(f"Dew: {dew:.1f} C")
    lbl_zone.set_text(f"Zone: {zone}")

    time.sleep_ms(200)
```

---

## Source Files

| File | Path |
|------|------|
| Module shell + stateless functions | `common/mpy/moddsp.c` |
| Filter classes (EMA, SMA, LPF, HPF, Median, Kalman1D) | `common/mpy/moddsp_filters.c` |
| IMU classes (Madgwick, Pedometer) | `common/mpy/moddsp_imu.c` |

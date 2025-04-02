from pathlib import Path

import numpy as np
from matplotlib import pyplot as plt

# config PI
kp = 1.1
ki = 0.9
sum_limit = 10e6
out_limit = 200e3
stage = 0

# config simulation
f_pru = 201e6
f_pru_spec = 200e6
t_interval = 0.1

tick_interval_ns = 1e9 / f_pru_spec
t_interval_ticks = 0.1 * 1e9 / tick_interval_ns

# internals
kp_int = kp
ki_int = ki * t_interval
steps = 300
result = np.zeros(shape=(steps, 5))

# initial state
offset_ns = -20e6
output_sum = 0
input_last = 0
correction_ticks = 0
input_smooth = None

# simulation
for idx in range(steps):
    offset_ns += 1e9 * ((t_interval_ticks + correction_ticks) / f_pru - t_interval)

    input_now = offset_ns

    if input_now < -1e9 * t_interval / 2:
        input_now += 1e9 * t_interval
    elif input_now > 1e9 * t_interval / 2:
        input_now -= 1e9 * t_interval

    error = -input_now
    input_diff = input_now - input_last

    if not (-1e9 * t_interval / 2 < input_now < 1e9 * t_interval / 2):
        print("INPUT is out of bounds")
    if not (-1e9 * t_interval < input_diff < 1e9 * t_interval):
        print("INPUT_DIFF is out of bounds")

    if input_smooth is None:
        input_smooth = abs(input_now)
    else:
        input_smooth = (5 * input_smooth + abs(input_now)) / 6

    if (input_smooth < 1e6) and (stage == 0):
        kp_int *= 0.5
        ki_int *= 0.5
        stage = 1
    if (input_smooth < 200e3) and (stage == 1):
        kp_int *= 0.5
        ki_int *= 0.5
        stage = 2
    if (input_smooth < 20e3) and (stage == 2):
        kp_int *= 0.5
        ki_int *= 0.5
        stage = 3
    if (input_smooth > 2e6) and (stage > 0):
        kp_int = kp
        ki_int = ki * t_interval
        state = 0

    output_sum += ki_int * error
    output_sum = min(output_sum, sum_limit)
    output_sum = max(output_sum, -sum_limit)

    output = kp_int * error
    output += output_sum
    output = min(output, out_limit)
    output = max(output, -out_limit)

    correction_ticks = output

    input_last = input_now

    result[idx, :] = [idx * t_interval, input_now, input_smooth, output_sum, correction_ticks]

fig, axs = plt.subplots(4, 1, sharex="all", figsize=(20, 4 * 6), layout="tight")

axs[0].set_ylabel("offset [ns]")
axs[0].plot(result[:, 0], result[:, 1:3])
axs[0].set_ylim(bottom=-2000000, top=2000000)

axs[1].set_ylabel("offset [ns]")
axs[1].plot(result[:, 0], result[:, 1:3])
axs[1].set_ylim(bottom=-1000, top=1000)

axs[2].set_ylabel("out_sum [ticks]")
axs[2].plot(result[:, 0], result[:, 3])

axs[3].set_ylabel("output [ticks]")
axs[3].plot(result[:, 0], result[:, 4])

axs[3].set_xlabel("Runtime [s]")

for ax in axs:
    # deactivates offset-creation for ax-ticks
    ax.get_yaxis().get_major_formatter().set_useOffset(False)
    ax.get_xaxis().get_major_formatter().set_useOffset(False)

path_here = Path(__file__).with_suffix(".png")
plt.savefig(path_here)
plt.close(fig)
plt.clf()

# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent vectorized reference for the complex matched-filter kernel."""

import numpy as np
from dsp_rescue.soft import window
from dsp_rescue.dsp import payloads, crc_batch


def decode(iq, starts):
    padded = window(iq, starts, margin=4)
    packet = padded[:, 4:484]
    positions = np.array([0, 1, 4, 5, 14, 15, 18, 19])
    frequencies = np.arange(-100000, 100001, 5000)
    rotation = np.exp(-2j * np.pi * frequencies[:, None] * positions[None, :] / 4000000)
    coherent = np.abs(packet[:, positions] @ rotation.T) ** 2
    selected = frequencies[np.argmax(coherent, axis=1)]
    z = padded * np.exp(
        -2j * np.pi * selected[:, None] * (np.arange(488) - 4)[None, :] / 4000000
    )
    offsets = np.arange(-1, 3)
    shape = z[:, 4 + np.array([0, 4, 14, 18])[:, None] + offsets[None, :]].mean(axis=1)
    starts = 4 + 32 + np.arange(112) * 4
    left = z[:, starts[:, None] + offsets[None, :]]
    right = z[:, starts[:, None] + 2 + offsets[None, :]]
    logits = (
        np.abs(np.einsum("nbt,nt->nb", left, shape.conj())) ** 2
        - np.abs(np.einsum("nbt,nt->nb", right, shape.conj())) ** 2
    )
    pred = payloads(logits)
    valid = (crc_batch(pred) == 0) & ((pred[:, 0] >> 3) == 17)
    return {(int(i), pred[i].tobytes().hex()) for i in np.flatnonzero(valid)}

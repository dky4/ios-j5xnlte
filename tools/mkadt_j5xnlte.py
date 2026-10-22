#!/usr/bin/env python3
"""Build an entry-only j5xnlte ADT from the donor's minimal schema.

This includes the msm8916 GICv2 and eMMC MMIO window for a read-only
probe. It is not yet an iOS root/device tree. See docs/j5xnlte-bringup.md.
"""
import argparse
import os
from pathlib import Path

import adt
import mkadt


def set_prop(node, name, value):
    for i, (key, flags, _) in enumerate(node.props):
        if key == name:
            node.props[i] = (key, flags, value)
            return
    node.props.append((name, 0, value))


def child(node, name):
    for candidate in node.children:
        if candidate.prop('name') == mkadt.cstr(name):
            return candidate
    raise ValueError(f'missing donor ADT node {name}')


def build():
    root = mkadt.build(seed=os.urandom(64), qemu=True)
    set_prop(root, 'compatible', mkadt.cstr('samsung,j5xnlte') +
             mkadt.cstr('qcom,msm8916') + mkadt.cstr('AppleARM'))
    chosen = child(root, 'chosen')
    buttons = child(root, 'buttons')
    # The J5 has a mechanical Home button (PM8916/GPIO, driver later).
    set_prop(buttons, 'home-button-type', mkadt.u32(0))
    set_prop(buttons, 'button-names', b''.join(mkadt.cstr(name) for name in
                                       ('volup', 'voldown', 'hold', 'home')))
    # 720x1280 @2x = 360x640 points (SpringBoard FBSDisplayMode).
    set_prop(chosen, 'display-scale', mkadt.u32(2))
    # Kernel-managed RAM starts 128 MiB above DRAM so lk2nd's own load
    # addresses (kernel ~0x80080000, ramdisk ~0x82000000) stay below it.
    # mkpayload.py takes phys_base/mem_size from these properties.
    set_prop(chosen, 'dram-base', mkadt.u64(0x88000000))
    set_prop(chosen, 'dram-size', mkadt.u64(0x78000000))
    set_prop(chosen, 'firmware-version', mkadt.cstr('j5xnlte-entry'))
    set_prop(chosen, 'system-firmware-version', mkadt.cstr('j5xnlte-entry'))
    cpus = child(root, 'cpus')
    cpu0 = child(cpus, 'cpu0')
    set_prop(cpu0, 'compatible', mkadt.cstr('arm,cortex-a53') + mkadt.cstr('ARM,v8'))
    set_prop(cpu0, 'reg', mkadt.u32(0))
    # The msm8916 architected-timer DT runs at 19.2 MHz (from TCXO).
    set_prop(cpu0, 'timebase-frequency', mkadt.u32(19200000))
    # Same descriptive values as the added cores: all four A53s are identical.
    set_prop(cpu0, 'clock-frequency', mkadt.u32(1209600000))
    set_prop(cpu0, 'memory-frequency', mkadt.u32(533000000))
    # The msm8916 DT has one A53 cluster of four cores, MPIDR 0..3. The
    # cpus=N boot argument still selects a prefix, so cpus=1 and cpus=4
    # remain valid.
    for logical in range(1, 4):
        cpus.children.append(mkadt.node(f'cpu{logical}', [
            ('device_type', mkadt.cstr('cpu')),
            ('compatible', mkadt.cstr('arm,cortex-a53') + mkadt.cstr('ARM,v8')),
            ('reg', mkadt.u32(logical)),
            ('cpu-id', mkadt.u32(logical)),
            ('cluster-id', mkadt.u32(0)),
            ('cluster-type', mkadt.u32(0)),
            ('die-cluster-id', mkadt.u32(0)),
            ('cluster-core-id', mkadt.u32(logical)),
            # One shared 512 KiB L2 for the single cluster.
            ('l2-cache-id', mkadt.u32(0)),
            ('l3-cache-id', mkadt.u32(0)),
            ('state', mkadt.cstr('stopped')),
            ('timebase-frequency', mkadt.u32(19200000)),
            ('clock-frequency', mkadt.u32(1209600000)),
            ('bus-frequency', mkadt.u32(100000000)),
            ('memory-frequency', mkadt.u32(533000000)),
            ('peripheral-frequency', mkadt.u32(19200000)),
            ('fixed-frequency', mkadt.u32(19200000)),
        ]))
    arm_io = child(root, 'arm-io')
    set_prop(arm_io, 'device_type', mkadt.cstr('j5xnlte-io'))
    set_prop(arm_io, 'compatible', mkadt.cstr('qcom,msm8916-io') +
             mkadt.cstr('arm-io'))
    # /arm-io children are relative to the msm8916 MMIO base 0x01000000.
    set_prop(arm_io, 'ranges', mkadt.u64(0, 0x1000000, 0xff000000))
    arm_io.children = [n for n in arm_io.children if n.prop('name') == mkadt.cstr('wdt')]
    arm_io.children.append(mkadt.node('gic', [
        ('compatible', mkadt.cstr('arm,cortex-a15-gic')),
        ('interrupt-controller', mkadt.cstr('master')),
        # msm8916 DT: PPI 2/3/4/1 (secure/non-secure/virtual/hyp) puts the
        # virtual timer at INTID 20. sm7150 uses 19; do not transfer that
        # value here without a device measurement.
        ('virtual-timer-intid', mkadt.u32(20)),
        # GICD 0x0b000000 / GICC 0x0b002000, relative to 0x01000000.
        ('reg', mkadt.u64(0x0a000000, 0x1000, 0x0a002000, 0x1000)),
    ]))
    root.children = [n for n in root.children if n.prop('name') not in
                     (mkadt.cstr('sweet-fbcon'), mkadt.cstr('sweet-ufs'))]
    # Read-only eMMC window (sdhc_1 core, absolute physical register) for a
    # later GPT probe. Display (disp0/fbcon) comes with the MDP5 milestone.
    root.children.append(mkadt.node('j5xnlte-emmc', [
        ('reg', mkadt.u64(0x07824900, 0x2000)),
    ]))
    return root


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    blob = adt.serialize(build())
    args.output.write_bytes(blob)
    print(f'{args.output}: {len(blob)} bytes')

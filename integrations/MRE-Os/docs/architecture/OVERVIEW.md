# MRE OS Architecture

## Layer boundaries

- Boot: validates handoff state and transfers control to the AArch64 kernel.
- Kernel: exceptions, virtual memory, scheduler, interrupts, timers, synchronization and capability enforcement.
- HAL: stable, OS-native contracts for services; no Android ABI.
- Drivers: bus and device implementations using kernel-owned APIs.
- Services: storage, display, input, audio, power, network and location.
- Framework: application-facing APIs and permissions.
- Runtime: isolated execution environment for MRE applications.
- UI: compositor, shell, settings and recovery interface.

## Driver contract

Drivers declare a compatible string and probe/remove callbacks. Device discovery is responsible for constructing validated device descriptors from the hardware manifest/DTB. Drivers must not access arbitrary MMIO, DMA or IRQ resources without kernel-issued capabilities.

## Current boundary

The current repository implements only a host-testable driver registry and firmware policy interface. Kernel resource APIs, synchronization, bus enumeration, DMA, interrupts, firmware loading, boot and services are future milestones. Do not compile hardware driver stubs as if they were working drivers.

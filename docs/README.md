# Documentation Index

Technical guides for the BlackPill SUMP Logic Analyzer project.

## Quick Navigation

### 📖 Getting Started
- **[README.md](../README.md)** ← Start here! Project overview, quick-start, specifications

### 🏗️ Architecture & Design
- **[ARCHITECTURE.md](ARCHITECTURE.md)** - Deep-dive into system design, data flow, timing
- **[FIRMWARE.md](FIRMWARE.md)** - Firmware internals: clock config, protocol engines, sampling
- **[PROTOCOL.md](PROTOCOL.md)** - Detailed SUMP & framed protocol specifications (coming soon)

### 💻 Development Tools
- **[PYTHON_TOOLS.md](PYTHON_TOOLS.md)** - Host tools guide: CLI, Qt6 GUI, validation scripts
- **[DEBUGGING.md](DEBUGGING.md)** - Debug workflow, breakpoints, VSCode integration, J-Link setup

### ⚙️ Hardware
- **[HARDWARE.md](HARDWARE.md)** - Pinouts, GPIO mapping, power consumption, mechanical (coming soon)

---

## Document Summary

| Document | Purpose | Audience | Level |
|----------|---------|----------|-------|
| README.md | Project overview, quick-start | Everyone | Beginner |
| ARCHITECTURE.md | System design, data flow, state machines | Engineers | Intermediate–Advanced |
| FIRMWARE.md | Firmware development, code walkthrough | Firmware developers | Intermediate–Advanced |
| PYTHON_TOOLS.md | Host tool usage, API reference | Users, developers | Beginner–Intermediate |
| DEBUGGING.md | Debug workflow, testing strategy | Developers | Intermediate |
| PROTOCOL.md | Protocol specifications, wire format | Protocol implementers | Advanced |
| HARDWARE.md | Electrical specs, schematics, connectors | Hardware engineers | Intermediate |

---

## File Organization

```
docs/
├── README.md                  ← This file
├── ARCHITECTURE.md            ← System design
├── FIRMWARE.md                ← Firmware development
├── PYTHON_TOOLS.md            ← Host tools
├── DEBUGGING.md               ← Debug workflow
├── PROTOCOL.md                ← Protocol specs (planned)
└── HARDWARE.md                ← Hardware details (planned)
```

---

## Key Sections by Topic

### 🔧 Building & Flashing
- [FIRMWARE.md: Building & Debugging](FIRMWARE.md#building--debugging)
- [README.md: Getting Started](../README.md#getting-started)

### 📊 Sampling & Timing
- [ARCHITECTURE.md: Sampling Engine](ARCHITECTURE.md#sampling-engine)
- [FIRMWARE.md: System Initialization](FIRMWARE.md#system-initialization)

### 🔄 Protocol Details
- [ARCHITECTURE.md: Protocol Specifications](../README.md#protocol-specifications)
- [PYTHON_TOOLS.md: Protocol Implementation](PYTHON_TOOLS.md#protocol-implementation)

### 🪲 Debugging & Testing
- [DEBUGGING.md: Recommended Breakpoints](DEBUGGING.md#suggested-breakpoints)
- [PYTHON_TOOLS.md: Self-Test Suite](PYTHON_TOOLS.md#self-test-suite)

### 📈 Performance
- [ARCHITECTURE.md: Performance Tuning](ARCHITECTURE.md#performance-tuning)
- [FIRMWARE.md: Optimization Techniques](FIRMWARE.md#optimization-techniques)

---

## Reading Order (Recommended)

### For Users
1. [README.md](../README.md) - Understand what the project does
2. [PYTHON_TOOLS.md](PYTHON_TOOLS.md) - Learn how to use the tools
3. [DEBUGGING.md](DEBUGGING.md) - Troubleshoot issues

### For Hardware Developers
1. [README.md](../README.md) - Project context
2. [HARDWARE.md](HARDWARE.md) - Electrical specifications
3. [ARCHITECTURE.md](ARCHITECTURE.md) - How it works together

### For Firmware Developers
1. [README.md](../README.md) - Project overview
2. [FIRMWARE.md](FIRMWARE.md) - Code walkthrough
3. [ARCHITECTURE.md](ARCHITECTURE.md) - Design principles
4. [DEBUGGING.md](DEBUGGING.md) - Development workflow
5. [PROTOCOL.md](PROTOCOL.md) - Protocol details

### For Integration/Porting
1. [ARCHITECTURE.md](ARCHITECTURE.md) - Understand the design
2. [FIRMWARE.md](FIRMWARE.md) - Firmware structure
3. [PROTOCOL.md](PROTOCOL.md) - Protocol specifications
4. [DEBUGGING.md](DEBUGGING.md) - Validation techniques

---

## FAQ

**Q: Where do I start?**
A: Begin with [README.md](../README.md) for quick-start instructions, then jump to [PYTHON_TOOLS.md](PYTHON_TOOLS.md) if you want to use the host tools.

**Q: How do I build and flash the firmware?**
A: See [README.md: Getting Started](../README.md#getting-started) or [FIRMWARE.md: Building & Debugging](FIRMWARE.md#building--debugging).

**Q: How do I debug the firmware?**
A: Check [DEBUGGING.md](DEBUGGING.md) for breakpoints, VSCode configuration, and J-Link setup.

**Q: What are the protocol specifications?**
A: Read [PROTOCOL.md](PROTOCOL.md) (planned) or [README.md: Protocol Specifications](../README.md#protocol-specifications).

**Q: How can I extend the project?**
A: See [ARCHITECTURE.md: Future Enhancements](ARCHITECTURE.md#known-limitations--future-work) for improvement ideas.

---

## Contributing

Found a documentation error or unclear explanation? Please submit an issue or pull request!

## License

MIT License. See [LICENSE](../LICENSE).


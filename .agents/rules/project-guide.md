---
trigger: always_on
---

This project is a Raspberry Pi Pico RP2040 based standing desk controller. Built in C++.

## Overview

- Check README.md for overview
- 4 buttons, a small OLED display that shows the current height and "state" of the controller
- RP2040 that communicates over UART to the desk
- The user can store desk heights as "presets" and the controller can auto-drive itself to the stored height when the user asks to.

## Code Style

- Comments used only when they are needed to actually explain something. Prefer functions and self-documenting code.
- C++ must be used in a safe way: prefer C++ abstractions over C that guarantee some safety aspects.
- State machines explicitly defined in code if possible to allow more easily reading how the system works

## Code change workflow

- Changes MUST be made in small steps that can be separately committed to Git. Steps are planned first and implemented one-by-one later.
- All change work must be done in a separate Git branch
- Changes MUST NEVER EVER be pushed to git - only humans have the permission to do that
- Commit messages must concisely describe the change in the commit. Prefer one-line commit messages.

## Building the project

- Go to the build/ directory and run `ninja all`
- If the build directory does not exist, or ninja errors out due to a build config error, run `cmake .` inside the build dir
- NEVER EVER try to remove the build directory to "clean" the state - just ask a human to resolve the build error then.
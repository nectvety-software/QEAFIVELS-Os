# System Apps, Locale, Storage and Power

Read .agents/AGENTS.md first. Core contracts live in system/core/include/vqeaf.

- Default locale is vi-VN, UTF-8; en-US is fallback only.
- Default font family is Ubuntu; bundle original font binaries and Ubuntu Font Licence text together.
- UI must render Vietnamese combining/precomposed diacritics correctly; test full Vietnamese pangram and uppercase/lowercase accents.
- Screen timeout choices are exactly 15 seconds, 1 minute, 5 minutes, 30 minutes and Never. Default is 1 minute.
- Use monotonic time. Genuine user activity resets idle deadline. Timeout only blanks display; never stop calls, audio, alarms, emergency services or kernel scheduling.
- Disable timeout control when display capability is not READY; never pretend a screen was powered off.
- Storage paths are absolute, traversal-safe and resolved through mounted VFS roots. Require confirmation for destructive file operations.
- Never claim persistent storage until a real block driver and filesystem implementation are present.

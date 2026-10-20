#pragma once

// AQTRunCat's own neutral icon for the Codex integration (original artwork,
// same license as AQTRunCat): code brackets inside a gauge ring, in a colour
// distinct from the Claude Code icon. It is not, and does not imitate, any
// OpenAI logo.
inline constexpr char CodexMetricIconSvg[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <circle cx="32" cy="32" r="27" fill="none" stroke="#7f8c9a" stroke-width="6" opacity="0.45"/>
  <path d="M32 5 A27 27 0 0 1 55.4 45.5" fill="none" stroke="#2fa37d" stroke-width="6" stroke-linecap="round"/>
  <rect x="16" y="20" width="32" height="24" rx="5" fill="#2fa37d"/>
  <path d="M26 26 L20 32 L26 38" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M38 26 L44 32 L38 38" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M34.5 24.5 L29.5 39.5" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
</svg>
)svg";

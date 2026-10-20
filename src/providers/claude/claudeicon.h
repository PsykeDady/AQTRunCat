#pragma once

// AQTRunCat's own neutral icon for the Claude Code metric (original artwork,
// same license as AQTRunCat): a terminal prompt inside a gauge ring. It is
// not, and does not imitate, any Anthropic logo.
inline constexpr char ClaudeMetricIconSvg[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <circle cx="32" cy="32" r="27" fill="none" stroke="#7f8c9a" stroke-width="6" opacity="0.45"/>
  <path d="M32 5 A27 27 0 0 1 58.6 36.7" fill="none" stroke="#3d9be9" stroke-width="6" stroke-linecap="round"/>
  <rect x="17" y="21" width="30" height="22" rx="4" fill="#3d9be9"/>
  <path d="M22 27 L28 32 L22 37" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M31 37 H41" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
</svg>
)svg";
